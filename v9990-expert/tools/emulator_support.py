"""Isolated openMSX control for the V9990 expert edition; no BIOS files are copied.

Importing this module neither writes files nor starts an emulator. Call
prepare_profiles() to create a private FS-A1ST configuration using externally
installed, user-owned BIOS files and its original 256 KiB RAM. OpenMSX.start() explicitly starts one hidden child. Windows uses a
private token-authenticated loopback connection created by a local Tcl script,
because the installed GUI builds do not preserve stdout. Other platforms use
stdio. There is no shared bridge or fixed listening port.

Example:
    with OpenMSX() as emu:
        emu.load_rom("outputs/SAToReinker-Over-EXPERT.rom", mapper="ASCII8")
        emu.run_for(5)
        print(emu.machine_info(), emu.timing_violations())

The default renderer is none. Use OpenMSX(capture=True) for native screenshots:
this initializes the renderer before the machine and advances in real time.
On Windows it shows only this runner's own process window without activation.
It never substitutes a reconstruction for an emulator screenshot. read_block()
remains available in completely headless sessions.
"""

from __future__ import annotations

import argparse
import base64
from collections import deque
from dataclasses import dataclass
import json
import os
from pathlib import Path
import queue
import re
import socket
import subprocess
import threading
import time
import uuid
from xml.sax.saxutils import escape
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_EXE = Path(r"C:\Program Files\openMSX\openmsx.exe")
MACHINES = {"ntsc": "GRAZE_V9990_FS_A1ST"}


def tcl_word(value: object) -> str:
    """Quote one literal Tcl argument, including paths containing braces/$/[]."""
    text = str(value)
    replacements = {"\\": "\\\\", '"': '\\"', "$": "\\$", "[": "\\[",
                    "]": "\\]", "\n": "\\n", "\r": "\\r", "\t": "\\t"}
    return '"' + "".join(replacements.get(c, c) for c in text) + '"'


@dataclass(frozen=True)
class Profile:
    executable: Path
    directory: Path
    system_data: Path
    user_data: Path
    home: Path
    settings: Path
    extension: str = "gfx9000"

    def environment(self) -> dict[str, str]:
        env = os.environ.copy()
        env.update(OPENMSX_SYSTEM_DATA=str(self.system_data),
                   OPENMSX_USER_DATA=str(self.user_data), OPENMSX_HOME=str(self.home),
                   SDL_AUDIODRIVER="dummy")
        return env

    def command_line(self, standard: str, control: str = "stdio") -> list[str]:
        return [str(self.executable), "-control", control, "-setting", str(self.settings),
                "-machine", MACHINES[standard], "-exta", self.extension, "-command",
                "set renderer none;set power off;set pause on;set throttle off;"
                "set speed 100;set limitsprites true;set accuracy pixel"]


