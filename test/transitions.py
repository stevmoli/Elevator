#!/usr/bin/env python3
"""
Emulator test rig for Elevator's time transitions.

For each case it sets the emulator clock, launches the watchface, lets real
time run through a minute change, and screenshots the result once everything
should have settled. That screenshot is compared pixel-for-pixel against a
reference: the same time shown by a freshly launched face after its launch
float has finished. If a digit failed to come back, or came back in the wrong
place, the two differ.

It also records the transition as a GIF (frames grabbed straight from the
emulator's display) and checks that the digits actually animated.

Results, including any failing screenshots and diffs, go under --out.

Usage (from the repo root, with the project already built and an emulator
available):
    python3 test/transitions.py --platform basalt --format 24h --cases hours
    python3 test/transitions.py --cases all --out /tmp/elevator-results

Requires: pebble-tool on PATH, Pillow.
"""

import argparse
import datetime
import json
import os
import socket
import subprocess
import sys
import tempfile
import time

from PIL import Image, ImageChops

INSTALL_SECONDS = 2.3  # roughly how long `pebble install` takes to relaunch the face
SETTLE_SECONDS = 3     # launch float is 1s; leave margin


def pebble(platform, *args):
    result = subprocess.run(["pebble", *args, "--emulator", platform],
                            capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit("pebble %s failed:\n%s%s" % (" ".join(args), result.stdout, result.stderr))


class Emulator:
    def __init__(self, platform):
        self.platform = platform
        self.clock_base = None  # (emulator time, monotonic time) at the last set_time

    def set_format(self, fmt):
        pebble(self.platform, "emu-time-format", "--format", fmt)

    def set_time(self, t):
        pebble(self.platform, "emu-set-time", "--utc", t.strftime("%H:%M:%S"))
        self.clock_base = (t, time.monotonic())

    def now(self):
        t, mono = self.clock_base
        return t + datetime.timedelta(seconds=time.monotonic() - mono)

    def wait_until(self, t):
        delay = (t - self.now()).total_seconds()
        if delay > 0:
            time.sleep(delay)

    def launch(self):
        pebble(self.platform, "install")

    def screenshot(self, path):
        pebble(self.platform, "screenshot", "--no-open", path)
        return Image.open(path).convert("RGB")

    def frames(self, start, end, interval=0.1):
        """
        Grab the display repeatedly between emulator times start and end, straight
        from QEMU's monitor. Much faster than `pebble screenshot`, so it can follow
        an animation; the frames include the emulator's border.
        """
        with open(os.path.join(tempfile.gettempdir(), "pb-emulator.json")) as f:
            qemu = next(iter(json.load(f)[self.platform].values()))["qemu"]
        monitor = socket.create_connection(("127.0.0.1", qemu["monitor"]))
        self._read_prompt(monitor)
        dump = os.path.join(tempfile.gettempdir(), "elevator-frame-%s.ppm" % self.platform)
        self.wait_until(start)
        grabbed = []
        while self.now() < end:
            monitor.sendall(("screendump %s\n" % dump).encode())
            self._read_prompt(monitor)
            grabbed.append(Image.open(dump).convert("RGB"))
            time.sleep(interval)
        monitor.close()
        return grabbed

    @staticmethod
    def _read_prompt(monitor):
        data = b""
        while not data.endswith(b"(qemu) "):
            data += monitor.recv(4096)


def at(hhmm, second=0):
    h, m = map(int, hhmm.split(":"))
    today = datetime.datetime.combine(datetime.date.today(), datetime.time())
    return today + datetime.timedelta(hours=h, minutes=m, seconds=second)


def differing_pixels(a, b):
    diff = ImageChops.difference(a, b).convert("L")
    return diff.width * diff.height - diff.histogram()[0], diff


def reference(emu, t, out, cache):
    """Screen for time t after a fresh launch has settled."""
    key = t.strftime("%H%M")
    if key not in cache:
        emu.set_time(t + datetime.timedelta(seconds=10) - datetime.timedelta(seconds=INSTALL_SECONDS))
        emu.launch()
        emu.wait_until(t + datetime.timedelta(seconds=10 + SETTLE_SECONDS))
        cache[key] = emu.screenshot(os.path.join(out, "ref_%s.png" % key))
    return cache[key]


def run_transition(emu, name, launch_at, check_at, expected_time, out, cache, check_motion=True):
    """
    Launch the face at launch_at, then compare the screen at check_at with the
    reference for expected_time. If check_motion, also confirm something moved
    around the minute change at the end of launch_at's minute.
    """
    ref = reference(emu, expected_time, out, cache)

    emu.set_time(launch_at - datetime.timedelta(seconds=INSTALL_SECONDS))
    emu.launch()

    problems = []
    if check_motion:
        minute_end = launch_at.replace(second=0, microsecond=0) + datetime.timedelta(minutes=1)
        frames = emu.frames(minute_end - datetime.timedelta(seconds=4), minute_end + datetime.timedelta(seconds=3))
        frames[0].save(os.path.join(out, "%s_anim.gif" % name), save_all=True,
                       append_images=frames[1:], duration=100, loop=0)
        if all(differing_pixels(frames[0], f)[0] == 0 for f in frames[1:]):
            problems.append("no animation around the minute change")

    emu.wait_until(check_at)
    after = emu.screenshot(os.path.join(out, "%s_after.png" % name))
    count, diff = differing_pixels(after, ref)
    if count:
        diff.point(lambda p: 255 if p else 0).save(os.path.join(out, "%s_diff.png" % name))
        problems.append("%d pixels differ from reference %s" % (count, expected_time.strftime("%H:%M")))
    return problems


def cases(kind):
    """Yield (name, launch_at, check_at, expected_time, check_motion)."""
    one_min = datetime.timedelta(minutes=1)

    if kind in ("hours", "all"):
        # Every hour change: HH:59 -> HH+1:00
        for h in range(24):
            target = at("%02d:00" % h)
            yield ("hour_%02d00" % h, target - one_min + datetime.timedelta(seconds=50),
                   target + datetime.timedelta(seconds=5), target, True)

    if kind in ("minutes", "all"):
        # Minute changes where a 1 appears or disappears, at hours with and without 1s
        for hour in ("10", "11", "20"):
            for minute in ("01", "02", "10", "11", "12", "20", "21", "22", "41", "42"):
                target = at("%s:%s" % (hour, minute))
                yield ("min_%s%s" % (hour, minute), target - one_min + datetime.timedelta(seconds=50),
                       target + datetime.timedelta(seconds=5), target, True)

    if kind in ("launch", "all"):
        # Launching the face right around a minute change
        for target_s in ("11:11", "10:00", "12:59", "01:00", "21:10"):
            target = at(target_s)
            for offset in (-3, -2, -1.5, -1, -0.5, 0, 0.5, 1):
                launch_at = target + datetime.timedelta(seconds=offset)
                yield ("launch_%s_%+.1f" % (target_s.replace(":", ""), offset), launch_at,
                       target + datetime.timedelta(seconds=SETTLE_SECONDS + 3), target, False)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--platform", default="basalt")
    parser.add_argument("--format", choices=["12h", "24h", "both"], default="both")
    parser.add_argument("--cases", choices=["hours", "minutes", "launch", "all"], default="all")
    parser.add_argument("--only", help="run only cases whose name contains this text")
    parser.add_argument("--out", default="test-results")
    args = parser.parse_args()

    emu = Emulator(args.platform)
    failures = 0
    for fmt in (["12h", "24h"] if args.format == "both" else [args.format]):
        out = os.path.join(args.out, args.platform, fmt)
        os.makedirs(out, exist_ok=True)
        emu.set_format(fmt)
        cache = {}
        for name, launch_at, check_at, expected, motion in cases(args.cases):
            if args.only and args.only not in name:
                continue
            problems = run_transition(emu, name, launch_at, check_at, expected, out, cache, motion)
            status = "FAIL" if problems else "ok"
            print("%-4s %s %s %s" % (status, fmt, name, "; ".join(problems)), flush=True)
            failures += bool(problems)

    print("%d failure(s)" % failures)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
