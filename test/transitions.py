#!/usr/bin/env python3
"""
Emulator test rig for Elevator's time transitions.

For each case it sets the emulator clock, launches the watchface, lets real
time run through a minute change, and screenshots the result once everything
has settled. That screenshot is compared pixel-for-pixel against a reference:
the same time shown by a freshly launched face after its launch float has
finished. If a digit failed to come back, or came back in the wrong place, the
two differ.

It also records the transition as a GIF (frames grabbed straight from the
emulator's display) and checks that the digits actually animated.

The rig talks to the emulator directly (through pebble-tool's own libpebble2)
and relaunches the face instead of reinstalling it. If the emulator stops
responding, it is rebooted from a clean state and the case is retried once.

Results, including any failing screenshots and diffs, go under --out.

Usage (from the repo root, with the project already built):
    python3 test/transitions.py --platform basalt --format 24h --cases hours
    python3 test/transitions.py --platform aplite,basalt --cases all --out /tmp/elevator-results

Requires: pebble-tool on PATH (the script re-runs itself with pebble-tool's
Python, which has libpebble2 and Pillow).
"""

import argparse
import datetime
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import uuid

try:
    from libpebble2.communication import PebbleConnection
    from libpebble2.communication.transports.websocket import WebsocketTransport
    from libpebble2.protocol.apps import AppRunState, AppRunStateStart, AppRunStateStop
    from libpebble2.protocol.system import WatchVersion, WatchVersionRequest, TimeMessage, SetUTC
except ImportError:
    # Re-run with the interpreter pebble-tool was installed with, which has libpebble2
    pebble_path = shutil.which("pebble")
    with open(pebble_path) as f:
        interpreter = f.readline()[2:].strip()
    os.execv(interpreter, [interpreter] + sys.argv)

from PIL import Image, ImageChops

LAUNCH_SECONDS = 0.5   # roughly how long a relaunch takes before the face is running
LEAVE_LEAD = 6         # transition cases launch this many seconds before the minute changes
APP_UUID = uuid.UUID(json.load(open("package.json"))["pebble"]["uuid"])


class EmulatorHung(Exception):
    pass


# When an emulator goes away, its connection's reader thread fails noisily; the rig notices on its own
_default_excepthook = threading.excepthook
threading.excepthook = lambda a: None if a.thread is not None and a.thread.name == "PebbleConnection" else _default_excepthook(a)


def pebble(platform, *args, timeout=1500):
    result = subprocess.run(["pebble", *args, "--emulator", platform],
                            capture_output=True, text=True, timeout=timeout)
    if result.returncode != 0:
        raise EmulatorHung("pebble %s failed:\n%s%s" % (" ".join(args), result.stdout, result.stderr))


