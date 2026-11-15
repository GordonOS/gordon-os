#!/usr/bin/env python3
"""window-shot.py -- capture ONE window, and nothing but the window, to a PNG.

Documentation screenshots of the host tools (`reufs-gui.py`) are taken with
this, because every simple way of doing it puts something in the picture that
is not the window -- and does it silently:

  * GetWindowRect includes the invisible resize border Windows 11 keeps around
    a window: 7-8 px of whatever is behind it, down every side.
  * Windows 11 rounds a window's corners, so even inside the visible rectangle
    the four arcs show the desktop.
  * PrintWindow renders the window alone (the desktop cannot reach the bitmap),
    but leaves the last border rows and the corner arcs BLACK.

So the recipe here is: square the corners, render with PrintWindow, crop to
DwmGetWindowAttribute's extended frame bounds, then paint the black pixels that
touch the image border with the frame colour. Every black pixel of that fill is
connected to the outside of the window, which is what keeps a dark pane INSIDE
the window (the file manager's log) out of it.

Windows only (the capture is Win32); no third-party modules -- ctypes for the
capture, zlib for the PNG.

Usage:
    python tools/window-shot.py --list
    python tools/window-shot.py --title "GordonOS REU filesystem" --out shot.png
    python tools/window-shot.py --out shot.png     # asks for the title

Or from Python, with the window already open (the file has a dash, so it is
loaded by path, the way `reufs-gui.py` imports its library):

    import importlib.util
    spec = importlib.util.spec_from_file_location("window_shot", "tools/window-shot.py")
    window_shot = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(window_shot)
    window_shot.capture("GordonOS REU filesystem", "shot.png")
    window_shot.capture_handle(hwnd, "shot.png")     # you already have the HWND
"""

import argparse
import ctypes
import struct
import sys
import zlib
from collections import deque
from ctypes import wintypes as wt

DWMWA_EXTENDED_FRAME_BOUNDS = 9
DWMWA_WINDOW_CORNER_PREFERENCE = 33
DWMWCP_DONOTROUND = 1
PW_RENDERFULLCONTENT = 2
FRAME_RGB = (0xF0, 0xF0, 0xF0)      # the 1 px border Windows draws around a window


class RECT(ctypes.Structure):
    _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long),
                ("right", ctypes.c_long), ("bottom", ctypes.c_long)]


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", ctypes.c_long),
                ("biHeight", ctypes.c_long), ("biPlanes", wt.WORD),
                ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", ctypes.c_long),
                ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wt.DWORD),
                ("biClrImportant", wt.DWORD)]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wt.DWORD * 3)]


if sys.platform == "win32":
    _user32 = ctypes.windll.user32
    _gdi32 = ctypes.windll.gdi32
    _dwmapi = ctypes.windll.dwmapi
else:
    _user32 = _gdi32 = _dwmapi = None


# --- finding the window -----------------------------------------------------
def window_handle(tk_window):
    """The frame window (title bar included) that owns a Tk toplevel."""
    child = tk_window.winfo_id()
    return _user32.GetParent(child) or child


def _title_of(hwnd):
    n = _user32.GetWindowTextLengthW(hwnd)
    if n <= 0:
        return ""
    buf = ctypes.create_unicode_buffer(n + 1)
    _user32.GetWindowTextW(hwnd, buf, n + 1)
    return buf.value


def titles():
    """[(hwnd, title)] for every visible top-level window, in z-order."""
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def visit(hwnd, _lparam):
        if _user32.IsWindowVisible(hwnd):
            title = _title_of(hwnd)
            if title:
                found.append((hwnd, title))
        return True

    _user32.EnumWindows(visit, 0)
    return found


def find(title_part):
    """The first visible window whose title contains `title_part` (case-blind)."""
    want = title_part.lower()
    for hwnd, title in titles():
        if want in title.lower():
            return hwnd
    raise SystemExit("no visible window title contains %r -- try --list" % title_part)


def square_corners(hwnd):
    """Ask Windows 11 not to round this window, so nothing rounded is left."""
    pref = ctypes.c_int(DWMWCP_DONOTROUND)
    return _dwmapi.DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                                         ctypes.byref(pref), ctypes.sizeof(pref)) == 0


