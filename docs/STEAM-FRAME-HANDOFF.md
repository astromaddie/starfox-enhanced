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

The newer `1bd5713` source passed all four hosted CI jobs, and the runtime
package upload/hash validation passed. The wearer reported that it runs, with a
large opaque black border taking up roughly half of the view; that presentation
defect is assigned separately. Device logs for PID 100569 record successful
Frame binding loads and later `FOCUSED` transitions. Subsequent captures taken
while the runtime reported standby are black and do not show the
reported HUD defect. Exact button, audio, and menu behavior still require
separate wearer confirmation. Current evidence
is in `/tmp/starfox-frame-run-1bd5713` and
`/tmp/starfox-frame-device-evidence.json`.

The current controller change enables `/interaction_profiles/valve/frame_controller_valve`
only when `XR_VALVE_frame_controller_interaction` is advertised. It maps left
stick/D-pad to steering, right A/B/X/Y to brake/bomb/fire/boost, bumpers to
left/right roll, right Menu to Start, and left View to Select; physical A/Menu
also confirms host menus. Existing
Simple, Touch, and Index bindings remain. Input arbitration selects one active
source per action, with desktop gamepad fallback for actions the OpenXR profile
does not activate. SDL menu/select/reset edges are synchronized during focus
loss so held controls are not replayed on resume. Existing face-button swap
preferences continue to apply. An `xrSyncActions` not-focused result also
suppresses desktop controls and pauses game/audio for that frame even while the
cached session event still says focused. The injected-runtime tests verify
exact profile paths, fallback-profile bindings, source arbitration, button
mapping, haptic binding availability, failure cleanup, and focus-resume edges.
The wearer tested the dedicated profile on `db2b7fb` and confirmed the buttons
worked, but its A-fire/X-boost/Y-brake map disagreed with the on-screen
control settings. Their advertised mapping was A brake, B bomb, X shoot,
Y boost. The correction above follows that report; its hardware retest is
pending. The Frame SDL fallback matches it, while other profiles and PCVR
fallback are unchanged. Physical A/Menu still confirms the host menu through
a separate confirmation level; gameplay A is not aliased to fire. Cartridge
C_TYPE and host face-swap behavior remain unchanged.

The shared OpenXR haptic API supports the Frame and existing fallback profiles;
it maps the authored dual-band amplitude to `max(low, high)` on the native
40 ms pulse and stops active outputs on focus loss and shutdown. The extracted
flat cartridge rumble sequencer is now advanced by the shared VR frame driver
once immediately after each 60 Hz source raster is presented and before its
possible logic tick. Duplicate XR eye submissions do not advance it. Live VR
advances the cartridge registers only for Original with the game's rumble
setting enabled, available authored rumble symbols, and an eligible output
sink. A usable OpenXR haptic binding is selected over SDL gamepad rumble;
never both are triggered for one effect. Without a sink the sequencer does not
mutate cartridge registers. Focus loss, paused/menu state, session exit, and
shutdown stop active outputs. EX does not advance authored rumble sequences.

The independent parity executable compares 120 native source rasters with the
production VR frame driver at 72, 90, and 120 Hz. It compares complete game
state, APU write traces, audio ports, serialized audio state, PCM, and rumble
effects/cadence, including duplicate-eye retries, changing per-eye matrices
through production `eye_camera()`, focus pause/resume, and a no-output-sink
case. The held steer/fire/boost/roll scenario goes through production
`VrGameInput` and is compared with an independently latched flat SNES-button
input. On the host, Original passed with rumble enabled (21 active authored
samples) and disabled (0); EX passed with rumble enabled (0). The
Original/EX game-input regressions also passed with the new source-raster
ordering check. The earlier independent Original focus run reported equal
game state but a serialized audio-state difference beginning at byte 65,584
and PCM divergence at sample 67,385 at 72 Hz; output ports and logic/audio/
raster counts matched. A later controlled run using one instrumented binary
with rumble enabled and disabled passed both settings twice, but the earlier
72 Hz focus discrepancy remains unexplained and unreproduced under that
instrumentation. The new parity matrix passes; it does not establish the cause
of that historical discrepancy or device haptic playback. Private regression
inputs and paired-control evidence remain outside the repository.