class Emulator:
    def __init__(self, platform):
        self.platform = platform
        self.clock_base = None  # (emulator time, monotonic time) at the last set_time
        self.format = None
        self.conn = None
        self.monitor = None
        self.dump = os.path.join(tempfile.gettempdir(), "elevator-frame-%s.ppm" % platform)
        self.start()

    def start(self):
        """Boot the emulator if needed, install the current build, and connect."""
        pebble(self.platform, "install")
        with open(os.path.join(tempfile.gettempdir(), "pb-emulator.json")) as f:
            info = next(iter(json.load(f)[self.platform].values()))
        self.conn = PebbleConnection(WebsocketTransport("ws://localhost:%d/" % info["pypkjs"]["port"]))
        self.conn.connect()
        self.conn.run_async()
        # QEMU's monitor takes one client at a time, so keep this one for every screen grab
        self.monitor = socket.create_connection(("127.0.0.1", info["qemu"]["monitor"]))
        self._read_prompt()
        if self.format:
            pebble(self.platform, "emu-time-format", "--format", self.format)

    def reboot(self):
        """Throw away the emulator and its saved state, and boot a fresh one."""
        for closer in (lambda: self.monitor.close(), lambda: self.conn.close()):
            try:
                closer()
            except Exception:
                pass
        # Stop only this platform's QEMU and pypkjs, so emulators for other platforms keep running
        info_path = os.path.join(tempfile.gettempdir(), "pb-emulator.json")
        with open(info_path) as f:
            all_info = json.load(f)
        for info in all_info.pop(self.platform, {}).values():
            for pid in (info["qemu"]["pid"], info["pypkjs"]["pid"]):
                try:
                    os.kill(pid, 15)
                except ProcessLookupError:
                    pass
        with open(info_path, "w") as f:
            json.dump(all_info, f)
        time.sleep(2)
        shutil.rmtree(os.path.expanduser("~/.local/share/pebble-sdk/%s/%s" % (self.sdk_version(), self.platform)),
                      ignore_errors=True)
        self.start()

    @staticmethod
    def sdk_version():
        return os.path.basename(os.path.realpath(os.path.expanduser("~/.local/share/pebble-sdk/SDKs/current")))

    def set_format(self, fmt):
        self.format = fmt
        pebble(self.platform, "emu-time-format", "--format", fmt)

    def set_time(self, t):
        """Set the emulator clock so it reads t when this returns."""
        # The watch only takes whole seconds: set the second below t, then wait out the fraction
        whole = t.replace(microsecond=0)
        ts = int(whole.replace(tzinfo=datetime.timezone.utc).timestamp())
        self.conn.send_packet(TimeMessage(message=SetUTC(unix_time=ts, utc_offset=0, tz_name="UTC")))
        self.clock_base = (whole, time.monotonic())
        self.wait_until(t)

    def now(self):
        t, mono = self.clock_base
        return t + datetime.timedelta(seconds=time.monotonic() - mono)

    def wait_until(self, t):
        delay = (t - self.now()).total_seconds()
        if delay > 0:
            time.sleep(delay)

    def launch(self):
        """Restart the face, and make sure the watch reports it running (a hung emulator won't)."""
        queue = self.conn.get_endpoint_queue(AppRunState)
        try:
            self.conn.send_packet(AppRunState(data=AppRunStateStop(uuid=APP_UUID)))
            time.sleep(0.15)
            self.conn.send_packet(AppRunState(data=AppRunStateStart(uuid=APP_UUID)))
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                try:
                    msg = queue.get(timeout=max(0.1, deadline - time.monotonic()))
                except Exception:
                    break
                if isinstance(msg.data, AppRunStateStart) and msg.data.uuid == APP_UUID:
                    return
            raise EmulatorHung("the face didn't start within 5 s")
        finally:
            queue.close()

    def check_alive(self):
        """A frozen emulator still gives identical (so "settled") screenshots; make sure the watch answers."""
        try:
            # Ask for the firmware version: a ping would leave a "Ping" notification over the face
            self.conn.send_and_read(WatchVersion(data=WatchVersionRequest()), WatchVersion, timeout=5)
        except Exception as e:
            raise EmulatorHung("the watch didn't answer (%s)" % type(e).__name__)

    def grab(self):
        """The screen as the watch shows it, straight from QEMU (much faster than `pebble screenshot`)."""
        try:
            self.monitor.sendall(("screendump %s\n" % self.dump).encode())
            self._read_prompt()
        except OSError as e:
            raise EmulatorHung("QEMU monitor: %s" % e)
        # QEMU's display shows the screen inset by 2 px and with greys scaled to 2/3 (255 shows as 170)
        img = Image.open(self.dump).convert("RGB").crop((2, 2, 146, 170))
        return img.point(lambda v: min(255, round(v * 1.5)))

    def screenshot(self, path):
        img = self.grab()
        img.save(path)
        return img

    def frames(self, start, end, interval=0.1):
        """Grab the display repeatedly between emulator times start and end, to follow an animation."""
        self.wait_until(start)
        grabbed = []
        while self.now() < end:
            grabbed.append(self.grab())
            time.sleep(interval)
        return grabbed

    def _read_prompt(self):
        self.monitor.settimeout(10)
        data = b""
        while not data.endswith(b"(qemu) "):
            chunk = self.monitor.recv(4096)
            if not chunk:
                raise EmulatorHung("QEMU monitor closed")
            data += chunk


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
        emu.set_time(t + datetime.timedelta(seconds=1) - datetime.timedelta(seconds=LAUNCH_SECONDS))
        emu.launch()
        emu.wait_until(t + datetime.timedelta(seconds=2.5))  # the launch float takes 1 s
        cache[key] = settled_screenshot(emu, os.path.join(out, "ref_%s.png" % key))
    return cache[key]