def prepare_profiles(work: Path | str | None = None,
                     executable: Path | str | None = None,
                     system_data: Path | str | None = None,
                     bios_directory: Path | str | None = None,
                     extension: str = "gfx9000") -> Profile:
    """Write private settings/config only; retain stock FS-A1ST 256 KiB hardware.

    External BIOS filenames are made absolute in the private machine XML. ROM
    contents are never copied, modified or included in an output package.
    """
    exe = Path(executable or os.environ.get("OPENMSX_EXE") or DEFAULT_EXE).resolve()
    share = Path(system_data or os.environ.get("OPENMSX_SYSTEM_DATA") or exe.parent / "share").resolve()
    bios = Path(bios_directory or share / "systemroms").resolve()
    if extension.lower() not in ("gfx9000", "video9000"):
        raise ValueError("extension must be gfx9000 or video9000")
    if not exe.is_file():
        raise FileNotFoundError(f"Install openMSX or set OPENMSX_EXE: {exe}")
    tree = ET.parse(share / "machines/Panasonic_FS-A1ST.xml")
    root = tree.getroot()
    for filename in root.findall(".//rom/filename"):
        external = bios / (filename.text or "")
        if not external.is_file():
            raise FileNotFoundError(f"Separately installed user-owned FS-A1ST ROM required: {external}")
        filename.text = external.as_posix()
    ram = root.find(".//PanasonicRAM/size")
    if ram is None or ram.text != "256":
        raise ValueError("Expected the stock FS-A1ST 256 KiB memory baseline")
    directory = Path(work or ROOT / "work").resolve() / "openmsx" / uuid.uuid4().hex[:12]
    user_data, home = directory / "user-data", directory / "home"
    (user_data / "machines").mkdir(parents=True)
    home.mkdir()
    root.find("info/code").text = MACHINES["ntsc"]
    root.find("info/description").text = "SAToReinker expert test: stock FS-A1ST 256 KiB; external BIOS; private settings."
    (user_data / "machines" / (MACHINES["ntsc"] + ".xml")).write_text(
        '<?xml version="1.0"?>\n<!DOCTYPE msxconfig SYSTEM "msxconfig2.dtd">\n' +
        ET.tostring(root, encoding="unicode"), encoding="utf-8")
    settings = directory / "settings.xml"
    settings.write_text(
        '<!DOCTYPE settings SYSTEM "settings.dtd">\n<settings><settings>'
        '<setting id="renderer">none</setting><setting id="sound_driver">null</setting>'
        '<setting id="power">false</setting><setting id="pause">true</setting>'
        '</settings><bindings/><shortcuts/></settings>\n', encoding="utf-8")
    return Profile(exe, directory, share, user_data, home, settings, extension.lower())


def read_symbols(path: Path | str) -> dict[str, int]:
    """Read JSON symbols, an SDCC map, or common label EQU/=/colon formats."""
    path = Path(path)
    text = path.read_text(encoding="utf-8-sig")
    if path.suffix.lower() == ".json":
        values = json.loads(text)
        return {k: int(v, 0) if isinstance(v, str) else int(v) for k, v in values.items()}
    symbols = {name: int(address, 16) for address, name in
               re.findall(r"^\s*([0-9A-Fa-f]{8})\s+([_A-Za-z]\w*)\s", text, re.M)}
    for name, number in re.findall(
            r"^\s*([_A-Za-z][\w.]*)\s*:?\s+(?:EQU\s+|=\s*)?"
            r"((?:0x|\$)[0-9A-Fa-f]+|[0-9A-Fa-f]+[hH]|\d+)\s*$", text, re.M | re.I):
        symbols[name] = int(number[1:], 16) if number.startswith("$") else (
            int(number[:-1], 16) if number.lower().endswith("h") else
            int(number, 16) if number.lower().startswith("0x") else int(number, 10))
    if not symbols:
        raise ValueError(f"No symbols found: {path}")
    return symbols


class _ScriptSocket:
    """Private loopback listener; only its token-authenticated child can connect."""

    def __init__(self, profile):
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.connection = self.reader = None
        self.token = uuid.uuid4().hex
        self.script = profile.directory / "private-control.tcl"
        self.script.write_text('''
proc graze_control_xml {value} {string map {& &amp; < &lt; > &gt;} $value}
proc graze_control_read {} {
    if {[gets $::graze_control_channel line] < 0} {
        if {[eof $::graze_control_channel]} {close $::graze_control_channel;quit}
        return
    }
    set code [catch {
        set command [encoding convertfrom utf-8 [binary decode base64 $line]]
        uplevel #0 $command
    } result]
    set status [expr {$code == 0 ? "ok" : "nok"}]
    puts $::graze_control_channel [format {<reply result="%s">%s</reply>} $status [graze_control_xml $result]]
    flush $::graze_control_channel
}
set ::graze_control_channel [socket 127.0.0.1 PORT]
fconfigure $::graze_control_channel -blocking 0 -buffering line -translation lf -encoding utf-8
puts $::graze_control_channel TOKEN
puts $::graze_control_channel {<openmsx-output>}
flush $::graze_control_channel
fileevent $::graze_control_channel readable graze_control_read
'''.replace("PORT", str(self.listener.getsockname()[1])).replace("TOKEN", self.token), encoding="utf-8")

    def connect(self, seconds):
        self.listener.settimeout(seconds)
        self.connection, _ = self.listener.accept()
        self.connection.settimeout(seconds)
        self.reader = self.connection.makefile("rb")
        if self.reader.readline().decode("ascii").strip() != self.token:
            raise RuntimeError("Unexpected client on private emulator control socket")
        self.connection.settimeout(None)
        self.listener.close()

    def read1(self, size):
        return self.reader.read1(size)

    def write(self, data):
        if data.strip() == b"<openmsx-control>":
            return
        if self.connection is None:
            raise BrokenPipeError("Private emulator control connection was never established")
        script = ET.fromstring(data).text or ""
        self.connection.sendall(base64.b64encode(script.encode("utf-8")) + b"\n")

    def close(self):
        if self.connection:
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self.connection.close()
        if self.reader:
            self.reader.close()
        self.listener.close()


