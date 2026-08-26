"""PlatformIO pre-build script: bakes the git describe output into
FIRMWARE_VERSION, so a flashed board can report exactly which commit/tag it is
running. Falls back to "unknown" if git is unavailable or this isn't a git
checkout (e.g. a source tarball).
"""
import subprocess

Import("env")  # noqa: F821  -- injected by PlatformIO's SCons environment


def git_describe():
    try:
        out = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=env["PROJECT_DIR"],
            stderr=subprocess.DEVNULL,
        )
        return out.decode().strip()
    except Exception:
        return "unknown"


version = git_describe()
print("Firmware version: {}".format(version))
env.Append(BUILD_FLAGS=['-DFIRMWARE_VERSION=\\"{}\\"'.format(version)])
