#!/usr/bin/env python3
"""Boot the Xperz ISO in QEMU and verify the serial kernel shell."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading


OVMF_PAIRS = (
    ("/usr/share/OVMF/OVMF_CODE_4M.fd", "/usr/share/OVMF/OVMF_VARS_4M.fd"),
    ("/usr/share/OVMF/OVMF_CODE.fd", "/usr/share/OVMF/OVMF_VARS.fd"),
    ("/usr/share/edk2/ovmf/OVMF_CODE.fd", "/usr/share/edk2/ovmf/OVMF_VARS.fd"),
)


def find_ovmf():
    code = os.environ.get("OVMF_CODE")
    variables = os.environ.get("OVMF_VARS")
    if code or variables:
        if not code or not variables:
            raise RuntimeError("Set both OVMF_CODE and OVMF_VARS to run the UEFI test.")
        if not Path(code).is_file() or not Path(variables).is_file():
            raise RuntimeError("OVMF_CODE and OVMF_VARS must name existing files.")
        return Path(code), Path(variables)

    for code, variables in OVMF_PAIRS:
        if Path(code).is_file() and Path(variables).is_file():
            return Path(code), Path(variables)
    return None


def run_case(label, command, build_dir):
    print(f"Testing {label} boot: {' '.join(map(str, command))}", flush=True)
    process = subprocess.Popen(
        list(map(str, command)),
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    output = []
    ready = threading.Event()
    responses_complete = threading.Event()
    required = (
        "XPERZ OS KERNEL READY",
        "Commands: HELP ABOUT MEM CLEAR HALT",
        "Xperz OS is an experimental x86_64 kernel foundation.",
        "Usable memory:",
    )

    def collect_output():
        for line in process.stdout:
            output.append(line.decode("utf-8", errors="replace"))
            log = "".join(output)
            if required[0] in log:
                ready.set()
            if all(marker in log for marker in required):
                responses_complete.set()

    reader = threading.Thread(target=collect_output, daemon=True)
    reader.start()
    try:
        booted = ready.wait(timeout=15)
        if booted:
            try:
                process.stdin.write(b"help\nabout\nmem\n")
                process.stdin.flush()
            except BrokenPipeError:
                pass
            responses_complete.wait(timeout=5)
    finally:
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        reader.join(timeout=2)

    log = "".join(output)
    missing = [marker for marker in required if marker not in log]
    if missing:
        log_path = build_dir / f"qemu-{label.lower().replace(' ', '-')}.log"
        log_path.write_text(log, encoding="utf-8")
        reason = "firmware did not reach the kernel" if not booted else "shell responses were incomplete"
        raise RuntimeError(
            f"{label} boot failed ({reason}); "
            f"missing {missing}. QEMU output saved to {log_path}."
        )
    print(f"{label} boot reached the Xperz kernel shell and passed.", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--require-uefi", action="store_true")
    args = parser.parse_args()

    if not args.iso.is_file():
        parser.error(f"ISO not found: {args.iso}")
    qemu = shutil.which(args.qemu)
    if qemu is None:
        parser.error(f"QEMU executable not found: {args.qemu}")
    build_dir = args.iso.parent

    bios_command = [
        qemu,
        "-machine", "q35",
        "-m", "512M",
        "-drive", f"file={args.iso},format=raw,if=ide,index=0",
        "-boot", "order=c",
        "-display", "none",
        "-monitor", "none",
        "-serial", "stdio",
    ]
    run_case("BIOS", bios_command, build_dir)

    ovmf = find_ovmf()
    if ovmf is None:
        if args.require_uefi:
            raise RuntimeError("OVMF is required for the UEFI boot test but was not found.")
        print("OVMF not found; UEFI boot test skipped. Install OVMF to test both paths.")
        return

    code, variables = ovmf
    variable_copy = build_dir / "OVMF_VARS.test.fd"
    shutil.copyfile(variables, variable_copy)
    uefi_command = [
        qemu,
        "-machine", "q35",
        "-m", "512M",
        "-drive", f"file={args.iso},format=raw,if=ide,index=0",
        "-boot", "order=c",
        "-display", "none",
        "-monitor", "none",
        "-serial", "stdio",
        "-drive", f"if=pflash,format=raw,unit=0,file={code},readonly=on",
        "-drive", f"if=pflash,format=raw,unit=1,file={variable_copy}",
    ]
    run_case("UEFI", uefi_command, build_dir)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError) as error:
        print(f"QEMU boot test failed: {error}", file=sys.stderr)
        sys.exit(1)