def settled_screenshot(emu, path, attempts=10):
    """Screenshot once two in a row match, so a launch float still finishing isn't captured."""
    shot = emu.screenshot(path)
    for _ in range(attempts):
        time.sleep(0.5)
        again = emu.screenshot(path)
        if differing_pixels(shot, again)[0] == 0:
            return again
        shot = again
    raise EmulatorHung("screen never settled for %s" % path)


def run_transition(emu, name, launch_at, check_at, expected_time, out, cache, check_motion=True, jump=None):
    """
    Launch the face at launch_at, then compare the settled screen after check_at with
    the reference for expected_time. If check_motion, also confirm something moved
    around the minute change at the end of launch_at's minute. If jump is
    (jump_at, jump_to), the clock jumps to jump_to at jump_at while the face runs.
    """
    ref = reference(emu, expected_time, out, cache)

    emu.set_time(launch_at - datetime.timedelta(seconds=LAUNCH_SECONDS))
    emu.launch()
    if jump:
        jump_at, jump_to = jump
        emu.wait_until(jump_at)
        emu.set_time(jump_to)

    problems = []
    if check_motion:
        minute_end = launch_at.replace(second=0, microsecond=0) + datetime.timedelta(minutes=1)
        frames = emu.frames(minute_end - datetime.timedelta(seconds=3), minute_end + datetime.timedelta(seconds=2.5))
        frames[0].save(os.path.join(out, "%s_anim.gif" % name), save_all=True,
                       append_images=frames[1:], duration=100, loop=0)
        if all(differing_pixels(frames[0], f)[0] == 0 for f in frames[1:]):
            problems.append("no animation around the minute change")

    emu.wait_until(check_at)
    after = settled_screenshot(emu, os.path.join(out, "%s_after.png" % name))
    count, diff = differing_pixels(after, ref)
    if count:
        emu.check_alive()  # a frozen emulator is the emulator's fault, not the face's
        diff.point(lambda p: 255 if p else 0).save(os.path.join(out, "%s_diff.png" % name))
        problems.append("%d pixels differ from reference %s" % (count, expected_time.strftime("%H:%M")))
    return problems


