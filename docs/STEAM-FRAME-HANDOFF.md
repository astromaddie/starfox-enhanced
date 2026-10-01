# Steam Frame native port handoff

## First build milestone

The first milestone adds a native Linux ARM64 OpenXR executable named
`starfox_steamframe`. It uses the existing desktop VR entry point, gamepad/audio
host, OpenXR loader, Vulkan renderer, and shared VR libraries. The existing
`starfox_pcvr` target and Android Quest path remain available.

`STARFOX_BUILD_STEAM_FRAME=ON` requires both `STARFOX_BUILD_VR=ON` and a
64-bit Linux ARM target. The standalone desktop VR configuration obtains
static SDL3 3.4.14 from the pinned upstream archive (SHA-256
`30d4aa2b3037718142b32dffd4e72f917ebb6cc5227150e7bb9c45efb2153aeb`). The
flat runtime keeps its existing SDL adapter and platform patch steps. Android
continues to use the SDL AAR/Prefab package.

The cross-build uses host Clang and LLD with the Steam Runtime Sniper ARM64
sysroot snapshot `3.0.20260415.224995`. The archive is fetched from
`https://repo.steampowered.com/steamrt-images-sniper/snapshots/3.0.20260415.224995/`
and checked against SHA-256
`8e162d235aeb1e6d283ab028e2c7b933061abc1d59830829c9e311cc73b3dd20`. CMake
searches target headers, libraries, packages, and pkg-config files inside that
sysroot; build tools and Python generators run on the host. The build script
first compiles the flat ARM64 baseline, then the Steam Frame player and the
OpenXR diagnostic, and stages only the `steamframe` install component.

Run `tools/build_steam_frame.sh [source-root] [build-root] [package-root]` on a
Linux host with CMake, Ninja, Clang/LLD, Python 3, curl, tar, sha256sum, and
readelf. The runtime package and hardware diagnostics are separate outputs.
`diagnostics/` contains the flat `starfox_pc` ABI baseline and the
`starfox_vr_runtime_check --loader-only` OpenXR diagnostic. Its ELF report
checks each executable's AArch64 identity and resolves the recursive dynamic
dependency closure against the pinned sysroot, while rejecting non-relocatable
RPATH/RUNPATH entries. Each artifact set gets `BUILD-METADATA.json` with source
revision/state, compiler/build-tool versions, pinned SDK/dependency identities,
and SHA-256 checksums of every staged payload file. The metadata does not hash
itself. Package validation rejects cartridge ROMs, generated BINs, music,
saves, and bundled Vulkan loader/driver files.

## Runtime storage and package

The executable and `LAUNCH-STEAM-FRAME.sh` can be run from a read-only install
directory and from any current working directory. The default bundle,
preferences, saves, and shader cache live under
`$XDG_DATA_HOME/StarFoxEnhanced`. If `XDG_DATA_HOME` is unset or relative, the
default is `$HOME/.local/share/StarFoxEnhanced`. `--bundle`, `--data-dir`, and
`--msu` remain launch overrides. PCVR retains its existing bundle-beside-player
and `vr-data`-beside-player defaults.

The package deliberately includes no ROM, generated `Starfox-Assets.BIN`,
private soundtrack, save data, or Vulkan driver. Prepare the BIN from the
owner's own supported retail ROM with the matching desktop `starfox_asset_builder`,
then copy it to the default XDG data directory or pass `--bundle PATH`. The
Frame system supplies its Vulkan runtime and driver.

## Acceptance still required

Source, packaging, and host regression checks do not establish Steam Frame
device compatibility. The first cross-build must pass in Linux CI, including
the AArch64 ELF and sysroot dependency-closure checks for the flat baseline,
VR executable, and OpenXR diagnostic. Device inventory confirmed the native
ARM64 runtime selector `SteamLinuxRuntime_4-arm64`, version
`4.0.20260805.254769`; the ordinary `SteamLinuxRuntime_sniper` wrapper is
x86-64. Keep the required Sniper ARM64 SDK snapshot as the build sysroot and
test backward compatibility on the Frame using the installed ARM64 runtime.
OpenXR discovery/session startup, stereo Vulkan rendering, head tracking,
controller mapping, audio output, read-only installation with writable XDG
storage, and sustained performance remain hardware acceptance gates.

## Local evidence (2026-10-01)

The native macOS VR configuration completed and built `starfox_pcvr` plus the
production desktop-path resolver tests. PCVR `--help`, Steam Frame CMake guard
checks, path defaults/overrides, and runtime/diagnostic package allowlist tests
passed (4/4 targeted CTest cases). The existing VR application test compiled,
but on this macOS host it aborted in the existing save-write failure case with
`std::__1::__fs::filesystem::filesystem_error: ... rename: Is a directory`.
The test expects to catch this rename error while `CartridgeSave::synchronize`
tries to replace a directory. The test and save implementation are unchanged
from starting revision `e88c2adc2cc5721af3d6e1991ca6cf1b91ae77e5`; the test
executable links Apple's system `libc++.1.dylib` and no new shared dependency.
Linux CI runs the same application test.

The ARM64 cross-build was not run locally: this host is macOS and does not
provide the Linux Clang/LLD build environment. The build script exits before
downloading the sysroot on non-Linux hosts. The hosted Linux CI job is the
required first cross-build evidence.

The CI run `36816034697` on source commit `9220065` passed Linux PCVR, desktop
path and VR application regressions; Quest AAR/Prefab packaging and its package
validator; and the existing Windows PCVR build/help smoke using MinGW-w64 GCC
15.2. ARM64 configuration was the sole failing job. CMake's `-pthread` probe
selected the SDK's static `libpthread.a` because the pinned sysroot's linker-name
symlink used an absolute target; LLD then reported unresolved glibc loader
internals. The archive also contains the matching shared `libpthread.so.0`.
The build now normalizes all 38 absolute symlinks found in the pinned SDK's
ARM64 linker roots to equivalent relative direct targets in the extracted
build sysroot, leaving the downloaded archive and checksum unchanged. The
checksum-verified archive inventory contained 335 absolute links overall;
each of the 38 scoped targets resolved within the SDK. The remaining links are
outside linker roots and are left as archived. The
normalization report and its hash are included with hardware diagnostics and
both artifact metadata files. The corrected ARM64 CI run remains required to
confirm CMake and ELF dependency-closure behavior.

The next ARM64 CI compile exposed a separate pinned-toolchain compatibility
issue: the checksum-verified Sniper SDK's `usr/include/c++/10/bit` contains no
`std::bit_cast` declaration or `__cpp_lib_bit_cast` feature macro, although the
project uses C++20 bit casts in state serialization and rendering. Production
call sites now use a constrained `starfox::bit_cast` adapter, selecting the
standard implementation when available and Clang's builtin with this older
libstdc++. A focused test forces the builtin path and checks size/trivial-copy
constraints, signed and floating-point bit patterns, and exact serialized
bytes. On the macOS host, both the standard and forced-builtin test variants
passed; `starfox_pcvr`, `starfox_vr_game_input_check`, and the compatibility
test target built, the PCVR help smoke passed, and the private Original and EX
full-state/audio progression checks passed. These host results do not verify
the Clang 18/GCC 10 ARM64 compilation; the next Linux CI run must confirm it.