The runtime package now installs a root `vrpreferences.json` with the project-
selected 2160-resolution, minimum-90-Hz, no-half-framerate, motion-smoothing-
off defaults expressed through Valve's documented per-app schema ([Valve
documentation](https://partner.steamgames.com/doc/steamhardware/steamframe/vrpreferences)).
These are requested defaults, not evidence that SteamVR applied them.
Package allowlist/contents validation passes locally; actual Frame package
installation and SteamVR settings verification still require the next hosted
build and a later manual device check. No device configuration or SteamVR
restart was changed for this source check.

The verified current host checks are `starfox_vr_input_check`,
`starfox_vr_runtime_tests`, `starfox_rumble_sequencer_tests`, the PCVR `--help`
smoke, the Original and EX game-input regressions, and the Original/EX
flat-versus-VR parity matrix described above. The matrix and regression logs
were saved under `/tmp/starfox-frame-rumble-*` for this run. The macOS host
cannot perform the Linux
Clang/LLD cross-build; hosted Linux CI provides that evidence. One earlier
macOS application save-failure fixture hit an Apple filesystem rename exception,
while the corresponding Linux CI application regression passed.


## Approved Layout A presentation milestone (source committed; device acceptance pending)

Source base is `1bd5713a04828c6c3a9543019cf27d8412afa715`. The shared
presentation change is committed. No cartridge bytes,
generated bundle, screenshots, or private regression inputs are in this diff.
SDL/OpenXR/Vulkan pins and the native Vulkan backend are unchanged.

Preferences v6 append presentation fields to the v5 prefix. Versions 1–5
restore the existing camera, 100% world scale, 100% head translation, and zero
cockpit calibration. The menu exposes existing/cockpit camera, bounded world
scale, 0/50/100/150/200% head translation, cockpit XYZ offsets in centimetres,
and recenter. Cockpit calibration stays in physical centimetres when world
scale changes. Translation changes the anchored head centre only, preserving
IPD and raw compositor poses/FOV. Recenter is applied once at the next stereo
frame boundary; a pending right eye cannot observe a different origin.

Cockpit presentation uses the active authored player reference independently
of whether the ship is visible. Regular planet/space pilot strategies move the
view origin to that reference plus calibration; scripted strategies preserve
the source camera. Only the obstructing player packet is hidden in pilot mode.
No isolated COCKPIT mesh is inserted and no cartridge camera flags are written.
The earlier rainbow mesh inventory specimen was never runtime cockpit evidence.
Source game timing, source input/aim registers, and default camera behavior are
retained. Exact seat fit, aiming appearance, scripted transitions, and headset
comfort still require wearer acceptance; host matrix tests are not that proof.

Startup/runtime menus, source pause, map, and briefing presentation use an
independently owned 1024x896 mono OpenXR quad swapchain alongside the stereo
projection. Its world-locked, level pose starts at 1.75 m and its width is
1.15 m. Eye poses and FOV are runtime supplied. Image acquisition, wait, GPU
fence completion, release, and layer submission have distinct ownership; a
pending quad does not rerender the eyes or advance simulation. The shared
Vulkan target abstraction supports a single image list for this mono pass.
Startup source glyphs, approved panel/focus treatment, and two-row Exit
confirmation are rendered by the shared production packet builder.

Layout A retains original/EX sprite and portrait pixels with nearest sampling.
The compact dialogue group ends before the SHIELD/instrument row; each group
moves as a unit. The 256x224 OAM composition and inner FX meter origin (16,16)
are retained before offsets. Original and EX live-state GPU readbacks show
separated portrait, dialogue, SHIELD, bombs, and boost bars. Source reticle and
warning sprites stay out of HUD relocation. In cockpit mode instruments use
the authored ship orientation/reference. The desktop main menu also exposes
EXIT and reuses its existing NO/YES confirmation, with the opening press
latched so holding A cannot immediately dismiss it. Same-frame native before/after
captures established that the flat fallback previously dimmed and overpainted
the card with source menu rows; the existing card now composes after source
setup/style passes. The native GPU confirmation implementation is unchanged.

Verification of this source state: shared PCVR and native macOS desktop builds;
`starfox_vr_input_check`, `starfox_vr_presentation_check`, and
`starfox_vr_camera_check`, `starfox_vr_runtime_tests`, and
`starfox_rumble_sequencer_tests` pass. Coverage includes v1–v5 migration, v6 calibration,
Frame/fallback bindings and separate physical confirmation, source HUD offsets,
non-unit head translation with preserved IPD, recenter across pending eyes,
and projection-plus-quad ownership through wait/cancel/release. Native GPU
readbacks use the actual shared production packet builders, mono depth target,
draw packets, command fence, and readback on Apple M4/MoltenVK. The external
render-only harness enables macOS Vulkan portability; it does not claim a
successful full scene regression or a headset render. The actual desktop EXIT / NO / reopen / YES sequence also exits successfully;
the frame-201 before/after pair verifies the readable confirmation above menu
rows. Approved asset and implementation evidence remain private under the task visualization directory.

The broader macOS exception-path checks are not reported passed: existing
swapchain/session invalid-input tests and packet input-validation throws enter
Apple crash handling on this host. The focused positive-path presentation
checks and GPU readbacks are separate evidence. Linux/ARM64/Windows/Quest
builds (the Linux workflow now explicitly includes the affected asset-free
presentation/input/camera/swapchain/session/runtime/rumble regressions),
actual compositor quads, pause/map/briefing presentation, cockpit seat
fit, and revised controller mapping require the next immutable CI/device run.

The earlier recorded hardware baseline was `db2b7fb`, selected 72 Hz with two
1728x1728 eye swapchains. The runtime recommendation is 2160x2160 at 90 Hz;
90 Hz is neither configured nor measured. The baseline native capture shows
clear SHIELD/bombs without dialogue, while the older real dialogue capture
established the portrait overlap addressed here. CPU log samples are not
per-frame GPU/compositor performance evidence; no acceptance CSV/criteria
report exists. The source presentation milestone makes no performance claim.

## Gameplay HUD backing correction (7a47bd4 base)

The wearer reported that the working `1bd5713` HUD had a black border occupying
much of the viewing area. Its native readback and source path establish an
added 254x78 dark panel across the gameplay HUD, including unused dialogue
space. The ordinary packet pipeline draws that geometry opaquely; its stored
95% alpha does not make it transparent. Fresh headset captures while the HMD
was in standby were black and are not evidence of HUD appearance.

The gameplay packet builder now omits only that decorative backing and border.
Native portrait frames, text shadows, meter/bar artwork, and approved group
positions remain unchanged. Separate menu and Exit panels remain unchanged;
no blending pipeline, backend, dependency pin, control mapping, v6 preference,
or rumble behavior changes are part of this correction.

Private before/after native Vulkan readbacks are retained in the task's
`frame-ui/hud-backing-fix` evidence directory. They draw the actual Original
and EX source HUD packets over the same colored diagnostic checkerboard,
through the production packet pipeline, mono target, GPU fence and readback.
The checkerboard is explicitly a visibility diagnostic, not a cartridge world
or headset capture. It makes restored unoccupied areas visible; source-art
pixels outside the removed backing/border remain identical, and the menu
readback is identical. The asset-free presentation regression also exercises
the actual builder with inactive source artwork and rejects added geometry.

The native scene checker, presentation check, and input check build; the two
focused checks pass, as does the diff whitespace check. Wearer acceptance of
the corrected gameplay HUD remains pending the next immutable Frame build,
which must retain the committed `7a47bd4` rumble/default-preference work.
No new headset, performance, or 90 Hz acceptance is claimed.

The immutable `68cda35` package subsequently passed all four hosted CI jobs,
was uploaded with all ten public package hashes and seven private-file hashes
verified, and launched through the official Devkit client. XR reached
`FOCUSED` with two 2016x2016 views and successful Frame binding loads. Its
capture shows the VR presentation settings panel; the gameplay HUD and
authored haptics still await wearer confirmation. Runtime logs mention the
90 Hz preference, which does not establish actual refresh or performance.
The preserved v6 settings have existing camera, 1x world/head translation,
and zero cockpit offsets. Evidence is in `/tmp/starfox-frame-run-68cda35`
and `/tmp/starfox-frame-device-evidence.json`.

## Interface stereo-depth correction (68cda35 base)

The wearer identified source startup splash, controls, gameplay pause, and
in-game dialogue as having ships appear closer than artwork covering them.
Controlled Original renders confirm a stereo-distance versus draw-order
conflict. Title ships span 0.719–1.439 m against 2 m artwork; controls ships
span 0.832–1.223 m against 2 m artwork. Including the existing runtime's 0.25 m
model offset, the first Corneria player spans 0.875–1.344 m against the prior
1.75 m HUD, and the paused player spans 0.867–1.336 m. These corrected gameplay
numbers supersede the initial `depth-investigation` diagnostic that omitted
that host offset; title/controls measurements were unaffected.

Title/splash and both source controls states now use the existing whole-scene
interface quad at 1.75 m, width 1.15 m. Their animated menu-preview models and
source artwork share a single raster depth. This deliberately makes those menu
previews flat; gameplay world geometry remains stereoscopic. Compact gameplay
HUD/dialogue and pause/runtime host overlays use a separate initial 0.75 m
distance, width 1.15 * 0.75 / 1.75 m, retaining their previous angular size and
source component positions. Initial startup/map/briefing panels retain 1.75 m.
The anchor recaptures when the panel distance changes; ordinary head motion
within the same family does not move the retained world anchor. Source aiming
sprites/reticles/warnings retain their original 2 m placement. Camera/world
scale, pilot calibration, raw runtime eye views, v6 preferences, input and
rumble behavior are unchanged. The 0.75 m value is not comfort-calibrated.

Private evidence is in `frame-ui/depth-correction`: paired two-eye native
Vulkan readbacks for title, controls, dialogue and pause, plus active pilot
views. They use actual Original simulation states, production packet assembly,
EyeCamera and source rasterization. The whole-interface raster is reprojected
through textured packets to model its submitted quad geometry; this is not a
live compositor or headset capture. Legacy completed-tick geometry is used,
with actual host model offset/pilot transforms; ancillary sky is omitted and
native clear colour retained. Original pause is a frozen world/HUD fixture,
not EX pause-text coverage. Six before/after comparison boards were inspected.

Default-camera samples at ticks 362/382/442 put the player nearest vertices at
0.875/0.809/0.816 m; sampled active-pilot visible models are at least 1.504 m
away. One other model at tick 442 reaches 0.677 m below the HUD, after dialogue
has ended. Therefore the new distance addresses the reported player/interface
case, not every possible enemy, shadow, head movement or clipping case. It is
not a universal foreground-occlusion guarantee. No tighter depth was introduced.

The PCVR, native scene-check, presentation, input and camera targets build;
focused presentation/input/camera checks pass. Regressions cover interface
classification, angular-size invariance, unchanged source aiming plane and
far-to-near/near-to-far anchor transitions. The native HUD specimen checker
undoes the new overlay transform to preserve its exact source-pixel readback.
Hardware compositor appearance, comfort/readability at 0.75 m, and actual
wearer default/pilot acceptance remain pending. No performance claim is made.

## Frame baseline and optional profiling output (915774e base)

On the immutable `68cda35` device build, the wearer confirmed that the corrected
gameplay HUD no longer has the large black backing, Original authored rumble
responds to boost and destruction, and the Frame Menu plus View buttons return
to Steam. Exact process cleanup semantics were not established. Separate
runtime telemetry on the later `915774e` run recorded 4320x2160 at 90 Hz, while
the application recommended 2016x2016 eye views. The wearer accepted the
corrected depth on `915774e`; a tower-logo flicker was also reported for renderer
follow-up. Comfort and sustained performance remain pending. These
observations are separate from the profile implementation and do not establish
a frame-time target.

The desktop OpenXR player accepts `--profile-csv FILE` and optional
`--profile-frames N` (default 120, range 1..1000000), enough for a 30-minute
capture at 90 Hz (162000 submitted frames). For example, from a writable
development directory on Frame:

```
./starfox_steamframe --profile-csv "$HOME/frame-90hz.csv" --profile-frames 162000
```

The CSV begins with comment metadata identifying source revision/tree state
(captured at CMake configure time, so reconfigure after source changes),
OpenXR runtime and version, Vulkan device/driver/API, selected queue family,
timestamp valid bits/period, and per-eye/UI timestamp capability. Each row
represents one completed stereo submission and includes host cadence, game
logic/model/upload/layer CPU durations, per-eye CPU submit-to-fence time,
separate per-eye Vulkan GPU timestamp duration when available, a UI command
timing when recorded, and the existing scene/object/sprite upload and reuse
counters. Empty GPU cells mean unavailable; CPU submit-to-fence is explicitly
not GPU time. Query pools are reused and results are read after the existing
fence completion without a query wait. No source clock, simulation, audio, or
rumble behavior is changed by profiling. Compare cadence against the active
90 Hz mode and the roughly 11.11 ms display interval; use the recorded
per-eye GPU durations for GPU-work analysis and CPU columns for host work.

Timestamp support is reported unavailable when the selected queue has zero
timestamp-valid bits, invalid period metadata, missing query entry points, or
query-pool/readback failure. A bounded real Vulkan query/readback regression
is a required Linux host workflow test pinned to the installed Mesa Lavapipe
ICD (`llvmpipe`), so missing native query support fails that job instead of
being reported as skipped. The local macOS math and CLI checks passed; the
default local Vulkan loader path reports unavailable. Two earlier MoltenVK
query attempts and a later Vulkan-device mock check became uninterruptible
macOS processes (PIDs 83385, 86099, and 90309); no further local native or
exception-path probes were run. The 90309 device check did not complete. The
query result and unsupported-device path therefore remain a Linux CI gate.
No profile CSV, Frame performance baseline, optimization, or performance
acceptance is claimed yet.

The first profiling workflow (`36850010195`, source `1e5fa21`) failed in the
Linux compile because the timestamp command header used `std::uint32_t` and
`std::uint64_t` without directly including `<cstdint>`. The follow-up adds that
include; native query readback and all hosted checks still require the rerun.