# --- the capture ------------------------------------------------------------
def _render(hwnd):
    """PrintWindow into a buffer: [(r, g, b)] rows of the window's own pixels,
    already cropped to the visible frame bounds and with the frame filled."""
    outer = RECT()
    _user32.GetWindowRect(hwnd, ctypes.byref(outer))
    vis = RECT()
    _dwmapi.DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                                  ctypes.byref(vis), ctypes.sizeof(RECT))
    width, height = outer.right - outer.left, outer.bottom - outer.top
    hdc_screen = _user32.GetDC(0)
    mem = _gdi32.CreateCompatibleDC(hdc_screen)
    bmp = _gdi32.CreateCompatibleBitmap(hdc_screen, width, height)
    _gdi32.SelectObject(mem, bmp)
    _user32.PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT)
    info = BITMAPINFO()
    info.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    info.bmiHeader.biWidth = width
    info.bmiHeader.biHeight = -height          # top-down
    info.bmiHeader.biPlanes = 1
    info.bmiHeader.biBitCount = 32
    size = width * height * 4
    buf = ctypes.create_string_buffer(size)
    _gdi32.GetDIBits(mem, bmp, 0, height, buf, ctypes.byref(info), 0)
    _gdi32.DeleteObject(bmp)
    _gdi32.DeleteDC(mem)
    _user32.ReleaseDC(0, hdc_screen)

    left, top = vis.left - outer.left, vis.top - outer.top
    cw, ch = vis.right - vis.left, vis.bottom - vis.top
    raw = buf.raw
    pixels = [[tuple(raw[((y * width) + x) * 4:((y * width) + x) * 4 + 3])
               for x in range(left, left + cw)] for y in range(top, top + ch)]

    # PrintWindow leaves the rows and arcs it cannot draw black. Only a black
    # region reaching the image border is unrendered frame; one enclosed by the
    # window is a real dark pane, so the fill is a flood from the border.
    def black(px):
        return px[0] == 0 and px[1] == 0 and px[2] == 0

    seen = [[False] * cw for _ in range(ch)]
    queue = deque()
    for x in range(cw):
        for y in (0, ch - 1):
            if black(pixels[y][x]) and not seen[y][x]:
                seen[y][x] = True
                queue.append((x, y))
    for y in range(ch):
        for x in (0, cw - 1):
            if black(pixels[y][x]) and not seen[y][x]:
                seen[y][x] = True
                queue.append((x, y))
    while queue:
        x, y = queue.popleft()
        pixels[y][x] = FRAME_RGB
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if 0 <= nx < cw and 0 <= ny < ch and not seen[ny][nx] and black(pixels[ny][nx]):
                seen[ny][nx] = True
                queue.append((nx, ny))
    return cw, ch, pixels


def _write_png(path, width, height, pixels):
    raw = b"".join(b"\x00" + b"".join(bytes(px) for px in row) for row in pixels)

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    with open(path, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n")
        fh.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        fh.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        fh.write(chunk(b"IEND", b""))


def capture_handle(hwnd, out):
    """Capture `hwnd` (title bar included) to `out`. Returns (width, height)."""
    if _user32 is None:
        raise SystemExit("window-shot.py captures Windows windows only")
    square_corners(hwnd)
    width, height, pixels = _render(hwnd)
    _write_png(out, width, height, pixels)
    return width, height


def capture(title_part, out):
    """Capture the first visible window whose title contains `title_part`."""
    return capture_handle(find(title_part), out)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Capture one window to a PNG.")
    ap.add_argument("--title", default=None,
                    help="part of the window title (default: ask)")
    ap.add_argument("--out", required=False, default=None, help="the PNG to write")
    ap.add_argument("--list", action="store_true", help="list visible window titles")
    args = ap.parse_args(argv)

    if args.list:
        for hwnd, title in titles():
            print("%10d  %s" % (hwnd, title))
        return 0
    if not args.out:
        ap.error("--out is required")
    title = args.title
    if not title:
        title = input("part of the window title: ").strip()
        if not title:
            ap.error("no title given")
    width, height = capture(title, args.out)
    print("wrote %s (%d x %d) from the window titled %r"
          % (args.out, width, height, title))
    return 0


if __name__ == "__main__":
    sys.exit(main())
