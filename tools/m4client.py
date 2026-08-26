#!/usr/bin/env python3
"""Client for the Matrix Portal M4 signed API.

Uses only the standard library.

    python tools/m4client.py --host 192.168.1.50 pair
    python tools/m4client.py --host 192.168.1.50 get /api/status
    python tools/m4client.py --host 192.168.1.50 post /api/led '{"on": true}'
    python tools/m4client.py --host 192.168.1.50 get /api/clients
    python tools/m4client.py --host 192.168.1.50 delete /api/clients/<id>

`pair` only succeeds inside the 60 second window opened by pressing UP on the
board, and stores the returned credentials in --credentials (default
./.m4-credentials.json).
"""

import argparse
import hashlib
import hmac
import json
import os
import sys
import time
import urllib.error
import urllib.request

DEFAULT_CREDENTIALS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                                   ".m4-credentials.json")


def canonical_request(method, target, ts, nonce, body):
    """Must match buildCanonicalRequest() in lib/apiauth/request_sig.cpp."""
    return "\n".join([
        method.upper(),
        target,
        str(ts),
        nonce,
        hashlib.sha256(body).hexdigest(),
    ]).encode()


def sign(secret_hex, method, target, body):
    ts = int(time.time())
    nonce = os.urandom(8).hex()
    canonical = canonical_request(method, target, ts, nonce, body)
    signature = hmac.new(bytes.fromhex(secret_hex), canonical, hashlib.sha256).hexdigest()
    return ts, nonce, signature


def request(host, method, target, body=b"", headers=None, timeout=10):
    url = "http://{}{}".format(host, target)
    req = urllib.request.Request(url, data=body if body else None, method=method.upper())
    for name, value in (headers or {}).items():
        req.add_header(name, value)
    if body:
        req.add_header("Content-Type", "application/json")

    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            return response.status, response.read().decode()
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode()


def load_credentials(path):
    if not os.path.exists(path):
        sys.exit("No credentials at {}. Press UP on the board, then run `pair`.".format(path))
    with open(path) as handle:
        return json.load(handle)


def cmd_info(args):
    status, payload = request(args.host, "GET", "/")
    print(status, payload)


def cmd_pair(args):
    status, payload = request(args.host, "POST", "/pair")
    if status != 200:
        print(status, payload)
        if status == 401:
            print("\nThe pairing window is closed. Press UP on the board and retry within 60s.")
        return 1

    credentials = json.loads(payload)
    with open(args.credentials, "w") as handle:
        json.dump(credentials, handle, indent=2)
    # The secret is shown once by the board and never again.
    os.chmod(args.credentials, 0o600)

    print("Paired as {}".format(credentials["client_id"]))
    print("Credentials written to {}".format(args.credentials))
    return 0


def cmd_call(args):
    credentials = load_credentials(args.credentials)
    body = args.body.encode() if args.body else b""

    ts, nonce, signature = sign(credentials["secret"], args.method, args.target, body)
    authorization = "HMAC id={},ts={},nonce={},sig={}".format(
        credentials["client_id"], ts, nonce, signature)

    status, payload = request(args.host, args.method, args.target, body,
                              {"Authorization": authorization})
    print(status, payload)

    if status == 401 and "stale_timestamp" in payload:
        print("\nThe board rejected the timestamp. Check this machine's clock against NTP.")
    return 0 if 200 <= status < 300 else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True, help="board address, e.g. 192.168.1.50")
    parser.add_argument("--credentials", default=os.path.normpath(DEFAULT_CREDENTIALS))

    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("info", help="unauthenticated GET /").set_defaults(func=cmd_info)
    sub.add_parser("pair", help="POST /pair during the pairing window").set_defaults(func=cmd_pair)

    for method in ("get", "post", "put", "delete"):
        call = sub.add_parser(method, help="signed {} request".format(method.upper()))
        call.add_argument("target", help="path including any query string, e.g. /api/status")
        call.add_argument("body", nargs="?", default="", help="raw JSON body")
        call.set_defaults(func=cmd_call, method=method)

    args = parser.parse_args()
    return args.func(args) or 0


if __name__ == "__main__":
    sys.exit(main())
