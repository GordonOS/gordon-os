#!/usr/bin/env python3
"""shots.py -- capture the README's screenshots from a real VICE run.

Drives x64sc through its binary monitor (VICE manual ch. 13). Every command
halts the machine, so a poke into the kernel's key ring is atomic and lands
exactly as the keyboard IRQ would push it -- which is the only way to type into
GordonOS from a script:

  * VICE's own -keybuf, and the monitor's KEYBOARD FEED, feed the C64 KERNAL's
    keyboard buffer at $0277. GordonOS replaces the KERNAL and scans the matrix
    itself, so both are silently ignored.
  * tools/vice-step.ps1 and vice-test.ps1 drive the VICE window through Win32
    SendKeys, so they need Windows.

Input is therefore injected at the OS level:
  * keys -> the kernel's own ring buffer (keybCount $E216 / keybHead $E217 /
            keybTail $E218 / keybBuffer $E219, 16 entries, IRQ-owned)
  * pad  -> JOYPORT SET, which is hardware-level: the two games read port 2.
            Its port index is 0-based, so control port 2 is port 1.

A poke is one MEM SET of the whole key-block header, read back first so only
the three bytes that change are written.

Screenshots come from VICE's own -exitscreenshot, written when the driver QUITs
the emulator, at -VICIIborders 3 so the image is exactly the 320x200 display.

Usage:
    tools/shots.py                 capture every shot into docs/img/
    tools/shots.py boot edit       capture named shots only
    tools/shots.py --list          list the shot names and what they show
"""

import argparse
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
from functools import partial
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    sys.exit("shots.py: Pillow is required (pip install pillow)")

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "docs" / "img"
REU = ROOT / "reu" / "REU.bin"

# --- kernel data the pokes use (src/tasks/kernel-data.inc) -------------------
KEY_COUNT = 0xE216          # 16-entry ring, IRQ-owned
KEY_HEAD = 0xE217
KEY_TAIL = 0xE218
KEY_BUFFER = 0xE219
KEY_RING = 16
KEYB_HELD = 0xE214          # the key held RIGHT NOW, $FF = none -- a LEVEL
KEYB_LAST = 0xE215          # the scan's own last-seen code, compared for change

# --- screen codes, not PETSCII: a typed key IS a screen code -----------------
SPECIAL = {
    "RETURN": 0x81, "DEL": 0x80, "HOME": 0x82, "STOP": 0x83,
    "CRSR_DN": 0x84, "CRSR_RT": 0x86, "INST": 0x9F,
    "UP": 0x91, "DOWN": 0x11, "LEFT": 0x9D, "RIGHT": 0x1D,
    "F1": 0x85, "F3": 0x86, "F5": 0x87, "F7": 0x88, "SPACE": 0x20,
}

# --- VICE binary monitor -----------------------------------------------------
MON_CMD_MEM_SET = 0x02
MON_CMD_MEM_GET = 0x01
MON_CMD_JOYPORT_SET = 0xA2
MON_CMD_EXIT = 0xAA          # resume
MON_CMD_QUIT = 0xBB          # quit VICE (this is what writes -exitscreenshot)
MON_CMD_PING = 0x81

JOY_UP, JOY_DOWN, JOY_LEFT, JOY_RIGHT, JOY_FIRE = 1, 2, 4, 8, 16

# No port-2 device is attached: the games are driven from the keyboard, which is
# what their documentation describes. GordonPaint is the exception -- it is
# mouse-only, so its shot attaches a 1351.
DEFAULT_DEVICE = []
SHOT_DEVICE = {"grpaint": ["-controlport2device", "3"],   # the 1351
               "grpaint-new": ["-controlport2device", "3"],
               "mtest": ["-controlport2device", "3"]}   # so the readout has a mouse


