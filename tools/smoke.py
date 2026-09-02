#!/usr/bin/env python3
"""On-hardware smoke test for the MatrixFaces API.

Walks every route -- and every documented error path -- against a real board
and checks the status code and error code of each. The expectations were
captured from the firmware before the router was extracted from main.cpp, so
this doubles as the regression check that the extraction preserved behaviour.

    python tools/smoke.py --host 192.168.1.50

Needs credentials from a previous `m4client.py pair`. Settings it changes are
restored before it exits, so it is safe to run against a board you care about.

Exits non-zero if any check fails.
"""

import argparse
import json
import os
import socket
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from m4client import DEFAULT_CREDENTIALS, load_credentials, request, sign  # noqa: E402


class Smoke:
    def __init__(self, host, credentials):
        self.host = host
        self.credentials = credentials
        self.failures = []
        self.checks = 0

    # -- request helpers ----------------------------------------------------

    def signed(self, method, target, body=b""):
        ts, nonce, signature = sign(self.credentials["secret"], method, target, body)
        authorization = "HMAC id={},ts={},nonce={},sig={}".format(
            self.credentials["client_id"], ts, nonce, signature)
        return request(self.host, method, target, body,
                       {"Authorization": authorization})

    def unsigned(self, method, target, body=b""):
        return request(self.host, method, target, body)

    # -- assertions ---------------------------------------------------------

    def check(self, label, status, payload, expect_status, expect_error=None):
        self.checks += 1
        problems = []
        if status != expect_status:
            problems.append("status {} (want {})".format(status, expect_status))
        if expect_error is not None:
            try:
                actual = json.loads(payload).get("error")
            except (ValueError, AttributeError):
                actual = None
            if actual != expect_error:
                problems.append("error {!r} (want {!r})".format(actual, expect_error))

        if problems:
            self.failures.append("{}: {}".format(label, "; ".join(problems)))
            print("  FAIL  {:<44} {}".format(label, "; ".join(problems)))
        else:
            print("  ok    {:<44} {} {}".format(label, status,
                                                (payload or "")[:56]))
        return not problems

    def expect_signed(self, method, target, expect_status, expect_error=None, body=None):
        raw = json.dumps(body).encode() if body is not None else b""
        status, payload = self.signed(method, target, raw)
        label = "{} {}".format(method, target)
        self.check(label, status, payload, expect_status, expect_error)
        return payload

    def expect_unsigned(self, method, target, expect_status, expect_error=None):
        status, payload = self.unsigned(method, target)
        self.check("{} {} (unsigned)".format(method, target), status, payload,
                   expect_status, expect_error)
        return payload

    # -- raw socket, for cases urllib will not send -------------------------

    def raw(self, label, request_bytes, expect_status, timeout=12):
        self.checks += 1
        try:
            sock = socket.create_connection((self.host, 80), timeout=timeout)
            sock.settimeout(timeout)
            sock.sendall(request_bytes)
            data = b""
            while True:
                chunk = sock.recv(2048)
                if not chunk:
                    break
                data += chunk
            sock.close()
        except OSError as error:
            self.failures.append("{}: {}".format(label, error))
            print("  FAIL  {:<44} {}".format(label, error))
            return

        line = data.split(b"\r\n")[0].decode("utf-8", "replace")
        if " {} ".format(expect_status) not in line + " ":
            self.failures.append("{}: {!r} (want {})".format(label, line, expect_status))
            print("  FAIL  {:<44} {!r}".format(label, line))
        else:
            print("  ok    {:<44} {}".format(label, line))


