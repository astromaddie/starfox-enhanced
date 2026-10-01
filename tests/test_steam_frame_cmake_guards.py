#!/usr/bin/env python3
"""Prove Steam Frame configuration rejects missing VR and non-ARM targets."""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import tempfile


def configure_rejected(
    cmake: str,
    generator: str,
    source: pathlib.Path,
    build: pathlib.Path,
    expected: str,
    vr: bool,
    extra: list[str] | None = None,
) -> None:
    command = [
        cmake,
        "-S",
        str(source),
        "-B",
        str(build),
        "-G",
        generator,
        "-DSTARFOX_BUILD_STEAM_FRAME=ON",
        f"-DSTARFOX_BUILD_VR={'ON' if vr else 'OFF'}",
        "-DSTARFOX_BUILD_RUNTIME=OFF",
        "-DSTARFOX_BUILD_TESTS=OFF",
    ]
    command.extend(extra or [])
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    output = result.stdout + result.stderr
    if result.returncode == 0:
        raise AssertionError("invalid Steam Frame CMake configuration unexpectedly succeeded")
    if expected not in output:
        raise AssertionError(
            f"configuration failed without the expected guard {expected!r}:\n{output}"
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--generator", required=True)
    args = parser.parse_args()

    source = pathlib.Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="starfox-steam-frame-guards-") as temp:
        root = pathlib.Path(temp)
        configure_rejected(
            args.cmake,
            args.generator,
            source,
            root / "without-vr",
            "STARFOX_BUILD_STEAM_FRAME requires STARFOX_BUILD_VR=ON",
            vr=False,
        )
        configure_rejected(
            args.cmake,
            args.generator,
            source,
            root / "host-architecture",
            "STARFOX_BUILD_STEAM_FRAME requires a ",
            vr=True,
            extra=["-DCMAKE_SYSTEM_PROCESSOR=x86_64"],
        )

    print("Steam Frame CMake guards rejected VR-off and host-architecture builds.")


if __name__ == "__main__":
    main()
