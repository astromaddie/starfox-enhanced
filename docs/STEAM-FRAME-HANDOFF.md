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

## Current acceptance gates

The source, package, and host regression checks establish a valid ARM64 build,
but device-specific input and sustained performance still require the Steam
Frame. The device uses the ARM64 `SteamLinuxRuntime_4-arm64` wrapper, version
`4.0.20260805.254769`; the ordinary `SteamLinuxRuntime_sniper` wrapper is
x86-64. The Sniper ARM64 SDK remains the pinned build sysroot, and the runtime
was launched on the Frame through its installed ARM64 runtime.

The first ARM64 cross-build succeeded in CI run `36818878449` from source
`35b41e3`. Its runtime and hardware diagnostic artifacts were staged with ELF
identity, dependency-closure, and metadata checks. A later complete four-job
workflow run `36822744519` on `64150c5` passed Linux host regressions, Sniper
ARM64 packaging, MinGW PCVR, and Quest AAR/Prefab checks. The verified runtime
archive SHA-256 was
`f097bfc0c5ea052ae543a410352b16effb69f71c9b0d652b635185f45a92876d`; the
separate diagnostic archive was
`90c77fdd3bdeb25cbe3d82d4c60e2a1f3d909bd81da7796bd945a33870e4eb4`.

Two pinned-SDK build repairs are part of that evidence. The verified Sniper
archive had 38 absolute symlinks in ARM64 linker roots; the build normalizes
those links only in its disposable extracted sysroot, leaving the downloaded
archive and checksum unchanged. The pinned GCC 10 C++ headers also lack
`std::bit_cast`; production now uses a constrained compatibility adapter that
selects the standard implementation when present and the compiler builtin for
that SDK. Both repairs passed the hosted ARM64 build and ELF checks.

On device, the user confirmed native flat menu navigation, audio, and gameplay.
A native XR session reached `FOCUSED` with two 1728x1728 eye swapchains under
SteamVR client 2.17.10; an official runtime capture was saved at 1920x1080
(SHA-256 `5c37e833686e1892dee74edaf480a6c1d20c59925a5f97806e15397abee305fd`).
The user confirmed VR menu navigation and gameplay, but did not confirm VR
audio. They reported confusing controls: A fired, B/X/Y all bombed, the bumpers
rolled, the left grip acted as Select, and physical Menu did not start. The user
intentionally exited that session through Steam. Asymmetric eye poses and
sustained performance remain unverified. These observations came from the older
Touch/Index-compatible bindings and do not verify the new Frame map.

The current controller change enables `/interaction_profiles/valve/frame_controller_valve`
only when `XR_VALVE_frame_controller_interaction` is advertised. It maps left
stick/D-pad to steering, right A/B/X/Y to fire/bomb/boost/brake, bumpers to
left/right roll, right Menu to Start, and left View to Select; existing
Simple, Touch, and Index bindings remain. Input arbitration selects one active
source per action, with desktop gamepad fallback for actions the OpenXR profile
does not activate. SDL menu/select/reset edges are synchronized during focus
loss so held controls are not replayed on resume. Existing face-button swap
preferences continue to apply. An `xrSyncActions` not-focused result also
suppresses desktop controls and pauses game/audio for that frame even while the
cached session event still says focused. The injected-runtime tests verify
exact profile paths, fallback-profile bindings, source arbitration, button
mapping, haptic binding availability, failure cleanup, and focus-resume edges.
Device retest of the Frame-specific bindings remains pending.

The shared OpenXR haptic API supports the Frame and existing fallback profiles;
it maps the authored dual-band amplitude to `max(low, high)` on the native
40 ms pulse and stops active outputs on focus loss and shutdown. The flat
cartridge rumble sequencer was extracted without changing its register updates
and its focused sequence tests pass. The independent 72/90/120 Hz flat/VR
rumble-and-audio parity work is not part of this controller milestone; VR
source-raster rumble advancement and device haptic playback remain pending. An
earlier independent Original focus run reported equal game state but a
serialized audio-state difference beginning at byte 65,584 and PCM divergence
at sample 67,385 at 72 Hz; output ports and logic/audio/raster counts matched.
A later rebuilt run with full APU-write tracing passed Original parity at
72/90/120 Hz, but the earlier discrepancy has no established cause and the
rumble-disabled counterfactual has not run. Keep the 72 Hz focus audio
discrepancy open; do not treat the later pass as a resolution or as evidence of
VR rumble integration.

The verified current host checks are `starfox_vr_input_check`,
`starfox_vr_runtime_tests`, `starfox_rumble_sequencer_tests`, the PCVR `--help`
smoke, and the Original and EX full-state/audio progression tests at 90 Hz.
The unverified rumble integration and parity reproducer are preserved outside
the checkout in the owner's private development recovery directory; they are
excluded from Git and packages. The macOS host cannot perform the Linux
Clang/LLD cross-build; hosted Linux CI provides that evidence. One earlier
macOS application save-failure fixture hit an Apple filesystem rename exception,
while the corresponding Linux CI application regression passed.
