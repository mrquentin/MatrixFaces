"""PlatformIO pre-build script: picks the upload port by the target board's own
USB VID:PID, and fails the upload rather than guessing.

PlatformIO already prefers a port whose hardware ID matches the board being
built. What it does when no such port is present is the problem: it falls back
to "the first USB serial device I can see". Both boards in this project are
Adafruit boards on vendor ID 239A, and with only one of them plugged in that
fallback hands the M4's SAM-BA uploader the S3's port (and the reverse). Both
directions fail at the protocol handshake rather than writing anything, but a
build that stops with a clear message beats one that gets that far.

Only upload targets are affected: a plain `pio run`, and therefore CI, never
looks at a serial port at all.
"""
import sys

Import("env")  # noqa: F821  -- injected by PlatformIO's SCons environment

from platformio.device.list.util import list_serial_ports  # noqa: E402


UPLOAD_TARGETS = {"upload", "uploadfs", "uploadfsota"}


def board_hwids():
    """The board's USB IDs, as 'VVVV:PPPP' upper-case strings.

    Mostly the board definition's own list, plus whatever `custom_upload_hwids`
    adds for IDs it does not know about -- a recovery bootloader, say, which
    enumerates as the chip vendor rather than the board vendor.
    """
    declared = {
        ("%s:%s" % (vid, pid)).replace("0x", "").upper()
        for vid, pid in env.BoardConfig().get("build.hwids", [])  # noqa: F821
    }
    extra = env.GetProjectOption("custom_upload_hwids", "")  # noqa: F821
    return declared | {
        item.strip().replace("0x", "").upper() for item in extra.split(",") if item.strip()
    }


def matching_ports(hwids):
    return [
        port
        for port in list_serial_ports()
        if any(hwid in port["hwid"].upper() for hwid in hwids)
    ]


if UPLOAD_TARGETS & set(COMMAND_LINE_TARGETS):  # noqa: F821
    # An explicit --upload-port (or upload_port in an env) is the developer
    # saying they know better; leave it alone.
    if env.subst("$UPLOAD_PORT"):  # noqa: F821
        pass
    else:
        hwids = board_hwids()
        ports = matching_ports(hwids)
        if not ports:
            sys.stderr.write(
                "\nNo %s found on USB (looked for %s).\n"
                "Attached ports:\n%s\n"
                "Plug the board in, or pass --upload-port to override.\n\n"
                % (
                    env.BoardConfig().get("name"),  # noqa: F821
                    ", ".join(sorted(hwids)) or "no declared hardware IDs",
                    "\n".join(
                        "  %s  %s" % (p["port"], p["hwid"]) for p in list_serial_ports()
                    )
                    or "  (none)",
                )
            )
            env.Exit(1)  # noqa: F821
        # More than one of the same board is a situation only the developer can
        # resolve, so name them and stop rather than picking one.
        elif len(ports) > 1:
            sys.stderr.write(
                "\nMore than one %s is attached:\n%s\n"
                "Pass --upload-port to say which.\n\n"
                % (
                    env.BoardConfig().get("name"),  # noqa: F821
                    "\n".join("  %s  %s" % (p["port"], p["hwid"]) for p in ports),
                )
            )
            env.Exit(1)  # noqa: F821
        else:
            print("Upload port: %s (%s)" % (ports[0]["port"], ports[0]["hwid"]))
            env.Replace(UPLOAD_PORT=ports[0]["port"])  # noqa: F821
