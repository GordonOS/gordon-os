#!/usr/bin/env python3
"""vice-drive.py -- the sample VICE driver. Copy this shape; do not invent another.

GordonOS REPLACES the KERNAL and scans the CIA keyboard matrix itself, so the
only way a script can type into it is to poke the kernel's own ring buffer
(keybCount $E216 / keybHead $E217 / keybTail $E218 / keybBuffer $E219). That is
what tools/shots.py's Monitor does, and this file shows the whole loop around it.

Three dead ends, all of them already tried and recorded here so nobody repeats
them (they cost a session once):

  * `-keybuf` on the command line, and the monitor's KEYBOARD FEED, write the
    C64 KERNAL's buffer at $0277 -- ignored, because there is no KERNAL.
  * Driving the VICE WINDOW (Win32 SendKeys / keybd_event / click) loses the
    first key after every focus change ("run musedit" arrives as "un musedit")
    and drops keys mid-word ("tetrs"), because an instant press/release falls
    between two 60 Hz hardware scans. Timing and re-focusing do not fix it.
  * MODIFIER STATE cannot be poked -- kbPoll reads shift off the matrix, so an
    injected "s" is an unshifted s, and keybShift ($E229) is a copy re-derived
    from it. But the SHIFTED SCREEN CODE usually can be: an app that compares the
    incoming code is reached directly. musedit's save is `cmp #$53`, so
    `key(0x53)` saves. Check what the code tests before calling a path untestable.

Two rules about the emulator itself:

  * START IT VISIBLY, as its own step, so the user can watch it boot. Never
    Popen it with DEVNULL: an invisible launch also hides the failure.
  * MON_CMD_QUIT (Monitor.quit()) CLOSES THE WINDOW. Finish with resume() and
    leave VICE running; the user closes it.

Usage
    python tools/vice-drive.py launch                 # start it, visible, and stop
    python tools/vice-drive.py type "run musedit tetris"
    python tools/vice-drive.py key a
    python tools/vice-drive.py fb temp/fb-a.bin       # dump the framebuffer
    python tools/vice-drive.py cmp temp/fb-a.bin temp/fb-b.bin

`launch` and everything else are separate commands ON PURPOSE: launching is a
step the user can see and can refuse.
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import shots  # noqa: E402  (the monitor driver: Monitor, launch, screen_code)

PORT = 6510          # the binary monitor; -binarymonitoraddress below
FB = 0x2000          # the framebuffer (320x200 hires = 8000 bytes)
FB_LEN = 8000
BOOT_SETTLE = 9.0    # enough for the banner and the shell prompt


# --- launch: VISIBLE, its own step -------------------------------------------
def cmd_launch(args):
    cmd = [os.environ.get("VICE_EXE", "x64sc"),
           "-speed", "200",          # the project speed: the SID player compensates tempo and pitch for it
           "-reu", "-reusize", "16384",
           "-reuimage", str(ROOT / "reu" / "REU.bin"),
           "-reuimagerw",                       # tasks are loaded into the image
           "-VICIIborders", "3",
           *shots.rom_args(),
           "-binarymonitor",
           "-binarymonitoraddress", "ip4://127.0.0.1:%d" % args.port,
           "-autostartprgmode", "1",            # inject the PRG; the KERNAL loader
                                                #   cannot place kernel data
           str(ROOT / "build" / "gordon-os.prg")]
    print("launching: %s" % " ".join(cmd[1:]))
    #  No DEVNULL: a visible window, and any complaint reaches the console.
    proc = subprocess.Popen(cmd)
    print("VICE pid %d -- monitor on %d" % (proc.pid, args.port))
    print("now: python tools/vice-drive.py type \"run musedit tetris\"")
    return 0


# --- attach ----------------------------------------------------------------
def attach(port, tries=40):
    for _ in range(tries):
        try:
            return shots.Monitor(port, timeout=2.0)
        except OSError:
            time.sleep(0.5)
    raise SystemExit("vice-drive: no monitor on %d -- was VICE launched with "
                     "-binarymonitor? see `vice-drive.py launch`" % port)


def cmd_type(args):
    m = attach(args.port)
    try:
        m.resume()                       # a monitor command halts the machine;
        time.sleep(args.settle)          #   resume is what lets it run again
        for line in args.lines:
            m.type(line)
            m.enter()
            time.sleep(args.wait)
    finally:
        m.resume()                       # NOT quit(): that closes the window
    print("typed: %s" % " | ".join(args.lines))
    return 0


def cmd_key(args):
    m = attach(args.port)
    try:
        m.resume()
        time.sleep(args.settle)
        m.key(shots.screen_code(args.chars))
    finally:
        m.resume()
    print("key: %s" % args.chars)
    return 0


def cmd_fb(args):
    m = attach(args.port)
    try:
        m.resume()
        time.sleep(args.settle)
        _, data = m.mem_get(FB, FB_LEN)   # (length, bytes) -- the tuple bites
    finally:
        m.resume()
    out = Path(args.path)
    out.write_bytes(data)
    print("framebuffer -> %s (%d bytes)" % (out, len(data)))
    return 0


def cmd_cmp(args):
    a = Path(args.a).read_bytes()
    b = Path(args.b).read_bytes()
    if len(a) != len(b):
        print("lengths differ: %d vs %d" % (len(a), len(b)))
        return 1
    n = sum(1 for x, y in zip(a, b) if x != y)
    print("%d of %d bytes differ (%.2f%%)" % (n, len(a), 100.0 * n / len(a)))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", type=int, default=PORT, help="binary monitor port")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("launch", help="start VICE visibly and leave it running")
    p.set_defaults(func=cmd_launch)

    p = sub.add_parser("type", help="type one or more command lines")
    p.add_argument("lines", nargs="+")
    p.add_argument("--settle", type=float, default=BOOT_SETTLE)
    p.add_argument("--wait", type=float, default=2.0, help="after each Enter")
    p.set_defaults(func=cmd_type)

    p = sub.add_parser("key", help="press one key (unshifted)")
    p.add_argument("chars")
    p.add_argument("--settle", type=float, default=1.5)
    p.set_defaults(func=cmd_key)

    p = sub.add_parser("fb", help="dump the framebuffer to a file")
    p.add_argument("path")
    p.add_argument("--settle", type=float, default=1.5)
    p.set_defaults(func=cmd_fb)

    p = sub.add_parser("cmp", help="compare two framebuffer dumps")
    p.add_argument("a")
    p.add_argument("b")
    p.set_defaults(func=cmd_cmp)

    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