def cases(kind):
    """Yield (name, launch_at, check_at, expected_time, check_motion, jump)."""
    lead = datetime.timedelta(seconds=LEAVE_LEAD)

    if kind in ("hours", "all"):
        # Every hour change: HH:59 -> HH+1:00
        for h in range(24):
            target = at("%02d:00" % h)
            yield ("hour_%02d00" % h, target - lead, target + datetime.timedelta(seconds=3), target, True, None)

    if kind in ("minutes", "all"):
        # Minute changes where a 1 appears or disappears, at hours with and without 1s
        for hour in ("10", "11", "20"):
            for minute in ("01", "02", "10", "11", "12", "20", "21", "22", "41", "42"):
                target = at("%s:%s" % (hour, minute))
                yield ("min_%s%s" % (hour, minute), target - lead, target + datetime.timedelta(seconds=3), target, True, None)

    if kind in ("launch", "all"):
        # Launching the face right around a minute change
        for target_s in ("11:11", "10:00", "12:59", "01:00", "21:10"):
            target = at(target_s)
            for offset in (-3, -2, -1.5, -1, -0.5, 0, 0.5, 1):
                launch_at = target + datetime.timedelta(seconds=offset)
                yield ("launch_%s_%+.1f" % (target_s.replace(":", ""), offset), launch_at,
                       target + datetime.timedelta(seconds=4), target, False, None)

    if kind in ("jumps", "all"):
        # The clock jumping while the face runs (daylight saving, time zone changes, a phone time sync),
        # including mid-way through digits leaving or arriving
        for name, launch_s, jump_s, to_s in (
                ("fwd_1h", "10:20:05", "10:20:08", "11:20:05"),
                ("back_1h", "11:20:05", "11:20:08", "10:20:05"),
                ("dst_spring", "01:59:20", "01:59:23", "03:00:05"),
                ("dst_fall", "01:59:20", "01:59:23", "01:00:05"),
                ("midnight_back", "00:30:05", "00:30:08", "23:30:05"),
                ("ones_to_none", "11:11:05", "11:11:08", "20:20:05"),
                ("none_to_ones", "20:20:05", "20:20:08", "11:11:05"),
                ("noon_to_one", "12:59:05", "12:59:08", "13:00:05"),
                ("during_leave", "10:59:53", "10:59:58.8", "10:30:05"),
                ("back_during_leave", "10:59:53", "10:59:59", "10:59:20"),
                ("during_arrive", "10:59:53", "11:00:00.3", "21:10:05")):
            jump_to = datetime.datetime.combine(datetime.date.today(), datetime.datetime.strptime(to_s, "%H:%M:%S").time())
            launch_at = datetime.datetime.combine(datetime.date.today(), datetime.datetime.strptime(launch_s, "%H:%M:%S").time())
            jump_at = datetime.datetime.combine(datetime.date.today(), datetime.datetime.strptime(jump_s, "%H:%M:%S.%f" if "." in jump_s else "%H:%M:%S").time())
            yield ("jump_" + name, launch_at, jump_to + datetime.timedelta(seconds=3), jump_to.replace(second=0), False,
                   (jump_at, jump_to))


def run_side_by_side(platforms):
    """Run this script once per platform, each with its own emulator, at the same time."""
    procs = []
    for platform in platforms:
        argv = [a for a in sys.argv[1:]]
        i = argv.index("--platform")
        argv[i + 1] = platform
        log = open("transitions-%s.log" % platform, "w")
        procs.append((platform, log, subprocess.Popen([sys.executable, sys.argv[0]] + argv, stdout=log, stderr=subprocess.STDOUT)))
    failed = False
    for platform, log, proc in procs:
        proc.wait()
        log.close()
        with open(log.name) as f:
            lines = f.read().splitlines()
        print("%s: %s (log in %s)" % (platform, lines[-1] if lines else "no output", log.name))
        failed |= proc.returncode != 0
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--platform", default="basalt",
                        help="watch model, or several separated by commas to test them side by side (e.g. aplite,basalt)")
    parser.add_argument("--format", choices=["12h", "24h", "both"], default="both")
    parser.add_argument("--cases", choices=["hours", "minutes", "launch", "jumps", "all"], default="all")
    parser.add_argument("--only", help="run only cases whose name contains this text")
    parser.add_argument("--out", default="test-results")
    args = parser.parse_args()

    platforms = args.platform.split(",")
    if len(platforms) > 1:
        sys.exit(run_side_by_side(platforms))

    emu = Emulator(args.platform)
    failures = 0
    for fmt in (["12h", "24h"] if args.format == "both" else [args.format]):
        out = os.path.join(args.out, args.platform, fmt)
        os.makedirs(out, exist_ok=True)
        emu.set_format(fmt)
        cache = {}
        for name, launch_at, check_at, expected, motion, jump in cases(args.cases):
            if args.only and args.only not in name:
                continue
            try:
                problems = run_transition(emu, name, launch_at, check_at, expected, out, cache, motion, jump)
            except EmulatorHung as e:
                # The emulator stops responding after long runs; start over from a clean one and retry once
                print("     emulator stopped responding (%s); rebooting it" % str(e).splitlines()[0], flush=True)
                emu.reboot()
                cache = {}
                problems = run_transition(emu, name, launch_at, check_at, expected, out, cache, motion, jump)
            status = "FAIL" if problems else "ok"
            print("%-4s %s %s %s" % (status, fmt, name, "; ".join(problems)), flush=True)
            failures += bool(problems)

    print("%d failure(s)" % failures)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
