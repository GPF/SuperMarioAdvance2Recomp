#!/usr/bin/env python3
"""Run the bounded SMA2 SH-4 dynarec diagnostic once under nullDC3.

The run directory must be new. The script stages regular-file copies of the
supplied game assets (nullDC3's /pc refuses symlinks), selects the GBA runtime dynarec through /pc, captures the serial
console, validates stage outcomes and native block evidence, and records the
emulator process status separately from the guest diagnostic verdict.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pty
import re
import selectors
import shutil
import subprocess
import sys
import time
import tomllib
from pathlib import Path


STAGES = ("stage1", "stage2", "stage3", "stageD", "stageE")
GUEST_START_MARKERS = (b"KallistiOS v", b"[sma2] main entered",
                       b"[sma2] runtime entered", b"[sma2] ROM loaded:")
FATAL_MARKERS = (
    b"Looks like null pointer read",
    b"Unimplemented opcode:",
    b"KOS panic:",
    b"[sma2] fatal:",
)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def require_hash(path: Path, expected: str, label: str) -> None:
    actual = sha256(path)
    if actual.lower() != expected.lower():
        raise ValueError(f"{label} SHA-256 mismatch: expected {expected}, got {actual}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--elf-sha256", required=True)
    parser.add_argument("--game-toml", type=Path, required=True)
    parser.add_argument("--bios", type=Path, required=True)
    parser.add_argument("--bios-sha256", required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--rom-sha256", required=True)
    parser.add_argument("--run-dir", type=Path, required=True)
    parser.add_argument("--timeout-seconds", type=float, default=120.0)
    return parser.parse_args()


def run(args: argparse.Namespace) -> int:
    emulator = args.emulator.resolve(strict=True)
    elf = args.elf.resolve(strict=True)
    manifest = args.game_toml.resolve(strict=True)
    bios = args.bios.resolve(strict=True)
    rom = args.rom.resolve(strict=True)
    require_hash(elf, args.elf_sha256, "ELF")
    # REIOS rejects an ELF file larger than 16 MiB before loading its PT_LOAD
    # segments. Debug information may be stripped into this isolated run dir.
    if elf.stat().st_size > 16 * 1024 * 1024:
        raise ValueError(
            f"nullDC3 REIOS ELF file-size limit is 16 MiB; {elf} is "
            f"{elf.stat().st_size} bytes (strip debug data to a separate copy)"
        )
    require_hash(bios, args.bios_sha256, "BIOS")
    require_hash(rom, args.rom_sha256, "ROM")

    config = tomllib.loads(manifest.read_text())
    bios_spec = config["bios"]
    rom_spec = config["rom"]
    if hashlib.sha1(bios.read_bytes()).hexdigest() != bios_spec["sha1"].lower():
        raise ValueError("BIOS SHA-1 does not match game.toml")
    if hashlib.sha1(rom.read_bytes()).hexdigest() != rom_spec["sha1"].lower():
        raise ValueError("ROM SHA-1 does not match game.toml")

    run_dir = args.run_dir.resolve()
    run_dir.mkdir(parents=True, exist_ok=False)
    pcroot = run_dir / "pcroot"
    (pcroot / Path(bios_spec["path"]).parent).mkdir(parents=True)
    (pcroot / Path(rom_spec["path"]).parent).mkdir(parents=True)
    (pcroot / "saves").mkdir(exist_ok=True)
    shutil.copyfile(manifest, pcroot / "game.toml")
    shutil.copyfile(bios, pcroot / bios_spec["path"])
    shutil.copyfile(rom, pcroot / rom_spec["path"])
    (pcroot / "sma2_cpu_backend.txt").write_text("j\n")

    portable = run_dir / "portable"
    portable.mkdir()
    log_path = run_dir / "console.txt"
    result_path = run_dir / "result.json"
    timeout = float(args.timeout_seconds)
    if timeout <= 0:
        raise ValueError("timeout-seconds must be positive")
    config_args = [
        "config:Headless=yes", ",", "audio:backend=none", ",",
        "config:Debug.SerialConsoleEnabled=yes", ",",
        "config:Exit.OnGuestExit=yes", ",",
        "config:Exit.AfterSeconds=120", ",",
        "config:Dynarec.Enabled=0", ",", "config:pvr.backend=refsw2", ",",
        f"config:Dcload.PcRoot={pcroot}", ",", "config:Dcload.PcWritable=yes",
    ]
    command = [str(emulator), "--portable", str(portable), "--config",
               *config_args, str(elf)]
    (run_dir / "command.json").write_text(json.dumps(command, indent=2) + "\n")
    (run_dir / "inputs.sha256").write_text(
        f"{sha256(elf)}  {elf}\n{sha256(bios)}  {bios}\n{sha256(rom)}  {rom}\n")

    master, slave = pty.openpty()
    proc = None
    selector = selectors.DefaultSelector()
    result: dict[str, object] = {
        "emulator": str(emulator), "elf": str(elf), "run_dir": str(run_dir),
        "process_started": False, "elf_loaded": False,
        "guest_diagnostic_verdict": "FAIL", "emulator_process_status": None,
        "supervisor_action": "not_started", "timeout_seconds": timeout,
    }
    buffer = bytearray()
    final_seen = False
    fatal_marker = None
    deadline = time.monotonic() + timeout
    with log_path.open("xb") as log:
        try:
            proc = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                                    stdout=slave, stderr=slave,
                                    close_fds=True, start_new_session=True)
            result["process_started"] = True
            result["pid"] = proc.pid
            os.close(slave)
            slave = -1
            os.set_blocking(master, False)
            selector.register(master, selectors.EVENT_READ)
            result["supervisor_action"] = "waiting_for_guest_diagnostic"
            while time.monotonic() < deadline:
                for _, _ in selector.select(0.1):
                    try:
                        data = os.read(master, 65536)
                    except BlockingIOError:
                        continue
                    except OSError:
                        data = b""
                    if not data:
                        continue
                    log.write(data)
                    log.flush()
                    buffer.extend(data)
                    fatal_marker = next(
                        (marker.decode("ascii") for marker in FATAL_MARKERS
                         if marker in buffer), None
                    )
                    if fatal_marker:
                        result["supervisor_action"] = "fatal_guest_marker_seen"
                        break
                    if b"[sma2-jit-diff] === final diagnostic summary ===" in buffer:
                        final_seen = True
                        break
                if fatal_marker:
                    break
                if final_seen:
                    break
                if proc.poll() is not None:
                    break

            if not final_seen:
                # A process may exit between writing its last serial bytes and
                # the next poll. Drain those bytes before classifying the run.
                for _ in range(20):
                    try:
                        data = os.read(master, 65536)
                    except (BlockingIOError, OSError):
                        break
                    if not data:
                        break
                    log.write(data)
                    log.flush()
                    buffer.extend(data)
                final_seen = (
                    b"[sma2-jit-diff] === final diagnostic summary ===" in buffer
                )

            if final_seen:
                result["supervisor_action"] = "waiting_for_guest_exit"
                exit_deadline = time.monotonic() + 10.0
                while time.monotonic() < exit_deadline and proc.poll() is None:
                    for _, _ in selector.select(0.1):
                        try:
                            data = os.read(master, 65536)
                        except (BlockingIOError, OSError):
                            continue
                        except OSError:
                            data = b""
                        if data:
                            log.write(data)
                            log.flush()
                            buffer.extend(data)
                if proc.poll() is not None:
                    proc.wait()
                    result["supervisor_action"] = "guest_exit_observed"
                else:
                    result["supervisor_action"] = "terminated_owned_emulator_after_guest_summary"
                    proc.terminate()
                    try:
                        proc.wait(timeout=3.0)
                    except subprocess.TimeoutExpired:
                        result["supervisor_action"] = "killed_owned_emulator_after_guest_summary"
                        proc.kill()
                        proc.wait(timeout=3.0)
            elif proc.poll() is None:
                if not fatal_marker:
                    result["supervisor_action"] = "timeout_terminated_owned_emulator"
                proc.terminate()
                try:
                    proc.wait(timeout=3.0)
                except subprocess.TimeoutExpired:
                    result["supervisor_action"] = "timeout_killed_owned_emulator"
                    proc.kill()
                    proc.wait(timeout=3.0)

            # Drain bytes buffered by the terminal after process exit.
            for _ in range(20):
                try:
                    data = os.read(master, 65536)
                except (BlockingIOError, OSError):
                    break
                if not data:
                    break
                log.write(data)
                buffer.extend(data)
            result["emulator_process_status"] = proc.returncode
        finally:
            if proc is not None and proc.poll() is None:
                # This Popen object is the only process the supervisor may stop.
                proc.terminate()
                try:
                    proc.wait(timeout=3.0)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=3.0)
            selector.close()
            os.close(master)
            if slave >= 0:
                os.close(slave)

    text = buffer.decode("utf-8", errors="replace")
    start_ok = all(marker.decode() in text for marker in GUEST_START_MARKERS)
    elf_loaded = "KallistiOS v" in text
    backend_ok = ("sma2_cpu_backend=sh4-dynarec" in text and
                  "[sma2-jit] backend init succeeded" in text)
    stage_results: dict[str, dict[str, object]] = {}
    stage_re = re.compile(
        r"\[sma2-jit-diff\] (stage1|stage2|stage3|stageD|stageE) "
        r"status=(\S+).*?blocks=(\d+) fallback=(\d+) insns=(\d+) "
        r"interp_cycles=(\d+) native_cycles=(\d+) mismatches=(\d+) "
        r"cpsr=(\S+) iwram=(\S+)"
    )
    for match in stage_re.finditer(text):
        stage_results[match.group(1)] = {
            "status": match.group(2), "blocks": int(match.group(3)),
            "fallback": int(match.group(4)), "instructions": int(match.group(5)),
            "interpreter_cycles": int(match.group(6)),
            "native_cycles": int(match.group(7)), "mismatches": int(match.group(8)),
            "cpsr": match.group(9), "iwram": match.group(10),
        }
    stages_ok = all(
        stage in stage_results and stage_results[stage]["status"] == "PASS" and
        stage_results[stage]["blocks"] > 0 and stage_results[stage]["fallback"] == 0 and
        stage_results[stage]["mismatches"] == 0
        for stage in STAGES
    )
    process_ok = result["emulator_process_status"] == 0
    required_ok = (start_ok and backend_ok and final_seen and stages_ok and process_ok)
    result["elf_loaded"] = elf_loaded
    result["diagnostic_entry_reached"] = start_ok
    result["fatal_guest_marker"] = fatal_marker
    result["backend_initialized"] = backend_ok
    result["final_summary_seen"] = final_seen
    result["stage_results"] = stage_results
    result["guest_diagnostic_verdict"] = "PASS" if required_ok else "FAIL"
    result["guest_program_return_status"] = "not exposed by nullDC3 process status"
    result["reason"] = (
        "all required dynarec stages passed with native blocks and zero fallback"
        if required_ok else "missing startup/backend/summary, stage mismatch or ineligible, "
                            "nonzero emulator status, crash, or timeout"
    )
    result_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if required_ok else 1


if __name__ == "__main__":
    try:
        raise SystemExit(run(parse_args()))
    except Exception as exc:
        print(f"nulldc3 dynarec diagnostic preflight failed: {exc}", file=sys.stderr)
        raise SystemExit(2)
