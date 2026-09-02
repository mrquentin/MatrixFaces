#!/usr/bin/env python3
"""Stand-in for MultiViewer's local GraphQL API, for testing the F1 app.

Serves the same POST /api/graphql endpoint on port 10101 that MultiViewer does,
replaying canned live-timing states so the F1 app can be exercised without a
race weekend.

    python tools/mv_mock.py                      # cycle through every scenario
    python tools/mv_mock.py --scenario red       # hold one state
    python tools/mv_mock.py --hold 10            # 10s per scenario
    python tools/mv_mock.py --delay 3            # stall 3s before responding

Point the board at it:

    python tools/m4client.py --host <board> post /api/apps/2/settings \\
        '{"host": "<this machine>"}'

`--delay` is the interesting one: it makes the poll block, which on the M4
stalls everything and on the S3 stalls nothing. That is the difference phase
4.2 bought, and this is the rig that demonstrates it -- measured at 2063ms
median HTTP latency on the M4 against 314ms on the S3, with `--delay 2`.

Keep `--delay` BELOW the client's responseTimeoutMs (3s). At or above it the
poll does not run slowly, it *times out*: framing_errors climb, the client
marks itself disconnected and drops into its long backoff, and the board ends
up mostly not polling at all -- which looks like "the delay had no effect".

FIXTURE PROVENANCE: these payloads are synthesized from the community-
documented shape of the F1 SignalR topics, not captured from a live session.
Use --record against a real MultiViewer during a session to replace them; see
the same note in test/test_multiviewer_parse/.
"""

import argparse
import http.server
import json
import socket
import sys
import time
import urllib.request

DRIVERS = {
    "1": {"RacingNumber": "1", "Tla": "VER", "FullName": "Max Verstappen"},
    "16": {"RacingNumber": "16", "Tla": "LEC", "FullName": "Charles Leclerc"},
    "44": {"RacingNumber": "44", "Tla": "HAM", "FullName": "Lewis Hamilton"},
    "81": {"RacingNumber": "81", "Tla": "PIA", "FullName": "Oscar Piastri"},
}


def state(track_status=None, current_lap=None, total_laps=None, messages=None):
    """Builds one f1LiveTimingState object."""
    out = {}
    if track_status is not None:
        out["TrackStatus"] = {"Status": "1", "Message": track_status}
    if current_lap is not None or total_laps is not None:
        lap = {}
        if current_lap is not None:
            lap["CurrentLap"] = current_lap
        if total_laps is not None:
            lap["TotalLaps"] = total_laps
        out["LapCount"] = lap
    out["DriverList"] = DRIVERS
    out["RaceControlMessages"] = {"Messages": messages or {}}
    return out


TOPICS = ("TrackStatus", "LapCount", "DriverList", "RaceControlMessages")


def topics_in(query):
    """The fields a GraphQL query body asked for, in the order above."""
    return [topic for topic in TOPICS if topic.encode() in query]


def select_topics(scenario, query):
    """Returns only the fields the query asked for.

    A real server answers the question it was asked, and the firmware relies on
    that: from phase 3.2 the S3 requests the cheap fields often and the
    expensive ones rarely, which is only safe because the parser leaves state
    it was not told about alone. Replying with everything regardless would hide
    a regression in exactly that behaviour.
    """
    if scenario is None:
        return None
    asked = topics_in(query)
    if not asked:
        return scenario
    return {topic: value for topic, value in scenario.items() if topic in asked}


def flag_message(index, flag, racing_number, scope="Driver"):
    return {
        str(index): {
            "Utc": "2026-09-01T14:00:00",
            "Category": "Flag",
            "Flag": flag,
            "Scope": scope,
            "RacingNumber": racing_number,
            "Message": "{} FLAG FOR CAR {}".format(flag, racing_number),
        }
    }


# Ordered so a cycling run walks the F1 app through every screen it can draw.
SCENARIOS = {
    "nosession": None,
    "laps": state("AllClear", 12, 57),
    "yellow": state("Yellow", 13, 57),
    "doubleyellow": state("DoubleYellow", 14, 57),
    "sc": state("SCDeployed", 15, 57),
    "vsc": state("VSCDeployed", 16, 57),
    "red": state("Red", 17, 57),
    "blueflag": state("AllClear", 18, 57, flag_message(1, "BLUE", 44)),
    "blueclear": state("AllClear", 19, 57, flag_message(2, "CLEAR", 44)),
}

