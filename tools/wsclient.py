#!/usr/bin/env python3
"""WebSocket client for the board's signed /api/ws endpoint.

The upgrade is an ordinary authenticated GET: it is signed exactly like every
other /api/ request, and the connection inherits that. There is no token and no
second handshake -- the socket is trusted because the request that opened it
was.

    python tools/wsclient.py --host 192.168.1.50 --listen 30
    python tools/wsclient.py --host 192.168.1.50 --set 0 color 16711680

Needs credentials from a previous `m4client.py pair`.

Deliberately dependency-free: no websockets package, just enough of RFC 6455 to
open a connection, mask a text frame and read the replies. That keeps this
runnable next to smoke.py on a machine with nothing installed.
"""

import argparse
import base64
import json
import os
import socket
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m4client import DEFAULT_CREDENTIALS, load_credentials, sign  # noqa: E402

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def accept_key_for(key):
    import hashlib
    return base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()


def open_socket(host, credentials, timeout=10):
    """Signed GET /api/ws, upgraded. Returns the connected socket."""
    key = base64.b64encode(os.urandom(16)).decode()
    target = "/api/ws"
    ts, nonce, signature = sign(credentials["secret"], "GET", target, b"")
    authorization = "HMAC id={},ts={},nonce={},sig={}".format(
        credentials["client_id"], ts, nonce, signature)

    request = (
        "GET {} HTTP/1.1\r\n"
        "Host: {}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: {}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Authorization: {}\r\n"
        "\r\n"
    ).format(target, host, key, authorization).encode()

    sock = socket.create_connection((host, 80), timeout=timeout)
    sock.sendall(request)

    header = b""
    while b"\r\n\r\n" not in header:
        chunk = sock.recv(1)
        if not chunk:
            raise RuntimeError("board closed the connection during the handshake")
        header += chunk

    status = header.split(b"\r\n", 1)[0].decode(errors="replace")
    if b"101" not in header.split(b"\r\n", 1)[0]:
        sock.close()
        raise RuntimeError("upgrade refused: {}\n{}".format(
            status, header.decode(errors="replace")))

    # Proves the server implements the protocol rather than merely echoing.
    expected = accept_key_for(key).encode()
    if expected not in header:
        sock.close()
        raise RuntimeError("Sec-WebSocket-Accept did not match; expected {}".format(
            expected.decode()))

    print("[ws] {}  accept key verified".format(status))
    return sock


def send_text(sock, text):
    """A masked client text frame, as the RFC requires of a client."""
    payload = text.encode()
    mask = os.urandom(4)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))

    if len(payload) < 126:
        header = struct.pack("!BB", 0x81, 0x80 | len(payload))
    elif len(payload) <= 0xFFFF:
        header = struct.pack("!BBH", 0x81, 0x80 | 126, len(payload))
    else:
        header = struct.pack("!BBQ", 0x81, 0x80 | 127, len(payload))
    sock.sendall(header + mask + masked)


def read_frame(sock):
    """One server frame. Returns (opcode, payload) or None on close."""
    def recv_exactly(n):
        buf = b""
        while len(buf) < n:
            chunk = sock.recv(n - len(buf))
            if not chunk:
                return None
            buf += chunk
        return buf

    head = recv_exactly(2)
    if head is None:
        return None
    opcode = head[0] & 0x0F
    length = head[1] & 0x7F
    if length == 126:
        length = struct.unpack("!H", recv_exactly(2))[0]
    elif length == 127:
        length = struct.unpack("!Q", recv_exactly(8))[0]

    # A server frame is never masked; if one is, the board is broken.
    if head[1] & 0x80:
        raise RuntimeError("server sent a masked frame, which the RFC forbids")

    payload = recv_exactly(length) if length else b""
    return opcode, payload


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--credentials", default=os.path.normpath(DEFAULT_CREDENTIALS))
    parser.add_argument("--listen", type=float, metavar="SECONDS",
                        help="hold the connection open and print what arrives")
    parser.add_argument("--set", nargs=3, metavar=("APP", "KEY", "VALUE"),
                        help="send one setting change, e.g. --set 0 color 16711680")
    args = parser.parse_args()

    credentials = load_credentials(args.credentials)
    sock = open_socket(args.host, credentials)

    try:
        if args.set:
            app, key, raw = args.set
            try:
                value = json.loads(raw)
            except json.JSONDecodeError:
                value = raw          # a bare string, e.g. a text setting
            message = json.dumps({"app": int(app), "key": key, "value": value})
            send_text(sock, message)
            print("[ws] sent {}".format(message))

        if args.listen:
            sock.settimeout(1.0)
            deadline = time.time() + args.listen
            print("[ws] listening {:.0f}s".format(args.listen))
            while time.time() < deadline:
                try:
                    frame = read_frame(sock)
                except socket.timeout:
                    continue
                if frame is None:
                    print("[ws] board closed the connection")
                    break
                opcode, payload = frame
                if opcode == 0x8:
                    print("[ws] close: {}".format(payload.hex()))
                    break
                print("[ws] <- {}".format(payload.decode(errors="replace")))
        elif not args.set:
            # Nothing asked for: just prove the connection stands.
            time.sleep(0.5)
            print("[ws] connection open")
    finally:
        sock.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