class OpenMSX:
    """One private child process; calls are serialized, with bounded waits."""

    def __init__(self, standard: str = "ntsc", *, profile: Profile | None = None,
                 timeout: float = 20, capture: bool = False, **profile_options):
        if standard not in MACHINES:
            raise ValueError("This test helper uses the NTSC FS-A1ST baseline")
        self.standard, self.profile = standard, profile
        self.profile_options, self.timeout = profile_options, timeout
        self.capture = capture
        self.process = None
        self.transport = None
        self.replies = queue.Queue()
        self.messages = deque(maxlen=64)
        self._lock = threading.Lock()

    def start(self) -> "OpenMSX":
        if self.process is not None:
            raise RuntimeError("This emulator session has already been started")
        self.profile = self.profile or prepare_profiles(**self.profile_options)
        args = self.profile.command_line(self.standard)
        if self.capture:
            # Starting with renderer=none and enabling it later can leave stale
            # native frames in screenshots on the installed openMSX builds.
            # Write a private settings copy; never modify a supplied profile.
            tree = ET.parse(self.profile.settings)
            settings = tree.getroot().find("settings")
            for name, value in (("renderer", "SDLGL-PP"), ("throttle", "true")):
                element = settings.find(f"setting[@id='{name}']")
                if element is None:
                    element = ET.SubElement(settings, "setting", id=name)
                element.text = value
            capture_settings = self.profile.directory / "capture-settings.xml"
            capture_settings.write_text(
                '<!DOCTYPE settings SYSTEM "settings.dtd">\n' +
                ET.tostring(tree.getroot(), encoding="unicode"), encoding="utf-8")
            args[args.index("-setting") + 1] = str(capture_settings)
            command_index = args.index("-command") + 1
            args[command_index] = args[command_index].replace(
                "set renderer none;", "set renderer SDLGL-PP;").replace(
                "set throttle off;", "set throttle on;")
        if os.name == "nt":
            self.transport = _ScriptSocket(self.profile)
            del args[1:3]  # The installed GUI build does not preserve stdout.
            args += ["-script", str(self.transport.script)]
        self.process = subprocess.Popen(
            args, cwd=self.profile.directory,
            env=self.profile.environment(), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        threading.Thread(target=self._read_errors, daemon=True).start()
        try:
            if self.transport:
                self.transport.connect(self.timeout)
            threading.Thread(target=self._read_output, daemon=True).start()
            self._write(b"<openmsx-control>\n")
            self.command("openmsx_info version")
            self.enable_timing_checker()
            if self.capture:
                self.enable_capture()
        except BaseException:
            self.close()
            raise
        return self

    def _read_output(self):
        parser = ET.XMLPullParser(events=("end",))
        try:
            with (self.profile.directory / "control.xml.log").open("wb") as log:
                stream = self.transport or self.process.stdout
                while data := stream.read1(4096):
                    log.write(data)
                    log.flush()
                    parser.feed(data)
                    for _, element in parser.read_events():
                        if element.tag == "reply":
                            self.replies.put((element.get("result"), "".join(element.itertext())))
                        elif element.tag in ("log", "update"):
                            self.messages.append("".join(element.itertext()))
                        element.clear()
        except BaseException as exc:
            self.messages.append(str(exc))
        finally:
            self.replies.put(("closed", "openMSX control stream closed"))

    def _read_errors(self):
        for line in iter(self.process.stderr.readline, b""):
            self.messages.append(line.decode("utf-8", errors="replace").strip())

    def _write(self, data):
        if self.transport:
            self.transport.write(data)
        else:
            self.process.stdin.write(data)
            self.process.stdin.flush()

    def command(self, script: str, timeout: float | None = None) -> str:
        if self.process is None or self.process.poll() is not None:
            raise RuntimeError("Emulator is not running: " + "\n".join(self.messages))
        with self._lock:
            self._write(("<command>" + escape(script) + "</command>\n").encode("utf-8"))
            try:
                status, text = self.replies.get(timeout=self.timeout if timeout is None else timeout)
            except queue.Empty as exc:
                # A late reply could be mistaken for the following command's reply.
                self.process.terminate()
                raise TimeoutError("openMSX command timed out; private child stopped. " +
                                   "\n".join(self.messages)) from exc
            if status != "ok":
                raise RuntimeError(text + "\n" + "\n".join(self.messages))
            return text.strip()

    def load_rom(self, rom: Path | str, mapper: str = "ASCII8"):
        rom = Path(rom).resolve()
        if not rom.is_file():
            raise FileNotFoundError(rom)
        self.command(f"set power off;cartb {tcl_word(rom.as_posix())} -romtype {tcl_word(mapper)};"
                     "set power on;set pause on")

    def run_for(self, seconds: float, timeout: float | None = None):
        """Advance emulated time and finish paused; no game-memory injection."""
        if seconds <= 0:
            raise ValueError("seconds must be positive")
        marker = "::graze_done_" + uuid.uuid4().hex
        self.command(f"set {marker} 0;set {marker}_timer [after time {float(seconds)} "
                     f"{{set pause on;set {marker} 1}}];set pause off")
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        try:
            while self.command(f"set {marker}") != "1":
                if time.monotonic() >= deadline:
                    raise TimeoutError("Emulated-time advance did not finish; machine paused")
                time.sleep(0.01)
        finally:
            if self.process.poll() is None:
                self.command(f"set pause on;after cancel [set {marker}_timer];unset {marker}_timer {marker}")

    def read_block(self, device: str, address: int, size: int) -> bytes:
        return bytes.fromhex(self.command(
            f"binary encode hex [debug read_block {tcl_word(device)} {address} {size}]"))

    def write_block(self, device: str, address: int, data: bytes):
        self.command(f"debug write_block {tcl_word(device)} {address} [binary format H* {tcl_word(data.hex())}]")

    def read_symbol(self, symbols: dict[str, int], name: str, size: int = 1, signed: bool = False) -> int:
        return int.from_bytes(self.read_block("memory", symbols[name], size), "little", signed=signed)

    def key(self, row: int, mask: int, down: bool = True):
        self.command(f"keymatrix{'down' if down else 'up'} {row} {mask}")

    def screenshot(self, path: Path | str, size: int = 320) -> Path:
        """Real emulator screenshot; call enable_capture() before advancing frames."""
        if size not in (320, 640):
            raise ValueError("Native screenshot size must be 320 or 640")
        path = Path(path).resolve()
        path.parent.mkdir(parents=True, exist_ok=True)
        self.command(f"openmsx::internal_screenshot -raw -size {size} {tcl_word(path.as_posix())}")
        if not path.is_file():
            raise RuntimeError("Screenshot command completed without an output file")
        return path

    def enable_capture(self, renderer: str = "SDLGL-PP", minimize: bool = False):
        """Enable real rendering in ONLY this child process's window.

        Rendering must advance at least one frame after this call before a
        screenshot. The normal window is shown without activation by default;
        minimized rendering can yield stale frames on the installed build.
        No other openMSX instance is affected.
        """
        if self.command("set renderer") != renderer:
            self.command(f"set renderer {tcl_word(renderer)}")
        self.command("set minframeskip 0;set maxframeskip 0;"
                     "set deflicker false;set blur 0;set glow 0")
        if os.name == "nt":
            import ctypes
            from ctypes import wintypes
            user32 = ctypes.WinDLL("user32", use_last_error=True)
            callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
            user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
            user32.ShowWindowAsync.argtypes = [wintypes.HWND, ctypes.c_int]
            user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]

            @callback_type
            def show_owned(window, _):
                pid = wintypes.DWORD()
                user32.GetWindowThreadProcessId(window, ctypes.byref(pid))
                if pid.value == self.process.pid:
                    # SW_SHOWMINNOACTIVE or SW_SHOWNOACTIVATE: never steal focus.
                    user32.ShowWindowAsync(window, 7 if minimize else 4)
                return True

            user32.EnumWindows(show_owned, 0)

    def enable_timing_checker(self):
        self.command("set ::graze_fast_vram_count 0;"
                     "proc graze_vram_timing_violation {} {incr ::graze_fast_vram_count};"
                     "set VDP.too_fast_vram_access_callback graze_vram_timing_violation")

    def timing_violations(self) -> int:
        return int(self.command("set ::graze_fast_vram_count"))

    def machine_info(self) -> dict:
        return {"machine": self.command("machine_info config_name"),
                "version": self.command("openmsx_info version"),
                "ram_bytes": int(self.command("debug size {Main RAM}")),
                "vram_bytes": int(self.command("debug size {Sunrise GFX9000 VRAM}")),
                "internal_vdp_vram_bytes": int(self.command("debug size {physical VRAM}")),
                "cpu": self.command("get_active_cpu"),
                "extension": self.profile.extension,
                "s1990_register6": int(self.command("debug read {S1990 regs} 6")),
                "emulated_seconds": float(self.command("machine_info time")),
                "physical_hardware_tested": False}

    def close(self):
        if self.process is None:
            return
        if self.process.poll() is None:
            try:
                self._write(b"<command>quit</command>\n")
                self.process.wait(timeout=3)
            except (OSError, subprocess.TimeoutExpired):
                self.process.terminate()
                try:
                    self.process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=3)
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()
        if self.transport:
            self.transport.close()

    def __enter__(self):
        return self.start()

    def __exit__(self, *_):
        self.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare-only", action="store_true", help="write profiles and print CLI; do not launch")
    parser.add_argument("--standard", choices=MACHINES, default="ntsc")
    parser.add_argument("--rom", type=Path)
    parser.add_argument("--mapper", default="ASCII8")
    parser.add_argument("--seconds", type=float, default=5)
    args = parser.parse_args()
    profile = prepare_profiles()
    if args.prepare_only:
        print(json.dumps({"profile": str(profile.directory), "command": profile.command_line(args.standard) if os.name != "nt" else None,
                          "control": "Call OpenMSX.start(): private Tcl loopback on Windows, stdio elsewhere",
                          "environment": {k: v for k, v in profile.environment().items()
                                          if k.startswith("OPENMSX_") or k == "SDL_AUDIODRIVER"}}, indent=2))
    else:
        if not args.rom:
            parser.error("--rom is required unless --prepare-only is specified")
        with OpenMSX(args.standard, profile=profile) as emulator:
            emulator.load_rom(args.rom, args.mapper)
            emulator.run_for(args.seconds)
            print(json.dumps({**emulator.machine_info(), "vram_timing_violations": emulator.timing_violations()}, indent=2))