CYCLE = ["laps", "yellow", "sc", "blueflag", "blueclear", "red", "vsc", "nosession"]


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    options = None  # set on the class before serving
    started = time.monotonic()
    requests = 0
    connections = 0

    def setup(self):
        # One instance per accepted connection, so this counts connections
        # rather than requests -- which is exactly what tells keep-alive apart
        # from reconnect-every-poll in the log below.
        super().setup()
        Handler.connections += 1
        self.connection_id = Handler.connections
        self.on_connection = 0

    def current_scenario(self):
        if Handler.options.scenario:
            return Handler.options.scenario
        elapsed = time.monotonic() - Handler.started
        return CYCLE[int(elapsed // Handler.options.hold) % len(CYCLE)]

    def do_POST(self):  # noqa: N802  (http.server's naming)
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length) if length else b""

        Handler.requests += 1
        name = self.current_scenario()

        if Handler.options.delay:
            time.sleep(Handler.options.delay)

        if Handler.options.truncate:
            # Deliberately cut the response short, to exercise the parser's
            # truncated path and the counter that reports it.
            payload = json.dumps({"data": {"f1LiveTimingState": SCENARIOS["laps"]}})
            raw = payload[: len(payload) // 2].encode()
        else:
            live = select_topics(SCENARIOS[name], body)
            raw = json.dumps({"data": {"f1LiveTimingState": live}}).encode()

        self.on_connection += 1

        print("  #{:<4} conn {}/#{:<3} {:<12} {:>6} bytes  [{}]".format(
            Handler.requests, self.connection_id, self.on_connection,
            name, len(raw), " ".join(topics_in(body)) or "?"))
        sys.stdout.flush()

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        # Honour what the client asked for. BaseHTTPRequestHandler already
        # decided close_connection from the request's Connection header, so
        # saying "close" unconditionally -- as this used to -- would make
        # keep-alive impossible to test against.
        if self.close_connection:
            self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(raw)

    def log_message(self, *args):
        pass  # the line printed above is enough


def record(host, out_path):
    """Captures a real MultiViewer response, to replace the synthesized ones."""
    query = json.dumps({
        "query": "{f1LiveTimingState{TrackStatus LapCount DriverList RaceControlMessages}}"
    }).encode()
    request = urllib.request.Request(
        "http://{}:10101/api/graphql".format(host), data=query,
        headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(request, timeout=10) as response:
        raw = response.read()

    with open(out_path, "wb") as handle:
        handle.write(raw)
    print("captured {} bytes to {}".format(len(raw), out_path))

    parsed = json.loads(raw)
    live = parsed.get("data", {}).get("f1LiveTimingState")
    if live is None:
        print("NOTE: f1LiveTimingState was null -- no session was open, so this "
              "fixture only covers the no-session path.")
    else:
        print("topics present:", ", ".join(sorted(live.keys())))
    return 0


def local_addresses():
    try:
        return socket.gethostbyname_ex(socket.gethostname())[2]
    except OSError:
        return []


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=10101)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--scenario", choices=sorted(SCENARIOS),
                        help="hold one scenario instead of cycling")
    parser.add_argument("--hold", type=float, default=15.0,
                        help="seconds per scenario when cycling (default 15)")
    parser.add_argument("--delay", type=float, default=0.0,
                        help="seconds to stall before responding")
    parser.add_argument("--truncate", action="store_true",
                        help="return a half response, to exercise the parser's truncated path")
    parser.add_argument("--record", metavar="HOST",
                        help="capture a real MultiViewer response from HOST and exit")
    parser.add_argument("--out", default="mv_capture.json", help="--record output path")
    args = parser.parse_args()

    if args.record:
        return record(args.record, args.out)

    Handler.options = args
    server = http.server.ThreadingHTTPServer((args.bind, args.port), Handler)

    print("mv_mock on {}:{}".format(args.bind, args.port))
    for address in local_addresses():
        print("  reachable at {}:{}".format(address, args.port))
    if args.scenario:
        print("  holding scenario: {}".format(args.scenario))
    else:
        print("  cycling every {}s: {}".format(args.hold, " -> ".join(CYCLE)))
    if args.delay:
        print("  stalling {}s per response".format(args.delay))
    if args.truncate:
        print("  truncating every response")
    print("Ctrl-C to stop.\n")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n{} requests served".format(Handler.requests))
    return 0


if __name__ == "__main__":
    sys.exit(main())