class Monitor:
    """The VICE binary monitor, enough of it to type, push a stick and quit."""

    def __init__(self, port, timeout=10.0):
        deadline = time.time() + timeout
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=2.0)
                break
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.25)
        self.sock.settimeout(5.0)
        self.req = 0

    def _recv(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise IOError("shots: monitor closed")
            buf += chunk
        return buf

    def cmd(self, code, body=b"", want=None, timeout=10.0):
        """Send one command, return (type, error, body) for its own response.

        Request  = 02 02 <body len:4> <request id:4> <command> <body>
        Response = 02 02 <body len:4> <type> <error> <request id:4> <body>
        A command halts the machine, so its exchange also produces events
        (STOPPED, register dumps) with request id 0xffffffff -- those are read
        and skipped until the answer to this command arrives.
        """
        self.req = (self.req + 1) & 0xFFFFFFFF
        pkt = bytes([0x02, 0x02]) + struct.pack("<I", len(body))
        pkt += struct.pack("<I", self.req) + bytes([code]) + body
        self.sock.sendall(pkt)
        deadline = time.time() + timeout
        while time.time() < deadline:
            hdr = self._recv(12)
            if hdr[0] != 0x02:
                raise IOError("shots: lost sync (%02x)" % hdr[0])
            length = struct.unpack("<I", hdr[2:6])[0]
            rtype, err = hdr[6], hdr[7]
            rid = struct.unpack("<I", hdr[8:12])[0]
            rbody = self._recv(length) if length else b""
            if rid != self.req:
                continue          # an event (STOPPED, register dump) -- skip it
            if want is not None and rtype != want:
                raise IOError("shots: command %02x answered %02x" % (code, rtype))
            if err != 0:
                raise IOError("shots: command %02x failed (error %02x)" % (code, err))
            return rtype, err, rbody
        raise IOError("shots: no response for command %02x" % code)

    def mem_get(self, addr, length, memspace=0x00, bank=0x0000):
        body = struct.pack("<BHHBH", 0x00, addr, addr + length - 1, memspace, bank)
        _, _, rbody = self.cmd(MON_CMD_MEM_GET, body)
        return struct.unpack("<H", rbody[0:2])[0], rbody[2:2 + length]

    def mem_set(self, addr, data, memspace=0x00, bank=0x0000):
        end = addr + len(data) - 1
        body = struct.pack("<BHHBH", 0x00, addr, end, memspace, bank) + bytes(data)
        self.cmd(MON_CMD_MEM_SET, body)

    # --- input -------------------------------------------------------------
    def key(self, code):
        """Push one key event into the kernel's ring, as the IRQ does.

        The emulator is halted for the poke and resumed straight after: a
        command leaves the CPU stopped, so a poke without the resume would
        freeze the machine on the first keystroke.
        """
        _, hdr = self.mem_get(KEY_COUNT, 3 + KEY_RING)
        count, head, tail = hdr[0], hdr[1], hdr[2]
        buf = bytearray(hdr[3:3 + KEY_RING])
        if count < KEY_RING:
            buf[head & 0x0F] = code
            head = (head + 1) & 0x0F
            self.mem_set(KEY_COUNT, bytes([count + 1, head, tail]) + bytes(buf))
        self.resume()
        return count < KEY_RING

    def type(self, text, gap=0.06):
        """Type text, with {NAME} for a special key, e.g. "run edit {RETURN}"."""
        first = True
        for token in text.split(" "):
            if not token:
                continue
            if not first:
                self.key(SPECIAL["SPACE"])
                time.sleep(gap)
            first = False
            if token.startswith("{"):          # {RETURN} {STOP} ...
                self.key(SPECIAL[token.strip("{}")])
                time.sleep(gap)
                continue
            for ch in token:
                self.key(screen_code(ch))
                time.sleep(gap)

    def enter(self, gap=0.12):
        self.key(SPECIAL["RETURN"])
        time.sleep(gap)

    def hold(self, code):
        """Hold a key down, as the scan does when the matrix reports it.

        keybHeld is written only on a CHANGE, so a poke survives: the scan keeps
        reading "nothing pressed", compares it with its own keybLast ($ff) and
        takes no action. The games read this level for a paddle or a smooth
        cursor, which no key EVENT can drive.
        """
        self.mem_set(KEYB_HELD, bytes([code]))
        self.resume()

    def release(self):
        self.mem_set(KEYB_HELD, bytes([0xFF]))
        self.resume()

    def press_hold(self, code, seconds):
        self.hold(code)
        time.sleep(seconds)
        self.release()
        time.sleep(0.05)

    def joy(self, port, value, hold=0.35):
        self.cmd(MON_CMD_JOYPORT_SET, struct.pack("<HH", port, value))
        self.resume()
        time.sleep(hold)
        self.cmd(MON_CMD_JOYPORT_SET, struct.pack("<HH", port, 0))
        self.resume()
        time.sleep(0.05)

    def resume(self):
        self.cmd(MON_CMD_EXIT)

    def quit(self):
        try:
            self.cmd(MON_CMD_QUIT)
        except IOError:
            pass                                # VICE may close before answering


def screen_code(ch):
    """ASCII -> the C64 screen code a keypress produces."""
    if "a" <= ch <= "z":
        return ord(ch) - ord("a") + 1
    if "A" <= ch <= "Z":
        return ord(ch) - ord("A") + 1
    if "0" <= ch <= "9":
        return 0x30 + (ord(ch) - ord("0"))
    table = {" ": 0x20, ".": 0x2E, ",": 0x2C, ":": 0x3A, ";": 0x3B, "-": 0x2D,
             "+": 0x2B, "=": 0x3D, "/": 0x2F, "*": 0x2A, "(": 0x28, ")": 0x29,
             "'": 0x27, '"': 0x22, "$": 0x24, "@": 0x40, "<": 0x3C, ">": 0x3E,
             "!": 0x21, "?": 0x3F, "#": 0x23, "%": 0x25, "&": 0x26}
    if ch in table:
        return table[ch]
    raise KeyError("shots: no screen code for %r" % ch)


def rom_args():
    """The -directory search path the project's own launch flow uses."""
    settings = ROOT / ".vscode" / "settings.json"
    if settings.exists():
        try:
            data = json.loads(settings.read_text())
            for key, value in data.items():
                if key == "vs64.viceArgs" and "-directory" in value:
                    parts = value.split()
                    return ["-directory", parts[parts.index("-directory") + 1]]
        except (ValueError, IndexError):
            pass
    #  No per-clone setting: the two locations a Linux VICE install uses, with the home
    #  directory expanded rather than assumed.
    return ["-directory", str(Path.home() / ".local/share/vice") + ":/usr/share/vice"]


def launch(shot_png, port, extra=()):
    cmd = ["x64sc",
           "-speed", "200", "-reu", "-reusize", "16384",
           "-reuimage", str(REU), "-VICIIborders", "3",
           *rom_args(),
           "-binarymonitor", "-binarymonitoraddress", "ip4://127.0.0.1:%d" % port,
           "-exitscreenshot", str(shot_png),
           *extra,
           str(ROOT / "build" / "gordon-os.prg")]
    return subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


# --- the shots ---------------------------------------------------------------
# Each entry: (what it shows, [steps]). A step is ('w', seconds) to let the
# machine run, ('keys', "text") to type, ('key', "RETURN"), or ('joy', value,
# seconds) to hold port 2. Typing happens while the machine is halted.
BOOT_SETTLE = 9.0          # cycles enough for the banner and the shell prompt


def shot_boot(m):
    m.resume(); time.sleep(BOOT_SETTLE)


def shot_shell(m):
    m.resume(); time.sleep(BOOT_SETTLE)
    m.type("ver"); m.enter(); time.sleep(1.0)
    m.type("me");  m.enter(); time.sleep(0.8)
    m.type("ps");  m.enter(); time.sleep(1.4)


def shot_pool(m):
    m.resume(); time.sleep(BOOT_SETTLE)
    m.type("ps");    m.enter(); time.sleep(1.4)
    m.type("pool");  m.enter(); time.sleep(1.6)


def shot_edit(m):
    m.resume(); time.sleep(BOOT_SETTLE)
    m.type("run edit notes"); m.enter(); time.sleep(3.0)
    for line in ("gordonOS notes",
                 "",
                 "The editor is a task like any other:",
                 "it owns a hires surface, holds",
                 "filesys.lib, and grows its document",
                 "in 4-page kMalloc chunks as you type",
                 "past the 24th line of each one.",
                 "",
                 "STOP+Q quits, STOP+W writes as,",
                 "STOP+M marks a selection.",
                 "F1 drops back to the shell."):
        if line:
            m.type(line)
        m.enter()
        time.sleep(0.18)


def shot_grpaint(m):
    m.resume(); time.sleep(BOOT_SETTLE)
    m.type("run grpaint alf"); m.enter(); time.sleep(6.0)


def shot_grpaint_new(m):
    m.resume(); time.sleep(BOOT_SETTLE)
    m.type("run grpaint shot"); m.enter(); time.sleep(6.0)


def shot_gortris_select(m):
    m.resume(); time.sleep(BOOT_SETTLE)
    m.type("run gortris"); m.enter(); time.sleep(4.0)
    m.key(screen_code("j")); time.sleep(1.0)       # attract -> level select
    m.key(screen_code("k")); time.sleep(0.5)       # step the level
    m.key(screen_code("k")); time.sleep(1.0)


def shot_gortris_play(m):
    m.resume(); time.sleep(BOOT_SETTLE)
    m.type("run gortris"); m.enter(); time.sleep(4.0)
    m.key(screen_code("j")); time.sleep(1.0)       # attract -> level select
    m.key(screen_code("s")); time.sleep(1.2)       # -> play
    # A few pieces, spread across the well: enough to read as a game in
    # progress, not so many that the stack reaches the spawn row.
    m.key(screen_code("j")); time.sleep(0.4)
    m.key(SPECIAL["SPACE"]); time.sleep(1.4)       # one left
    m.key(screen_code("k")); time.sleep(0.4)
    m.key(screen_code("k")); time.sleep(0.4)
    m.key(SPECIAL["SPACE"]); time.sleep(1.4)       # one right
    m.key(screen_code("j")); time.sleep(0.6)
    m.key(SPECIAL["SPACE"]); time.sleep(1.6)       # one centre, one still falling


def shot_grknoid_attract(m):
    m.resume(); time.sleep(BOOT_SETTLE)
    m.type("run grknoid"); m.enter(); time.sleep(4.0)


def shot_grknoid_play(m):
    m.resume(); time.sleep(BOOT_SETTLE)
    m.type("run grknoid"); m.enter(); time.sleep(4.0)
    m.key(screen_code("j")); time.sleep(0.8)       # attract -> play
    m.press_hold(SPECIAL["SPACE"], 0.4)            # serve
    time.sleep(0.6)
    for _ in range(6):                             # chase the ball
        m.press_hold(screen_code("j"), 0.9)
        m.press_hold(screen_code("k"), 0.9)


def shot_grview_hires(m):
    m.resume(); time.sleep(BOOT_SETTLE)
    m.type("run grview hires"); m.enter(); time.sleep(4.0)


def shot_grview_mc(m):
    m.resume(); time.sleep(BOOT_SETTLE)
    m.type("run grview mc"); m.enter(); time.sleep(4.0)


def shot_basic(m):
    m.resume(); time.sleep(BOOT_SETTLE)
    m.type("run basic"); m.enter(); time.sleep(6.0)
    m.type('print "gordonOS basic"'); m.enter(); time.sleep(0.8)
    m.type("print 2*3+4"); m.enter(); time.sleep(1.0)


def shot_sprdemo(m):
    m.resume(); time.sleep(BOOT_SETTLE)
    m.type("run sprdemo"); m.enter(); time.sleep(6.0)


def shot_gfxdemo(m):
    m.resume(); time.sleep(BOOT_SETTLE)
    m.type("run gfxdemo"); m.enter(); time.sleep(12.0)   # draws once, then idles


def shot_spirog(m):
    m.resume(); time.sleep(BOOT_SETTLE)
    m.type("run spirog"); m.enter(); time.sleep(14.0)


def shot_mtest(m):
    m.resume(); time.sleep(BOOT_SETTLE)
    m.type("mtest"); m.enter(); time.sleep(4.0)     # a .com: typed by its bare name


def shot_granim(m, extra=0.0):
    """The film player, held for `extra` seconds past the point where it shows.

    The film's clock steps on the machine's declared speed, so it runs for the
    same real time here as on a real C64 -- which is what lets a GIF be built
    from repeated runs at successive offsets.
    """
    m.resume(); time.sleep(BOOT_SETTLE)
    m.type("run granim llama"); m.enter(); time.sleep(6.0 + extra)


def shot_dir(m, command):
    """A dir listing: the command is a .com, so it is typed by its bare name."""
    m.resume(); time.sleep(BOOT_SETTLE)
    m.type(command); m.enter(); time.sleep(2.6)


SHOTS = {
    "granim-gif":     ("the film playing (a looping GIF, not a PNG)", None),
    "boot":            ("the boot screen: the banner and the shell prompt", shot_boot),
    "shell-session":   ("a typed session: ver, dir, ps", shot_shell),
    "pool":            ("ps and pool: the 109-page map", shot_pool),
    "edit":            ("the editor with a document open", shot_edit),
    "grpaint":         ("GordonPaint showing a loaded picture", shot_grpaint),
    "grpaint-new":     ("GordonPaint on a new, blank canvas", shot_grpaint_new),
    "gortris-select":  ("gortris level select with the hi-score table", shot_gortris_select),
    "gortris-play":    ("gortris mid-game with a stack", shot_gortris_play),
    "grknoid-attract": ("gorkanoid's title screen", shot_grknoid_attract),
    "grknoid-play":    ("gorkanoid: paddle, ball and a damaged wall", shot_grknoid_play),
    "grview-hires":    ("grview showing hires.pic", shot_grview_hires),
    "grview-mc":       ("grview showing mc.pic", shot_grview_mc),
    "basic":           ("Gordon BASIC running a line", shot_basic),
    "sprdemo":         ("eight sprite balls with collision", shot_sprdemo),
    "gfxdemo":         ("gfxdemo's line weave", shot_gfxdemo),
    "spirog":          ("the spirograph on a bitmap", shot_spirog),
    "mtest":           ("the 1351 readout", shot_mtest),
    "dir-p":           ("dir /p: a listing paused at the page prompt", partial(shot_dir, command="dir /p")),
    "dir-w":           ("dir /w: two columns, with a wildcard", partial(shot_dir, command="dir /w *.tsk")),
    "dir-tsk":         ("dir *.tsk: the tasks in the image", partial(shot_dir, command="dir *.tsk")),
    "dir-com":         ("dir *.com: the commands in the image", partial(shot_dir, command="dir *.com")),
}


def capture(name, port):
    #  The raw capture is VICE's own 320x200 PNG; it is scaled into docs/img/ and
    #  discarded. It goes to a temp directory, never to docs/img/, so a run that
    #  dies between the two leaves nothing behind that a publish could ship.
    raw = Path(tempfile.mkdtemp(prefix="gordon-shots-")) / (name + "-raw.png")
    proc = launch(raw, port, SHOT_DEVICE.get(name, DEFAULT_DEVICE))
    try:
        mon = Monitor(port)
        mon.cmd(MON_CMD_PING)
        SHOTS[name][1](mon)
        mon.quit()
    finally:
        try:
            proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            proc.kill()
    if not raw.exists():
        return False
    im = Image.open(raw).convert("RGB")
    im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
    im.save(OUT / (name + ".png"))
    raw.unlink()
    return True


def capture_gif(name, port, frames=12, step=0.055, fps=12):
    """A motion shot: one run per frame, each held a little longer than the last.

    The emulator is deterministic, so N runs that differ only in when they quit
    give N successive moments of the same film, and the GIF loops those.
    """
    OUT.mkdir(parents=True, exist_ok=True)
    workdir = Path(tempfile.mkdtemp(prefix="gordon-gif-"))
    pics = []
    for i in range(frames):
        raw = workdir / ("%s-%02d-raw.png" % (name, i))
        proc = launch(raw, port + i, DEFAULT_DEVICE)
        try:
            mon = Monitor(port + i)
            mon.cmd(MON_CMD_PING)
            shot_granim(mon, extra=step * i)
            mon.quit()
        finally:
            try:
                proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                proc.kill()
        if not raw.exists():
            print("  frame %d failed" % i)
            continue
        im = Image.open(raw).convert("RGB")
        pics.append(im.resize((im.width * 2, im.height * 2), Image.NEAREST))
        raw.unlink()
    if not pics:
        return False
    pics[0].save(OUT / (name + ".gif"), save_all=True, append_images=pics[1:],
                 duration=int(1000 / fps), loop=0, optimize=True)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("names", nargs="*", help="shots to capture (default: all)")
    ap.add_argument("--list", action="store_true", help="list the shots and exit")
    ap.add_argument("--port", type=int, default=6510, help="binary monitor port")
    args = ap.parse_args()

    if args.list:
        for name, (why, _) in SHOTS.items():
            print("%-17s %s" % (name, why))
        return 0

    names = args.names or list(SHOTS)
    bad = [n for n in names if n not in SHOTS]
    if bad:
        print("shots: unknown shot(s): %s" % ", ".join(bad), file=sys.stderr)
        return 2
    for i, name in enumerate(names):
        sys.stdout.write("%-17s" % name)
        sys.stdout.flush()
        if name == "granim-gif":
            ok = capture_gif("granim", args.port + i * 20)
        else:
            ok = capture(name, args.port + i)
        print("ok" if ok else "FAILED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
