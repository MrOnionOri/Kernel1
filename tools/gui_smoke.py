#!/usr/bin/env python3
"""Headless QEMU GUI regression test. Run after ./build.sh, from the repo root."""
import json
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"


class Guest:
    def __init__(self, directory, memory_mb=128):
        self.directory = Path(directory)
        self.symbols = {}
        for line in subprocess.check_output(["nm", "-n", str(BUILD / "kernel.elf")], text=True).splitlines():
            parts = line.split()
            if len(parts) == 3:
                self.symbols[parts[2]] = int(parts[0], 16)
        address = self.directory / "qmp.sock"
        self.process = subprocess.Popen([
            "qemu-system-i386", "-m", str(memory_mb), "-display", "none", "-vga", "std", "-snapshot",
            "-no-reboot", "-qmp", f"unix:{address},server=on,wait=off",
            "-drive", f"if=ide,index=0,format=raw,file={BUILD / 'kernel1.img'}",
            "-drive", f"if=ide,index=1,format=raw,file={BUILD / 'data.img'}",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(10)
        for _ in range(100):
            try:
                self.socket.connect(str(address))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                time.sleep(0.05)
        else:
            self.close()
            raise RuntimeError("QMP did not start")
        self.stream = self.socket.makefile("rwb", buffering=0)
        self.stream.readline()
        self.qmp("qmp_capabilities")

    def close(self):
        self.process.terminate()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.socket.close()

    def qmp(self, command, **arguments):
        self.stream.write((json.dumps({"execute": command, "arguments": arguments}) + "\n").encode())
        while True:
            reply = json.loads(self.stream.readline())
            if "error" in reply:
                raise RuntimeError(reply["error"])
            if "return" in reply:
                return reply["return"]

    def memory(self, address, size):
        output = self.directory / "memory.bin"
        self.qmp("pmemsave", val=address, size=size, filename=str(output))
        return output.read_bytes()

    def integer(self, name):
        return struct.unpack("<i", self.memory(self.symbols[name], 4))[0]

    def text(self, name, size=128):
        return self.memory(self.symbols[name], size).split(b"\0", 1)[0].decode("ascii", errors="replace")

    def window(self, index):
        fields = struct.unpack("<16i", self.memory(self.symbols["gfx_windows"] + index * 64, 64))
        return dict(zip(("x", "y", "w", "h", "z", "visible", "minimized", "maximized"), fields))

    def move(self, x, y):
        for _ in range(35):
            dx = x - self.integer("gfx_cursor_x")
            dy = y - self.integer("gfx_cursor_y")
            if dx == 0 and dy == 0:
                time.sleep(0.12)
                return
            events = [{"type": "rel", "data": {"axis": axis, "value": max(-90, min(90, delta))}}
                      for axis, delta in (("x", dx), ("y", dy)) if delta]
            self.qmp("input-send-event", events=events)
            time.sleep(0.055)
        raise AssertionError(f"Cursor did not reach {x}, {y}: actual "
                             f"{self.integer('gfx_cursor_x')}, {self.integer('gfx_cursor_y')}")

    def button(self, down):
        self.qmp("input-send-event", events=[{"type": "btn", "data": {"button": "left", "down": down}}])
        time.sleep(0.16)
        for _ in range(40):
            if bool(self.integer("gfx_windows_last_buttons") & 1) == down:
                return
            time.sleep(0.05)
        raise AssertionError(f"Window manager did not process mouse button {down}: "
                             f"previous={self.integer('gfx_windows_last_buttons') & 255}, "
                             f"mouse={self.memory(self.symbols['mouse'], 64).hex()}")

    def click(self, x, y):
        self.move(x, y)
        self.button(True)
        self.button(False)

    def dock(self, index):
        self.click(100, 44 + 34 + index * 30 + 13)

    def key(self, key):
        self.qmp("human-monitor-command", **{"command-line": f"sendkey {key}"})
        time.sleep(0.09)

    def command(self, text):
        for character in text:
            self.key("spc" if character == " " else character)
        self.key("ret")
        time.sleep(0.3)

    def screenshot(self, name):
        self.qmp("screendump", filename=str(BUILD / f"gui-{name}.png"), format="png")

    def pixels(self):
        time.sleep(0.15)
        self.qmp("stop")
        try:
            width, height, pitch, _, address = struct.unpack("<5I", self.memory(self.symbols["framebuffer"], 20))
            assert width == 1024 and height == 768
            return self.memory(address, pitch * height)
        finally:
            self.qmp("cont")

    def check_presented_rows(self):
        self.qmp("stop")
        try:
            width, height, pitch, _, front = struct.unpack("<5I", self.memory(self.symbols["framebuffer"], 20))
            assert (width, height, pitch) == (1024, 768, 4096)
            entry = struct.unpack("<I", self.memory(self.symbols["page_directory"] + 5 * 4, 4))[0]
            table = struct.unpack("<1024I", self.memory(entry & ~4095, 4096))
            for row in (0, 80, 343, 600, 767):
                assert table[row] & 1 and not table[row] & 4
                assert self.memory(table[row] & ~4095, pitch) == self.memory(front + row * pitch, pitch), \
                    f"Presented row {row} does not match the composed surface"
        finally:
            self.qmp("cont")


def run(guest):
    for _ in range(100):
        if guest.integer("gfx_desktop_clean") == 1:
            break
        time.sleep(0.1)
    time.sleep(0.5)
    assert guest.integer("gfx_desktop_clean") == 1, "Desktop failed to boot"
    assert not guest.window(3)["visible"], "Terminal should start hidden"
    assert guest.window(0)["visible"] and guest.window(2)["visible"]
    assert guest.symbols["bss_start"] >= 0x100000
    assert guest.integer("framebuffer_backbuffer") == 0x01400000, "Backbuffer not mapped"
    directory = struct.unpack("<I", guest.memory(guest.symbols["page_directory"] + 5 * 4, 4))[0]
    assert directory & 1 and not directory & 4, "Backbuffer must be supervisor-only"
    assert guest.integer("framebuffer_present_count") > 0, "No composed frame was presented"
    assert guest.memory(0x80000, 6) == b"K1RD2\0", "Initrd was not loaded"
    initrd_before = guest.memory(0x80000, 8192)
    guest.screenshot("boot")
    files = guest.window(0)
    guest.move(files["x"] + 22, files["y"] + 48)
    assert guest.integer("gfx_hover_id") != 0, "Toolbar hover not tracked"
    guest.screenshot("hover")
    guest.move(900, 680)
    assert guest.integer("gfx_hover_id") == 0, "Hover did not clear"
    guest.command("echo hidden")
    assert guest.integer("command_length") == 0, "Hidden terminal accepted input"
    guest.dock(5)
    assert guest.window(7)["visible"], "Settings did not open"
    settings = guest.window(7)
    guest.click(settings["x"] + 110, settings["y"] + 75)
    assert guest.integer("gfx_desktop_color_index") == 1
    guest.click(settings["x"] + 24, settings["y"] + 120)
    assert guest.integer("gfx_live_widgets") == 0
    guest.screenshot("settings")
    guest.move(900, 680)
    baseline = guest.pixels()
    guest.check_presented_rows()
    for point in ((0, 0), (1023, 767), (300, 170), (450, 430), (900, 680)):
        guest.move(*point)
    assert baseline == guest.pixels(), "Moving the cursor left pixel artifacts"
    guest.check_presented_rows()
    guest.click(settings["x"] + 24, settings["y"] + 120)
    assert guest.integer("gfx_live_widgets") == 1
    guest.click(settings["x"] + settings["w"] - 12, settings["y"] + 12)
    assert not guest.window(7)["visible"]
    guest.click(450, 430)
    assert guest.integer("gfx_desktop_clean") == 1
    print("PASS: GUI boot, keyboard focus, settings, empty desktop clicks", flush=True)

    guest.dock(0)
    files = guest.window(0)
    guest.click(files["x"] + 60, files["y"] + 98)
    assert guest.text("current_directory") == "apps", "Initrd apps folder missing"
    guest.click(files["x"] + 118, files["y"] + 48)
    assert guest.integer("gfx_files_page") > 0, "Next page did not advance"
    guest.click(files["x"] + 86, files["y"] + 48)
    assert guest.integer("gfx_files_page") == 0
    guest.click(files["x"] + 60, files["y"] + 98)
    assert guest.window(6)["visible"] and guest.integer("gfx_viewer_loaded") == 1
    guest.screenshot("viewer")
    guest.dock(0)
    guest.click(files["x"] + 54, files["y"] + 48)
    assert guest.text("current_directory") == ""
    print("PASS: file browsing, paging, viewer and parent navigation", flush=True)

    guest.move(files["x"] + 100, files["y"] + 12)
    guest.button(True)
    guest.move(files["x"] + 116, files["y"] + 380)
    guest.button(False)
    assert guest.window(0)["y"] > files["y"] + 200, "Window did not drag"
    guest.dock(1)
    apps = guest.window(2)
    guest.click(apps["x"] + 80, apps["y"] + 134)
    assert guest.window(4)["visible"], "Clock tile did not open clock"
    guest.screenshot("clock")
    clock_window = guest.window(4)
    guest.click(clock_window["x"] + clock_window["w"] - 12, clock_window["y"] + 12)
    guest.dock(5)
    settings = guest.window(7)
    guest.click(settings["x"] + 75, settings["y"] + 166)
    assert guest.window(0)["y"] == files["y"], "Arrange did not restore window placement"
    assert guest.window(6)["visible"], "Arrange should preserve open files"
    guest.click(settings["x"] + settings["w"] - 12, settings["y"] + 12)
    print("PASS: cursor pixel restoration, dragging, app tiles and arrangement", flush=True)

    guest.dock(6)
    assert guest.window(3)["visible"]
    guest.command("echo guiok")
    cells = guest.memory(guest.symbols["console_cells"], 96 * 48)
    assert b"guiok" in cells
    shell = guest.window(3)
    guest.click(shell["x"] + shell["w"] - 60, shell["y"] + 12)
    assert guest.window(3)["minimized"] and not guest.window(3)["visible"]
    guest.dock(6)
    assert guest.window(3)["visible"] and not guest.window(3)["minimized"]
    shell = guest.window(3)
    guest.click(shell["x"] + shell["w"] - 36, shell["y"] + 12)
    assert guest.window(3)["maximized"]
    guest.dock(6)
    assert guest.window(3)["maximized"], "Launcher should preserve maximization"
    shell = guest.window(3)
    guest.click(shell["x"] + shell["w"] - 36, shell["y"] + 12)
    assert not guest.window(3)["maximized"]
    guest.screenshot("terminal")
    print("PASS: terminal input, minimize, restore and maximize", flush=True)
    guest.command("run hello")
    assert guest.integer("shell_last_status") == 0, "KAPP did not execute"
    guest.command("reap 1")
    used_before = guest.integer("used_pages")
    heap_before = guest.integer("pages_used")
    # Task slots retain their kernel stacks. Account for that one-time heap growth.
    guest.command("memtest")
    time.sleep(0.5)
    assert guest.integer("used_pages") - used_before == guest.integer("pages_used") - heap_before
    guest.dock(6)
    guest.command("memtest")
    time.sleep(0.5)
    cells = guest.memory(guest.symbols["console_cells"], 96 * 48)
    assert b"memtest: PASS" in cells, "Process memory regression"
    guest.dock(6)
    guest.command("gfx windows off")
    assert guest.integer("gfx_windows_enabled") == 0
    assert guest.integer("console_deferred") == 0
    guest.move(800, 650)
    guest.command("echo cursorcheck")
    console_pixels = guest.pixels()
    guest.move(500, 500)
    guest.move(800, 650)
    assert console_pixels == guest.pixels(), "Legacy console repaint corrupted the cursor background"
    guest.command("gfx desktop")
    assert guest.integer("gfx_desktop_clean") == 1 and guest.integer("gfx_windows_enabled") == 1
    assert not guest.window(3)["visible"]
    assert guest.memory(0x80000, 8192) == initrd_before, "Initrd changed after running apps"
    guest.dock(0)
    files = guest.window(0)
    guest.click(files["x"] + 60, files["y"] + 98)
    assert guest.text("current_directory") == "apps", "Files lost initrd entries after running apps"
    guest.click(files["x"] + 22, files["y"] + 48)
    time.sleep(0.4)
    guest.screenshot("final")
    print("PASS: KAPP execution, process memory cleanup and console fallback", flush=True)


def low_memory(guest):
    for _ in range(100):
        if guest.integer("gfx_desktop_clean") == 1:
            break
        time.sleep(0.1)
    time.sleep(0.5)
    assert guest.integer("gfx_desktop_clean") == 1, "Low-memory desktop failed to boot"
    assert guest.integer("framebuffer_backbuffer") == 0, "Expected direct rendering fallback"
    entry = struct.unpack("<I", guest.memory(guest.symbols["page_directory"] + 5 * 4, 4))[0]
    if entry & 1:
        assert guest.memory(entry & ~4095, 4096) == bytes(4096), "Partial backbuffer mappings leaked"
    free = 32768 - guest.integer("used_pages")
    assert free == guest.integer("managed_pages") - guest.integer("pages_used"), "Backbuffer allocation leaked pages"
    guest.move(900, 680)
    guest.dock(5)
    assert guest.window(7)["visible"], "Direct rendering fallback lost mouse input"
    guest.screenshot("low-memory")
    print("PASS: low-memory allocation rollback and direct-rendering fallback", flush=True)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="kernel1-gui-") as directory:
        guest = Guest(directory)
        try:
            run(guest)
        except Exception:
            guest.screenshot("failure")
            raise
        finally:
            guest.close()
    with tempfile.TemporaryDirectory(prefix="kernel1-low-memory-") as directory:
        guest = Guest(directory, memory_mb=4)
        try:
            low_memory(guest)
        finally:
            guest.close()