def run(smoke):
    print("\nDiscovery and pairing (unauthenticated)")
    info = smoke.expect_unsigned("GET", "/", 200)
    print("        firmware:", json.loads(info).get("firmware_version"))
    smoke.expect_unsigned("POST", "/pair", 401, "pairing_closed")

    print("\nAuthentication gate")
    # Unknown endpoints under /api must answer 401, not 404: the API surface is
    # not enumerable without credentials.
    smoke.expect_unsigned("GET", "/api/status", 401, "missing_authorization")
    smoke.expect_unsigned("GET", "/api/nonexistent", 401, "missing_authorization")
    # ...while anything outside /api is a plain 404 either way.
    smoke.expect_unsigned("GET", "/nope", 404, "unknown_endpoint")
    smoke.expect_unsigned("POST", "/", 404, "unknown_endpoint")
    smoke.expect_unsigned("GET", "/pair", 404, "unknown_endpoint")
    smoke.expect_unsigned("GET", "/api", 404, "unknown_endpoint")

    print("\nRead routes")
    smoke.expect_signed("GET", "/api/status", 200)
    smoke.expect_signed("GET", "/api/clients", 200)
    smoke.expect_signed("GET", "/api/app", 200)
    apps = smoke.expect_signed("GET", "/api/apps", 200)
    smoke.expect_signed("GET", "/api/metrics", 200)

    # GET /api/apps used to be assembled in a 1 KB buffer and silently cut off.
    # Whatever its size now, it must be complete, parseable JSON.
    try:
        parsed = json.loads(apps)
        print("        /api/apps: {} bytes, {} apps, parses cleanly".format(
            len(apps), len(parsed.get("apps", []))))
    except ValueError as error:
        smoke.failures.append("/api/apps is not valid JSON: {}".format(error))
        print("  FAIL  /api/apps did not parse: {}".format(error))

    print("\nWrong methods (404, never 405)")
    smoke.expect_signed("POST", "/api/status", 404, "unknown_endpoint")
    smoke.expect_signed("GET", "/api/led", 404, "unknown_endpoint")
    smoke.expect_signed("DELETE", "/api/app", 404, "unknown_endpoint")
    smoke.expect_signed("GET", "/api/status/", 404, "unknown_endpoint")

    print("\nApp settings wildcard")
    smoke.expect_signed("GET", "/api/apps/0/settings", 200)
    # A numeric-but-unregistered index names a missing app...
    smoke.expect_signed("GET", "/api/apps/9/settings", 404, "unknown_app_index")
    # ...while a non-numeric one never named an endpoint at all.
    smoke.expect_signed("GET", "/api/apps/abc/settings", 404, "unknown_endpoint")
    smoke.expect_signed("GET", "/api/apps/999/settings", 404, "unknown_endpoint")
    smoke.expect_signed("GET", "/api/apps/0/other", 404, "unknown_endpoint")
    smoke.expect_signed("GET", "/api/apps/0", 404, "unknown_endpoint")
    # Deliberately stricter than the strtoul() this replaced, which accepted a
    # leading '+' and served app 1.
    smoke.expect_signed("GET", "/api/apps/+1/settings", 404, "unknown_endpoint")

    print("\nClient revocation wildcard")
    smoke.expect_signed("DELETE", "/api/clients/00112233445566zz", 400, "invalid_client_id")
    smoke.expect_signed("DELETE", "/api/clients/0011223344556677", 404, "unknown_client")
    smoke.expect_signed("DELETE", "/api/clients/", 400, "invalid_client_id")

    print("\nWrite routes and validation")
    original = json.loads(smoke.expect_signed("GET", "/api/apps/0/settings", 200))
    smoke.expect_signed("POST", "/api/led", 200, body={"on": True})
    smoke.expect_signed("POST", "/api/led", 400, "expected_on_boolean", body={"on": "yes"})
    smoke.expect_signed("POST", "/api/app", 400, "unknown_app_index", body={"index": 99})
    smoke.expect_signed("POST", "/api/app", 400, "expected_index_integer", body={"index": "one"})
    smoke.expect_signed("POST", "/api/apps/0/settings", 400, "invalid_setting_value",
                        body={"size": 99})
    smoke.expect_signed("POST", "/api/apps/0/settings", 400, "no_recognized_settings",
                        body={"bogus": 1})

    # A key whose characters also appear inside a string *value* must not be
    # mistaken for the key itself. This used to 400.
    text_app = None
    try:
        for app in json.loads(apps).get("apps", []):
            if app.get("name") == "text":
                text_app = app["index"]
    except ValueError:
        pass
    if text_app is not None:
        before = json.loads(smoke.expect_signed(
            "GET", "/api/apps/{}/settings".format(text_app), 200))
        payload = smoke.expect_signed("POST", "/api/apps/{}/settings".format(text_app), 200,
                                      body={"text": 'say "size" now'})
        try:
            applied = json.loads(payload).get("text")
            smoke.checks += 1
            if applied == 'say "size" now':
                print("  ok    quoted-key-in-value applies (regression)")
            else:
                smoke.failures.append("quoted key in value: text is {!r}".format(applied))
                print("  FAIL  quoted-key-in-value: text is {!r}".format(applied))
        except ValueError:
            pass
        smoke.expect_signed("POST", "/api/apps/{}/settings".format(text_app), 200, body=before)

    print("\nMalformed requests (raw sockets)")
    smoke.raw("malformed request line", b"GARBAGE\r\n\r\n", 400)
    smoke.raw("oversized target", b"GET /" + b"a" * 200 + b" HTTP/1.1\r\nHost: x\r\n\r\n", 414)
    smoke.raw("oversized header",
              b"GET /api/status HTTP/1.1\r\nHost: x\r\nAuthorization: " + b"b" * 400 + b"\r\n\r\n",
              431)
    smoke.raw("body over the cap",
              b"POST /pair HTTP/1.1\r\nHost: x\r\nContent-Length: 257\r\n\r\n" + b"y" * 257, 413)
    smoke.raw("truncated body times out",
              b"POST /pair HTTP/1.1\r\nHost: x\r\nContent-Length: 100\r\n\r\n{}", 408)

    print("\nRestoring")
    smoke.expect_signed("POST", "/api/led", 200, body={"on": False})
    smoke.expect_signed("POST", "/api/apps/0/settings", 200, body=original)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True, help="board address, e.g. 192.168.1.50")
    parser.add_argument("--credentials", default=os.path.normpath(DEFAULT_CREDENTIALS))
    args = parser.parse_args()

    smoke = Smoke(args.host, load_credentials(args.credentials))
    run(smoke)

    print("\n{} checks, {} failed".format(smoke.checks, len(smoke.failures)))
    for failure in smoke.failures:
        print("  - {}".format(failure))
    return 1 if smoke.failures else 0


if __name__ == "__main__":
    sys.exit(main())
