#!/usr/bin/env python3
"""Record pthread linker names and targets from the pinned Frame sysroot."""

from __future__ import annotations

import argparse
import os
import pathlib
import stat


def resolve_in_sysroot(path: pathlib.Path, root: pathlib.Path) -> pathlib.Path | None:
    """Resolve links as if the extracted tree were a chroot, never the host."""
    pending = list(path.relative_to(root).parts)
    resolved: list[str] = []
    links = 0
    while pending:
        part = pending.pop(0)
        if part in ("", "."):
            continue
        if part == "..":
            if not resolved:
                return None
            resolved.pop()
            continue
        resolved.append(part)
        candidate = root.joinpath(*resolved)
        if not candidate.is_symlink():
            continue
        links += 1
        if links > 40:
            return None
        target = os.readlink(candidate)
        resolved.pop()
        if target.startswith("/"):
            resolved.clear()
            target_parts = pathlib.PurePosixPath(target).parts[1:]
        else:
            target_parts = pathlib.PurePosixPath(target).parts
        pending = list(target_parts) + pending
    return root.joinpath(*resolved)


def describe(path: pathlib.Path, root: pathlib.Path) -> list[str]:
    info = path.lstat()
    kind = "symlink" if stat.S_ISLNK(info.st_mode) else "file"
    lines = [f"  {path.relative_to(root)}: {kind}, size={info.st_size}"]
    if path.is_symlink():
        lines.append(f"    link text: {os.readlink(path)}")
    resolved = resolve_in_sysroot(path, root)
    if resolved is None:
        lines.append("    target: escapes sysroot or has a symlink loop")
        return lines
    lines.append(
        f"    target: {resolved.relative_to(root) if resolved.is_relative_to(root) else resolved}; "
        f"exists={resolved.exists()}"
    )
    if not resolved.is_file():
        return lines
    lines.append(f"    target size: {resolved.stat().st_size}")
    if resolved.stat().st_size <= 65536:
        data = resolved.read_bytes()
        if data.startswith(b"!<arch>\n"):
            lines.append("    target format: ar archive")
        elif data.startswith(b"\x7fELF"):
            lines.append("    target format: ELF")
        elif data and b"\0" not in data:
            lines.append("    linker text:")
            lines.extend(f"      {line}" for line in data.decode("utf-8", "replace").splitlines())
    return lines


def inspect(root: pathlib.Path) -> str:
    root = root.resolve(strict=True)
    directories = [
        root / "lib/aarch64-linux-gnu",
        root / "usr/lib/aarch64-linux-gnu",
        root / "usr/lib64",
        root / "lib64",
    ]
    for base in (
        root / "lib/gcc/aarch64-linux-gnu",
        root / "usr/lib/gcc/aarch64-linux-gnu",
        root / "usr/lib/gcc-cross/aarch64-linux-gnu",
    ):
        if base.is_dir():
            directories.extend(path for path in base.iterdir() if path.is_dir())

    lines = [f"sysroot: {root}", "pthread linker-name inventory:"]
    seen: set[pathlib.Path] = set()
    for directory in directories:
        if not directory.is_dir():
            continue
        for path in sorted(directory.glob("libpthread*")):
            if path in seen:
                continue
            seen.add(path)
            lines.extend(describe(path, root))
    if not seen:
        lines.append("  no libpthread files found in the expected target library directories")
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sysroot", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    report = inspect(args.sysroot)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(report, encoding="utf-8")
    print(report, end="")


if __name__ == "__main__":
    main()
