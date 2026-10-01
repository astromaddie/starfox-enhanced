#!/usr/bin/env python3
"""Verify archived absolute linker symlinks resolve inside the SDK tree."""

from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile


def main() -> None:
    source = pathlib.Path(__file__).resolve().parents[1]
    inspector = source / "tools/inspect_steam_frame_sysroot.py"
    with tempfile.TemporaryDirectory(prefix="starfox-sysroot-inspection-") as temp:
        root = pathlib.Path(temp) / "sysroot"
        library_dir = root / "usr/lib/aarch64-linux-gnu"
        shared_dir = root / "lib/aarch64-linux-gnu"
        library_dir.mkdir(parents=True)
        shared_dir.mkdir(parents=True)
        (shared_dir / "libpthread.so.0").write_bytes(b"\x7fELF fixture")
        (library_dir / "libpthread.so").symlink_to(
            "/lib/aarch64-linux-gnu/libpthread.so.0"
        )
        (library_dir / "libpthread.a").write_bytes(b"!<arch>\n")
        output = pathlib.Path(temp) / "sysroot-report.txt"
        subprocess.run(
            [sys.executable, str(inspector), "--sysroot", str(root), "--output", str(output)],
            check=True,
            capture_output=True,
            text=True,
        )
        report = output.read_text(encoding="utf-8")
        expected = (
            "usr/lib/aarch64-linux-gnu/libpthread.so: symlink",
            "link text: /lib/aarch64-linux-gnu/libpthread.so.0",
            "target: lib/aarch64-linux-gnu/libpthread.so.0; exists=True",
            "target format: ELF",
            "usr/lib/aarch64-linux-gnu/libpthread.a: file",
            "target format: ar archive",
        )
        for fragment in expected:
            if fragment not in report:
                raise AssertionError(f"sysroot inspection omitted {fragment!r}:\n{report}")

    print("Steam Frame sysroot inspection resolves archived absolute library links.")


if __name__ == "__main__":
    main()
