#!/usr/bin/env python3
"""Capture a board's serial output, including the boot banner.

    python tools/boot_log.py 60                 # log the S3 for 60 seconds
    python tools/boot_log.py 60 --reboot        # ...starting from the next boot
    python tools/boot_log.py 60 --hwid 239A:80C9   # the M4 instead

`pio device monitor` cannot do the second of those on the S3. That board speaks
USB straight from the chip, so uploading takes its serial port off the bus
entirely: the 1200-bps touch reboots it into ROM download mode under a
different USB ID, and the application port only comes back after the reset at
the end of the upload. A monitor started beforehand loses its port, and one
started afterwards has already missed setup().

With `--reboot` this waits for the port to disappear and return, then opens it.
Opening it is also what satisfies the firmware's "wait up to 5s for a serial
monitor" at the top of setup(), so the banner is still there to be read. Run it
in the background, start the upload, and read the log after.

    python tools/boot_log.py 90 --reboot &
    pio run -e s3 -t upload

The M4 keeps its port across an upload, so plain `pio device monitor` is fine
there and this is only worth reaching for on the S3.
"""
import sys
import time

import serial
from serial.tools import list_ports

# The S3's *application* USB ID. Deliberately not the ROM loader's 303A:1001:
# while the board is in download mode there is nothing to listen to, and
# matching it would only mean opening the port esptool is trying to use. Both
# boards are on Adafruit's vendor ID, so the product half is what tells them
# apart -- see docs/development.md.
DEFAULT_HWID = "239A:8125"

DEFAULT_SECONDS = 45.0
# Long enough to cover a full upload, which is the point of --reboot.
APPEAR_TIMEOUT = 90.0


def find_port(hwid):
    for port in list_ports.comports():
        if hwid in (port.hwid or "").upper():
            return port.device
    return None


def wait_until(hwid, present, timeout):
    """Waits for the port to be there (or not). False if it never happened."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if (find_port(hwid) is not None) == present:
            return True
        time.sleep(0.1)
    return False


def open_when_ready(port):
    # Windows lists the port a moment before it will open.
    for _ in range(50):
        try:
            return serial.Serial(port, 115200, timeout=0.2)
        except serial.SerialException:
            time.sleep(0.2)
    return None


def main():
    args = sys.argv[1:]
    across_reboot = "--reboot" in args
    hwid = DEFAULT_HWID
    if "--hwid" in args:
        hwid = args[args.index("--hwid") + 1]
    hwid = hwid.upper()

    positional = [a for a in args if not a.startswith("-") and a.upper() != hwid]
    seconds = float(positional[0]) if positional else DEFAULT_SECONDS

    port = find_port(hwid)
    print("[log] %s port: %s" % (hwid, port or "not attached"), flush=True)

    if across_reboot and port is not None:
        print("[log] waiting for it to drop...", flush=True)
        if not wait_until(hwid, False, APPEAR_TIMEOUT):
            print("[log] it never dropped; logging what is there", flush=True)

    if port is None or across_reboot:
        print("[log] waiting for the board...", flush=True)
        if not wait_until(hwid, True, APPEAR_TIMEOUT):
            sys.stderr.write("No board on %s appeared.\n" % hwid)
            return 1
        port = find_port(hwid)

    stream = open_when_ready(port)
    if stream is None:
        sys.stderr.write("Could not open %s.\n" % port)
        return 1

    print("[log] logging %s for %gs" % (port, seconds), flush=True)
    deadline = time.time() + seconds
    with stream:
        while time.time() < deadline:
            chunk = stream.read(4096)
            if chunk:
                sys.stdout.write(chunk.decode("utf-8", "replace"))
                sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
