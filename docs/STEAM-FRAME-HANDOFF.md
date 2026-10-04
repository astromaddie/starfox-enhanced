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
responds to boost and destruction, and the wearer exited to Steam. This was
recorded as Menu plus View returning to Steam, which is wrong for this port:
Menu plus View opens the port's own runtime menu (see "System layer" below),
and the exit route actually used was not established. Exact process cleanup
semantics were not established either. Separate
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

The follow-up `71d67e5` workflow (`36850395405`) passed all four jobs: Linux
shared regressions, ARM64 packaging, Windows PCVR, and Quest AAR/Prefab. Linux
selected `/usr/share/vulkan/icd.d/lvp_icd.json` and passed the required native
timestamp-query test within its 18/18 checks. Runtime artifact `11156030722`
and diagnostic artifact `11154934777` matched their GitHub archive digests;
immutable package/checksum/allowlist validation and AArch64 ELF inspection
passed. This verifies host query readback, not Frame GPU performance. No new
package was deployed after the restart; its USB network connection is absent.

## Tower logo coplanar inset correction (after 71d67e5)

The reported tower is consistent with authored `BU_7`: a flared base, tall
rectangular wall, two top marker lines, and an inset textured sign. Original
face 7 is a solid wall at source Z=40 (X=-20..20, Y=-118..-10); face 8 is the
textured sign on that same plane (X=-15..15, Y=-113..-83). The old native decal
classification required identical vertex sets. The actual Original production
packet therefore emitted six textured vertices with zero decal flags, and the
resident path emitted face 8 with flags=1, without its decal bit.

Both paths now share a local-geometry containment check: an inset textured
polygon must be coplanar with and contained by a convex solid polygon, with a
common transform in the resident representation. Exact vertex-set overlays
remain supported, including duplicated vertex records. The added containment
rule rejects noncoplanar, protruding, disjoint, degenerate and concave backing
cases; fragmented
faces remain excluded. Ordinary textures receive no general depth bias. The
existing shader depth adjustment is unchanged. After the correction, actual
Original `BU_7` emits six textured vertices with six decal flags; resident face
8 has flags=17. This closes the demonstrated classification gap; it does not
yet establish that all wearer-observed flicker has been eliminated.

Local CPU validation: `starfox_vr_decal_check` and `starfox_vr_mesh_check` pass
(2/2 CTest), and `starfox_pcvr` builds. The decal executable also passes with
private Original and EX ROM/symbol inputs: real `BU_7` through CPU packets and
clipped/unclipped resident preparation, displaced and protruding logo negative
cases, and explosion exclusion. Asset-free tests cover identical faces, inset
quads/triangles, winding, tilted planes, scale, edge containment, separated and
disjoint surfaces, concavity and different resident transforms. Steam Frame's
Linux host workflow now builds/runs these checks plus the existing broader
packet check, since both shared packet-preparation paths changed. Hosted
results are pending. The local broader packet check aborts at the pre-existing
`Invalid axis upload region` exception case both before and after rebuilding;
it is not reported as passing, and its assertions remain intact.

Private evidence remains under `frame-ui/tower-flicker` in the October 1
visualization workspace: `packet-before`, `packet-after.cpp`, `packet-after`,
`verification.txt`, and `bu7-front-software.png`. The last is an inspected
source-software specimen showing the winged logo, wall and flared base; it is
identification evidence, not native depth/readback or headset flicker proof.
Its camera uses source angle units (65536 per turn); the older
`bu7-software.png` used a nearly zero yaw and is not useful match evidence.
No private ROM, symbol data, captures or artwork enter the repository/package.

The Mac restart cleared the earlier `/tmp` native captures, checkpoints and
run logs. The wearer's positive depth report on `915774e` remains a reported
observation, but those temporary image/provenance paths are no longer retained.
The previously hanging macOS Vulkan/MoltenVK/query/negative-exception probes
were not retried. Native rendered depth/readback, still-head/pause behavior,
and wearer acceptance of this tower correction remain pending. No performance
improvement or frame-time acceptance is claimed.

## Optional ship rotation in Cockpit mode (after b3e4de8)

The wearer confirmed the tower logo is stable on native Frame build `b3e4de8`
and reported intentionally exiting to Steam. That is hardware acceptance of
the tower correction; the exit is not a crash report. The existing cockpit
view hides the player's ship and has no modeled 3D cockpit interior. An
interior remains separate work requiring the mockup/assets approval process.

VR Presentation now includes `FOLLOW SHIP ROTATION: OFF/ON`, using the existing
scrolling rows. It defaults OFF, persists in v7 preferences (27 bytes), and
only affects an active Cockpit pilot view. All v1–v6 preference formats still
load; the new value defaults OFF when migrating, including calibrated v6
settings. Both the preference parser and application file-size guard accept
v7. Turning Cockpit off retains the stored rotation choice without applying it.

ON rotates the presented world into the authored ship frame after translation
to the calibrated pilot position. The inverse uses the actual composed Q15
ship/source-view basis, accounting for its small quantization errors. Physical
head orientation, translation and stereo eye separation are composed afterward;
raw OpenXR compositor eye poses are untouched. The matching inverse cancels
ship rotation in the existing HUD/instrument transform, so those instruments
remain stable in the ship frame and still respond to head movement. Physical
centimetre calibration remains independent of world scale. OFF retains the
prior camera and rotating-instrument behavior. Scripted/nonpilot flows retain
the existing camera; missing/replaced pilots and camera discontinuities use the
current endpoint rather than blending across unrelated states.

The live application already supplies the common presentation camera to stars,
background/terrain, tunnel surround, model/projectile packets, sprites, bomb
geometry and ray-view construction. This correction uses that shared path;
no gameplay, source aiming coordinates, source clocks, audio, inputs, artwork,
backend or dependency pins changed.

Validation: the PCVR player builds, and presentation/input/camera CTest checks
pass (3/3). Regressions cover pitch/yaw/bank direction, actual object
interpolation plus model-matrix composition with a rotated source camera,
calibrated pivot at 1x/2x world scale, independent physical head yaw/translation
and IPD, interpolation/reset gates, inactive/script/training flows, toggle
interaction, v1–v6 migration and v7 validation. Existing mocked compositor
pose/FOV checks remain intact. Hosted CI and wearer verification of this new
option remain pending; no comfort or performance acceptance is claimed.

Private evidence: `frame-ui/cockpit-rotation` in the October 1 visualization
workspace contains `capture.cpp`, `verification.txt`, `menu-off.png`,
`menu-on.png`, `menu-bottom.png`, `projection.json`, and
`controlled-bank-comparison.png`. Menu images rasterize the actual production
menu packets and cartridge glyph masks on the CPU; the row fits and scrolling
retains recenter/back. The bank comparison uses actual Original tick-420 world
and HUD packets with a controlled additional 30-degree pilot bank and the
production presentation/eye/model matrices. OFF has a level world and banked
HUD; ON has the inversely banked world and level HUD. It is a CPU wireframe
projection (22 world packets, 2 HUD packets, no pending model issues), not a
native shader, full background, compositor or headset capture. No known hanging
macOS Vulkan probes were retried, and no private art or captures are packaged.

## Cockpit follow steering and motion diagnosis (after 5967962)

Build `5967962` passed hosted release checks, was uploaded and ran focused on
Steam Frame. The wearer confirmed ship-follow rotation works, but reported
rough/uncomfortable movement and steering that becomes unusable when banked
sideways. The wearer explicitly authorized view-relative steering for this
option. The proposed 3D cockpit asset package was rejected; no interior assets
or construction are approved or included here. A visible cockpit remains
separate pending work.

When Cockpit and Follow Ship Rotation are both active, directional input now
adapts once at the native logic-tick boundary. The adapter projects the native
world X/Y movement plane through the same authored source-camera/ship basis as
the presented world, then selects the closest native eight-way direction.
At a quarter turn, physical stick-left therefore produces displayed leftward
movement. Native `C_TYPE` vertical inversion remains relative to the displayed
axes. Both SDL and OpenXR use this common path; face-button swaps, roll buttons,
menu navigation, Existing camera, and rotation OFF retain their prior paths.
The existing input threshold/latch runs before adaptation, preserving low
analog magnitudes and quick taps without a second deadzone. An exactly edge-on
movement plane has no unique two-axis mapping and retains native directions.

The basis is the latest completed source tick, refreshed after every game tick
inside a batched frame advance. It contains no HMD pose, presentation alpha or
XR prediction time. No source frequency, gameplay movement algorithm, audio,
rumble or camera interpolation changes are made. The adapter changes only the
logical directional controls of the opted-in cockpit view.

Local validation builds the PCVR player and passes presentation, input, camera
and the new cockpit-input CTest checks (4/4). The new check also passes against
both private Original and EX cartridges: actual source movement at 0/±90/180
bank with native vertical inversion; all eight directions, low/full analog
magnitudes, deadzones, pitch/yaw and rotated source view; latched taps and held
bank transitions; inactive and menu bypasses. Identical raster-keyed control
streams at 72/90/120 Hz and 100 ms batches produce identical complete native
save states in both native Original timing and unlocked 20 Hz timing. An
independent source-raster run derives the expected native tick count from
`logic_tick_ready()`, rather than assuming a fixed logic rate. For both
cartridges, this 72-raster-phase case yields 18 native-paced ticks or 24
unlocked ticks, with every tick changing the ship basis and 24 audio blocks
in either mode. Recording the adapted logical inputs and replaying them
through the flat simulation produces the identical complete state in both
timing modes. Hosted CI now includes
the asset-free cockpit check; cartridge tests register only with private local
inputs configured.

A controlled 90 Hz CPU trajectory compared the existing `5967962` camera with
the current build using identical logical source input, real Original gameplay,
SPC output, unlocked 20 Hz source timing, 900 presentation samples and 199
source ticks. This controlled trace does not use the wearer's native Original
pace. The CSV trajectories
are byte-identical. No camera discontinuity resets occurred. The camera basis
determinant spans 0.999913–1.001004, with column lengths 0.999895–1.000403;
there is no observed matrix collapse or amplified scale jump. During the
source barrel roll, consecutive presentation increments are approximately
13.75 degrees (maximum 13.751843 degrees per frame, about 1238 degrees/second).
This demonstrates continuous interpolation of very fast authored rotation in
this controlled 20 Hz case, not the rotation speed at native source pacing,
headset frame pacing or comfort. No arbitrary
smoothing was added. The reported roughness remains unverified on hardware;
Frame profile/compositor cadence and wearer verification are still required.

Private evidence is retained under `frame-ui/cockpit-controls` in the October 1
visualization workspace: source input probes for Original/EX, `trajectory.cpp`,
`trajectory-before`, `trajectory-after`, `baseline.csv`, `current.csv`, and
`trajectory-comparison.json`. These traces deliberately keep logical input
identical to isolate the camera; they do not simulate the new steering's
changed gameplay path. No private ROMs, symbols or artwork enter Git or the
package. No known hanging macOS Vulkan probes were retried.

## Steering delivery and cockpit design review (October 1)

Steering source `06f9065613db63dbfe16018912139ffa954c0d00` passed all four jobs
in workflow `36863282913`: ARM64 packaging, Linux, Windows PCVR and Quest.
Linux passed 22/22 CTests, including the required native Lavapipe timestamp
query. Runtime artifact `11162918004` matches SHA-256
`f35b7384f0e3d64a88c5f93f829b1dbf13b8797db0881b83b0045e0f038a2669`.
The actual user bundle contains `C_TYPE` for Original and EX; production bundle
decoding, symbol parsing, simulation and scene-history capture/tick succeeded
for both. This positive symbol check does not independently replace embedded
manifest compatibility checks.

Official Devkit upload preserved all seven current private files and verified
all ten runtime files against the package. The pre-upload checkpoint is
`build/frame-devkit/checkpoints/preupload-06f9065-20261001T125432Z`; delivery
evidence is under `build/frame-devkit/artifacts/06f9065`. The device reported
SteamOS build `20260922.6101926`, gamescope and native ARM64 runtime settings.
No game process remained before or after upload; no new launch was performed.
The temporary next-launch arguments request `06f9065-profile.csv` in device
XDG storage and 5,400 submitted stereo frames. Wearer/Valve recorder readiness
is pending. Restore ordinary unlimited launch arguments after the capture.
Neither actual 90 Hz delivery nor the reported roughness is accepted yet.

The wearer selected open-canopy concept A, then rejected its prepared 3D asset
package. Those assets are not approved and were not implemented. Revised
private concepts compare newly shaped interior B with source COCKPIT geometry
C, proposed materials and rear enclosure panels. Both display source HUD art
and an authored nose, with a proposed larger ship presentation reference;
physical fit and pose remain provisional. The source COCKPIT descriptor decodes
to 83 vertices/66 faces; its intended live material state and native placement
have not been established. Neither revised concept nor its final assets are
approved. No cockpit geometry or extracted Nintendo assets entered Git.

## Cockpit C asset preparation (October 1)

The wearer selected concept C first. This selects the source COCKPIT design;
final asset approval is still pending under the pasted UI working agreement.
The private `cockpit-mockups/asset-package-c/asset-review.png` now presents the
seated, lean, left, down and assembly views. Its manifest records provenance,
materials, placement and file hashes. All 122 decoded source front triangles
remain, with proposed silver/blue-grey flat materials. Seven new rear solids
add 84 triangles; editable OBJ and packet geometry agree, with closed,
consistently wound solids. The source instruments have no geometry occlusion
in the tested preview views, and the conservative head box has no triangle
overlap. These are desktop preparation checks, not headset comfort or native
renderer evidence. Original source topology was checked; EX remains open.

The proposed pilot reference is `(0, 0.28, 1.4)` metres in a 12x presentation
ship reference, with the authored forebody visible ahead. This proposal must
be implemented as a coherent presentation rig; gameplay/world scale remains
unchanged. The source instrument face is approximately 1.25 metres ahead.
Physical fit, binocular readability and calibration remain provisional.
Private extracted source geometry and HUD pixels must not enter Git or public
packages. Runtime source geometry must come from the user's asset bundle.
No cockpit runtime construction has started.

While preparing C, the official Devkit restored ordinary unlimited launcher
arguments to `['LAUNCH-STEAM-FRAME.sh']`. Native runtime settings and all ten
installed runtime files still match source `06f9065`; no executable payload
was replaced and no launch was performed. Evidence is in
`build/frame-devkit/artifacts/06f9065/launch-settings-restoration.json` and
`official-devkit-normal-argv-restore.log`. The earlier temporary profile
arguments are no longer active. Steering wearer verification and the motion
profile/Valve capture remain pending.

## Approved Cockpit C runtime implementation (October 1, source reviewed)

The wearer approved the prepared C package with “Yeah let's try it.” This
supersedes the pending-approval state above; the rejected earlier package
remains excluded. The shared VR runtime now constructs C in active Cockpit
view. Existing camera remains the default, preferences retain their current
format, and Follow ship rotation/source-tick steering are unchanged.

The front is decoded from the user's runtime bundle, preserving all 122
triangles and applying the approved flat, two-sided face materials. Actual
Original and EX bundles have identical ordered COCKPIT positions and face
ranges despite different descriptor addresses and some palette colours. A
geometry signature and per-face range validation reject incompatible topology
before applying the material map. Only the seven newly authored rear solids
(84 triangles), new materials and their generator enter Git. No extracted
cartridge mesh, pixel, ROM, symbol dump or generated bundle is added.

The live player's source forebody replaces the former zero-scale hidden ship.
Its source-local geometry is clipped at pilot Z = -0.065 metres, then placed
in the approved 12x presentation ship reference. The base pilot seat is
(0, 0.28, 1.4) metres; saved user-origin offsets are additive. Cabin, nose and
instruments share the existing ship-frame presentation transform. World scale,
source reticle rays, gameplay, source timing and physical HMD/eye poses remain
unchanged. Front/rear caches have 32 brightness/colour-space slots per live
cartridge; changing nose/effect geometry is rebuilt without an accumulating
cache. Only the player and exact native FLASHPLAYER strategy use CPU geometry
in this view; other resident model indices and packets remain intact.

The wearer requested the health-ring wireframe as a calibration reference.
The native player flash is a separate FLASHPLAYER_STRAT object, already
anchored to the player's authored pose by source interpolation. Cockpit view
now places its complete source geometry and materials in the same 12x ship
and seat reference as the nose, retaining native blink and lifetime. It is
not clipped to the forebody, and other effects are not generalized into this
path. A controlled fixture seeds the existing FLASHPLAYER_ISTRAT initializer
and then lets native ticks drive the effect. Both bundled cartridges produce
9 visible and 11 hidden phases. Packet checks register it to the actual source
camera/player pose and the nose under Follow ON/OFF, saved origin offsets,
and both tested world scales. This demonstrates the native effect's alignment;
a natural health-ring pickup and its exact caller remain wearer verification.

Original Layout A retains the approved mount. EX's actual single-player band
extends from source X=6 to 196 rather than fitting the approved 141-pixel-wide
band. A fixed uniform 141/190 fit centres it on the same face without changing
source art or aspect ratio. Full/empty health, zero/max bombs and inactive,
two-line and three-line communications fit the face with the identical mount
on every frame. This makes EX lettering physically smaller (about 2.26 mm per
source pixel versus Original's 3.05 mm); binocular readability is still a
hardware gate. Existing Layout A boss-health and EX multiplayer top-row
placements are preserved; they are outside this approved single-player panel
fit and have not been redesigned. Both cartridge checks verify that enabling
a boss meter retains its geometry and packet ordering through the mount, with
no band clipping; its native headset visibility/readability remains open.
Mounted HUD packets test cabin/world depth
but do not write depth, preserving coplanar source portrait/glyph layering.
World and ordinary overlay depth defaults retain their prior behaviour.

Local verification before the implementation commit (base a670f4e):

- PCVR and the affected geometry, presentation, input and pipeline test targets
  build successfully in build/host-vr.
- Seven focused CTests pass: presentation, cockpit geometry, cockpit input,
  OpenXR input, eye camera, generated asset freshness, and the positive cockpit
  depth-policy check. The existing complete Linux pipeline gate is retained.
- The actual Starfox-Assets.BIN passes the production geometry path for both
  Original and EX: 122 front + 84 rear + 7 clipped live-nose triangles in the
  captured first-active-pilot scenes, unchanged complete simulation state,
  unchanged other object geometry, valid resident indices, preserved HUD art,
  fixed HUD bounds and the seeded native repair alignment described above.
- Private production-packet dumps and CPU raster projections are retained in
  frame-ui/cockpit-c-runtime in the October 1 visualization workspace. Views
  include original/ex seated, lean, look-left, look-down and repair with Follow
  ON/OFF. The renderer consumes production packet geometry, matrices and HUD
  texels, uses a neutral background and labels its CPU-only status. Root
  inspected the cabin/nose against approved C; the refreshed EX look-down view
  demonstrates the repaired panel fit. These are not native GPU readbacks.

No known hanging macOS native Vulkan probes were retried. The broad mocked
pipeline test still encounters the previously observed macOS negative-path
exception abort (“Flush vertex memory: -5”); the isolated positive depth-policy
check passes. Linux CI must execute the complete required checks and GPU path.
Native headset rendering, natural ring pickup, seated fit, occlusion and stereo
readability remain open. Coordinator source and rendered-packet review passed;
ARM64 packaging and device launch follow the implementation commit.

### Window framing correction before delivery

After inspecting the repair-overlay projection, the wearer requested a much
wider, lower V window. The source COCKPIT topology is established for both
cartridges, but its native canopy-to-ship placement is not established by the
specimen decode. Nose/repair registration alone does not establish canopy fit.
The following adjustment is an explicitly provisional presentation calibration,
not a claim about the original game's native placement.

The upper shell widens by 70% and moves down 0.16 metres. A continuous blend
is zero at Y=-0.72 metres (the instrument-face top) and reaches full strength
at Y=-0.48 metres. The same coordinate mapping applies to source front and new
rear solids, retaining shared connections; topology, triangle counts and
materials do not change. The source-derived forward window lip moves from
X=±0.27,Y=-0.39,Z=-2.03 to X=±0.459,Y=-0.55,Z=-2.03 metres. Instrument face,
HUD mount, seat, nose, complete repair effect and world scale stay fixed.

Both actual bundled cartridge tests pass the new lip/pinned-panel assertions,
nondegenerate front/rear triangles, previous head/follow/calibration checks,
HUD bounds and native repair registration. PCVR builds and all seven focused
CTest checks still pass. Private before-framing/ images and eight packet dumps
retain the prior state; comparison-original/ex-{seated,lean,look-left,look-down,
repair-follow,repair-existing-rotation}.png provide matched before/after views.
framing-comparison.json confirms identical nose/effect/world/HUD packets and
all rig matrices across the change, with unchanged front/rear materials and
triangle counts. These remain CPU projections of production packets. Native
GPU rendering, canopy fit and comfort still require device verification.

## Cockpit C ARM64 delivery (October 1)

Source `99712f52f66917c08c323c3f74d5dc1800aec3bf` was committed and pushed
after coordinator diff, asset-provenance and rendered-packet review. Workflow
`36873671425` passed all four jobs: ARM64, Linux, Windows PCVR and Quest.
Linux passed 25/25 CTests, including the required native timestamp query
(0.03 seconds) using `/usr/share/vulkan/icd.d/lvp_icd.json`.

Runtime artifact `11167918958` has archive SHA-256
`bde8c87cf4439eeddfc2ba43f44b70a8b8cd64b854210a35b44f3592001aaf95`.
Diagnostics artifact `11167784185` has archive SHA-256
`ae6def4ff5eed94d5218b38dc006279067f53ab13ab4b1fc0d47999ecb8f9fde`.
Both matched GitHub digests and passed immutable package validation. Both
metadata records identify the clean exact source and pinned dependencies.
The ARM64 ELF SHA-256 is
`c444a60232e5cd3b02277430d2cf55c817a7be326f141073f168aec518f4d658`.

Fresh official Devkit status reported SteamOS `20260922.6101926`, gamescope,
and no Star Fox process. The checkpoint
`build/frame-devkit/checkpoints/preupload-99712f5-20261001T141913Z/`
contains all seven current private files (10,246,136 bytes), verified against
device hashes. Safe native upload preserved extras and settings, with checksum
verification, no Steam restart, and ordinary unlimited launcher arguments.
Native settings remain `steam_play=0` and `SteamLinuxRuntime_4-arm64`.
Postchecks verified all ten runtime files (185,619,761 bytes) and all seven
private files by size and SHA-256. No game launch occurred during upload.

Delivery evidence is under `build/frame-devkit/artifacts/99712f5/`:
`ci-evidence.json`, `official-devkit-upload.log`, and
`post-upload-verification.json`. The uploaded cockpit is awaiting wearer
readiness for launch. Natural health-ring alignment, binocular canopy fit,
readability, steering comfort, motion/performance capture and the broader
hardware acceptance gates remain open.

## Cockpit world scale (October 2)

Wearer report on Cockpit C: the world felt too small. Cause: the cabin
encloses the live ship enlarged 12x (`cockpit_ship_scale`), but the world
stayed at 256 units/m. The cabin's ship was therefore 12x the size of the
in-world ship, and the eye sat 1.4 source metres behind the real ship.

`presentation_scene_matrix` now uses `cockpit_world_scale()` while the pilot
view is active: 12x the `world_scale` setting. The enlargement is centred on
the pilot eye, so mono composition, aiming rays and angular sizes are
unchanged. Stereo depth, head translation and parallax now treat the world as
pilot-sized. At the default 1x setting, the native ship, its shots and the
scenery register on the cabin's nose. Outside the cockpit (chase view, cutscenes,
menus), the scale is unchanged. Cockpit steering divides out the enlargement,
so direction selection and the edge-on rejection threshold are unchanged.

`vr_cockpit_geometry_tests` now requires that the native ship drawn through the
world matrix lands within 5 mm of the cabin's nose at 1x. Reverting the scale
fails that check. Presentation, cockpit-input and cockpit-geometry checks pass
on macOS host builds for Original and EX. The test ROMs were built in /tmp from
the user's retail ROM.

Headset-unverified. Check: overall world size, reticle depth (world sprites
now sit 12x further away), comfort of near-miss scenery and canopy fit.

## System layer, menus and haptics strength (October 2, standard v0.1)

Adopts the cross-port Steam Frame system layer. **Headset acceptance pending**:
everything below is covered only by injected-runtime unit tests; no device was
available and nothing here has been worn.

**Controller system layer (Frame names; the same code serves Touch/Index, where
the left grip is the Select input).**

| Input | Action |
| --- | --- |
| R Menu | Source Start/pause, unchanged |
| L View, short press (under 1 s) | Source Select; in menus: back. Reported on release, so Select now arrives a press later than before |
| L View, hold 1 s | Recentre yaw and horizontal position, keeping standing height. 0.6 / 80 ms buzz on both hands |
| L View, hold 3 s | Recentre and recalibrate height. A second buzz |
| Menu + View, hold both 0.5 s | Opens this port's runtime menu |

Menu + View does **not** return to Steam: that earlier statement in this file
was wrong. The SNES pause cannot be extended with a "VR settings" item, so the
runtime menu this chord opens is the port's VR settings (existing/cockpit
camera, presentation, haptics strength, recentre, and the other options).
A View press that overlaps a Menu press belongs to the chord and never also
taps or recentres. A hold that has recentred is never also a Select tap.
Controls held through focus loss or at launch are ignored until released.

Hold timing is the shared `sfvr` library's `sfvr_view` (vendored in
`third_party/sfvr`, commit in `VERSION`). `include/starfox/vr/system_layer.hpp`
(`SystemLayer`) is a thin adapter over it that adds only the Menu + View chord
and the rule that a chord press never also taps or recentres. `OpenXrInput::poll` and the desktop
gamepad fallback (`DesktopControlEdges`) both use it. Recentre requests apply at
the next stereo frame boundary through `StereoRenderer::request_recenter`, like
the menu RECENTER row (which still recalibrates height as well). The 1 s hold
passes through `PositionAnchor::reset(heading, keep_height)`; a system recentre
(`XrEventDataReferenceSpaceChangePending`) is unchanged. The desktop fallback has
no haptic buzz.

**Menus.** Physical B (the bomb button on every profile, read before the face
swap setting) goes back on every menu page, in addition to the BACK rows and the
View short press. On the runtime main page it resumes; on the pre-game page it
does nothing. B must be released once after a menu opens, like confirmation.

**Quit to Steam.** The main page's last row is now QUIT TO STEAM, with a
confirm page ("QUIT TO STEAM?", NO / BACK, YES / QUIT TO STEAM). Confirming
calls `xrRequestExitSession`, keeps pumping events until the runtime's STOPPING
(`xrEndSession`) and EXITING arrive, then leaves; a runtime that never answers
is abandoned after 2 s. New labels have no translations yet and show in English.

**Reset game.** The four-input reset chord (both bumpers and both stick clicks)
is removed; those inputs are plain game actions again. A runtime-menu-only
RESET GAME row (main page, between RESUME and QUIT TO STEAM) with a
NO / BACK, YES / RESET GAME confirm rebuilds the game at INTROMAP and returns to
the start menu. Saves are kept, as before. Quest and PC players who used the
chord now use this row.

**Haptics strength.** OPTIONS > HAPTICS STRENGTH cycles 0-100% in 10% steps
(default 60%). It scales all OpenXR haptic output (authored rumble and the
system buzz); 0% silences it. Desktop gamepad rumble is not scaled. All OpenXR
haptic output goes through one `sfvr_haptic_queue`: rumble is queued with
`sfvr_haptic_rumble`, the system buzz as `SFVR_HAPTIC_SYSTEM`, and
`OpenXrInput::flush_haptics()` sends the coalesced result (strongest amplitude,
longest duration, per hand) once per frame, scaled by the setting. Preferences
are version 8, 28 bytes: the v7 record plus one byte, percent 0-100. Versions
1-7 migrate unchanged with 60%, and a v8 byte above 100 rejects the record, like
the other fields. The standard's `haptics` key maps to this byte; the env
override is below.

**Unchanged on purpose.** Fire/bomb/boost/brake/roll mappings. Adding trigger
aliases for fire is a pending user decision.

**Tests.** `starfox_vr_input_check` (hold timing through the fake OpenXR
runtime with injected time, chord, B back on every page, removed chord, reset
row, haptic scaling, v7 migration), `starfox_vr_session_check`
(`xrRequestExitSession` sequence) and `starfox_vr_camera_check` (recentre keeps
height). Open: on-device check of the buzz feel, hold timing, Menu + View
discoverability, the Select-on-release latency, and the clean Quit to Steam exit.

## sfvr adoption: perf line, env overrides, refresh rate (October 2)

**Headset acceptance pending.** Covered by unit tests and host builds only.
`third_party/sfvr` is the vendored shared C99 library (`VERSION` holds its
commit; never edit it here). The View timing and haptic queue above use it.

**`[vr-perf]` line.** Every 10 s the application prints one standard line to
stdout, formatted by `sfvr_perf` (`include/starfox/vr/perf_log.hpp`), for
example `[vr-perf] fps=89.9 missed=2 cpu=6.10ms logic=1.20 model=2.40 upload=1.30
layer=1.20 eye=2.21/2.19ms gpu=n/a`. It is added at each submitted stereo frame,
beside `--profile-csv`, which is unchanged. The stages are the existing
per-frame profile timings (logic, model assembly, upload, layer composition);
`eye` is each eye's CPU submit-to-fence milliseconds; `missed` counts frames
whose display time jumped more than 1.5 runtime display periods. Unmeasured
values count as zero. `gpu` is the left plus right eye GPU timestamp
milliseconds, or `n/a` when timestamps are off (see below). The CPU timings are
now always measured, not only with `--profile-csv`.

**Env overrides.** The standard form `SFX_VR_<KEY>` (name built with
`sfvr_settings_env_name("SFX", key)`, canonical snake_case keys from the sfvr
registry) is read at startup by `include/starfox/vr/env_overrides.hpp`. An
unset, empty or unparseable variable is ignored; numbers clamp to the registry
range. An override wins over the saved preference and is never written to
`vr-preferences.bin`. Implemented keys:

| Variable | Meaning |
| --- | --- |
| `SFX_VR_HAPTICS` | Haptics strength 0..1 (default from the saved setting, 0.6). The menu row shows `NN% ENV` while it is set, and changing it there has no effect |
| `SFX_VR_TIMING_GPU` | `1`/`true`/`on` enables GPU timestamp queries, which fills `gpu=` in `[vr-perf]`; default off (also on with `--profile-csv`) |
| `SFX_VR_REFRESH_RATE` | Target display refresh in Hz, default 90 (added with the refresh-rate request) |
| `SFX_VR_RESOLUTION_SCALE` | Diagnostic: eye buffers at 0.5–1 of the runtime's recommended size (default 1), to measure how GPU time follows pixel count |
| `SFX_VR_FORCE_RENDER` | `1` renders and submits every frame even when the runtime says not to (headset off, standby) and runs the game while unfocused, with empty controls. Real views when valid, else a synthetic head at the LOCAL origin (63 mm IPD, ±50° x ±48°). Default off. Added October 4 |
| `SFX_VR_DIAG_YAW` | Degrees (-360..360) to turn the rendered head about +Y, positive to the left, for stereo checks at 0/90/180°. Default 0. Added October 4 |
| `SFX_VR_AUTOSTART` | A level name such as `LEVEL1_1` (any case): skips the startup menu and starts that level as the level-select cheat would. An unknown name is logged and the menu shows. Not a registry key. Added October 4 |
| `SFX_VR_EXIT_AFTER` | Seconds after the first in-game frame; then quits through the QUIT TO STEAM path. Not a registry key. Added October 4 |
| `SFX_VR_OVERLAP_EYES` | `1` submits eye 1 straight after eye 0 instead of waiting for eye 0's fence (see "Overlapped eye submission" below). Default off, which keeps the serial eye loop. Not a registry key. Added October 4 |

No other registry key is implemented by this port, so no other variable has any
effect.

**Display refresh rate.** When `XR_FB_display_refresh_rate` is advertised the
runtime enables it, and once the session is running the application
(`include/starfox/vr/refresh_rate.hpp`) enumerates the offered rates and
requests the highest one at or below the target: 90 Hz by default, or
`SFX_VR_REFRESH_RATE` (72-144). If nothing offered is at or below the target it
takes the lowest offered rate. The log line is `[vr] display refresh offered: ...`.
If the focused frame rate stays under 90% of the current rate for two
consecutive 10 s windows, it requests 72 Hz once and stays there for the
session; a window with any unfocused frame resets the streak, and there is no
automatic return to 90. The current rate is re-read from the runtime at each
window end. This is in addition to the `vrpreferences.json` rate, which is
unchanged. Without the extension nothing is requested and the log says so.
Untested on a device: the Steam Frame runtime's offered rates and whether it
honours the request are not known.

## Complete cockpit hull (October 3)

Wearer report: the nose looked disconnected and seemed to point at the pilot.
No face was missing. The forebody kept only source geometry ahead of
z = -0.065 m: an open-backed, three-sided pyramid 2.2–5.2 m ahead of the seat.
From the seat, its near open base set the outline, and stereo read that as a
point aimed back at the pilot.

Cockpit view now draws the complete live player (20 triangles) through
`cockpit_ship_packet`, the same 12x ship/seat rig as the repair flash. The
nose runs continuously into the fuselage below the window, and the wing
blades and wings are visible beside the pilot. The seat is unchanged. The
fuselage ends 0.93 m ahead of the eye, so looking straight back still shows
mainly the cabin's rear modules; only the wingtips reach behind the seat.
Showing the tail and wings behind needs a seat further forward, plus a hull
cut-out where the cabin sits. That is a design change, pending a mockup.

Cartridge tests (Original and EX) check that the hull keeps every source
triangle and that it extends from the nose to behind the seat and past both
sides. They also check that sampled hull surfaces stay more than 0.35 m from
the eye, and that no hull triangle lies between the eye and the mounted
instrument face. Headset-unverified.

## Canopy seat (October 3)

The wearer chose the canopy-seat mockup
(`build/cockpit-mockups/seat-options-2026-10-03.png`, bottom row) over the
original seat. The seat moves 1.4 m forward to sit over the ship origin, so
the wing blades sweep overhead and the wingtips are visible behind.
`cockpit_seat_m` is now (0, 0.48, 0). That is 0.2 m higher than the mockup:
at the mockup height the eye sat on the canopy peak and saw the nose ridge
end-on, which reproduced the "pointing at me" outline. From 0.2 m above, the
nose rises from behind the window frame and is seen from above.

`cockpit_ship_packet` removes hull inside two pilot-space cut-outs
(`cockpit_hull_cutouts`): the cabin tub (|x| ≤ 1.12, y ≤ -0.28, z from -2.1 to
0.62) and the canopy around the head (|x| ≤ 0.5, y ≥ -0.28, z from -1.0 to
0.62). Triangles are clipped with interpolated attributes, zero-area pieces
on shared cut planes are dropped, and lines are trimmed. The repair flash
uses the same cut. The world camera follows the seat through
`presentation_scene_matrix`.

Cartridge tests (Original and EX) check that the live hull matches
`cockpit_ship_packet`, spans from more than 3.5 m ahead to more than 1.5 m
behind and past both sides, keeps 0.35 m head clearance, and never covers the
instrument face. They also check that the native ship still registers with
the rig and that the repair flash lies outside the cut-outs with source
colours. Production-packet CPU views:
`build/cockpit-mockups/canopy-seat-production-2026-10-03.png`. Headset-unverified.

### Canopy seat delivery (October 3)

Workflow `37085349660` passed for `656aff4`. Its `StarFoxEnhanced-steam-frame-arm64`
artifact was uploaded over Wi-Fi (192.168.1.31; the USB link was down; the host key
matched the recorded Frame key) with rsync, without deletion and excluding AppleDouble
files. All package files match the artifact by SHA-256 (`starfox_steamframe`
`e722b9fa…2937`). The device's `BUILD-METADATA.json` names a clean `656aff4`.
The game was not running, and no launch was performed. The cartridge assets and
`starfox-ex.srm` are unchanged, along with `pregame.cfg`,
`~/.local/share/StarFoxEnhanced`, and the Steam argv/settings JSON. The previous `e86cc07`
runtime, metadata, launcher and vrpreferences are kept in `~/devkit-game/_StarFoxEnhanced_prev/`.

## Cockpit motion smoothing (October 3)

Wearer report: turning felt jerky. A 90 Hz probe of LEVEL1_1 through the real
frame driver (steer left, steer right, steer with roll) found linear interpolation
working as designed. The jerk is in the source motion. Velocity changes in a step
every 20 Hz tick, and the source camera tilts ±8.4° at a constant ~60°/s with
instant starts and stops. One tick in six seconds moved 1.84x the normal distance.
The wearer's saved preferences had Follow ship rotation ON, which adds the ship's
own bank and barrel rolls (up to 94° at ~1,170°/s).

The wearer chose smoothing only; roll and Follow ship rotation are unchanged. In
pilot view, `presentation_scene_matrix` and `presentation_instrument_matrix` now
take older/previous/current snapshots (`GameSceneHistory::older()`). The pilot
position, ship rotation, source camera and view follow a uniform quadratic
B-spline through the last three ticks (de Boor over the existing Q15
interpolation). Position and velocity are continuous across ticks, half a tick
(25 ms) behind the linear path. A cut before `previous` restarts there; a cut
before `current` jumps, as before. Scene content stays linear; the scene matrix
carries a rigid correction from the linear to the smoothed source camera. The
world, sprites, shadows and cabin therefore share one smoothed pose. Other views
are unchanged. Things spawned at the ship, such as lasers, can appear up to
~1.5 m ahead of the cabin nose at full speed (half a tick of forward motion).

Probe result: world acceleration seen from the cockpit (Follow OFF) drops from a
5,340 m/s² peak (RMS 370–620) to 1,190–1,730 (RMS 200–330). Angular-acceleration
spikes drop 3–5x. With Follow ON, spikes drop about 3x; roll rates are unchanged.
`vr_presentation_tests` checks, through the real content path, that the smoothed
view equals the linear path half a tick later under steady motion (including a
16-bit wrap), that the cabin matches, that position is continuous across ticks
and velocity is continuous to within a tenth of the linear jump, and that cuts and
non-cockpit views are handled. Headset-unverified.

### Follow ship rotation ease (October 3)

The wearer asked for turn smoothing in Follow ship rotation too. Tick smoothing
only rounds the corners at ticks; the ship's own banks and rolls still reached
the view at full rate. `CockpitFollowEase` now eases the attitude Follow turns the
world by. It runs once per display frame from XR display time, as a quaternion
slerp toward the B-spline source attitude with a 0.1 s time constant. It snaps on
cuts, on the first frame and after gaps over 0.25 s, and never trails by more
than 90° so fast barrel rolls keep their direction. `cockpit_follow_attitude`
supplies the target; `presentation_scene_matrix` uses the eased attitude only for
the follow rotation. The seat offset, cabin (fixed in Follow) and steering mapping
(evaluated per source tick) stay on the source attitude, so during a fast roll
the steering axes can differ from the displayed ones by up to the ease lag.

Probe (same turn as above, Follow ON): peak world rotation rate drops from
1,173 to 353°/s, angular-acceleration peaks from 24,000–96,000 to 2,700–7,700°/s²,
and linear-acceleration peaks about 11x versus unsmoothed. Tests cover the
round trip, the rate, settling, cut and pause snapping, the 90° cap, and that
Follow OFF ignores the ease. Headset-unverified.

### Smoothing delivery (October 3)

Workflow `37090303671` passed for `eafbc1c` (tick smoothing `2af5ea8` plus the
Follow ease). Its ARM64 package was uploaded over Wi-Fi (192.168.1.31) the same
way as the canopy-seat delivery. All package files match the artifact by SHA-256
(`starfox_steamframe` `0cb5c64a…01b2`). The device metadata names a clean
`eafbc1c`, no AppleDouble files were added, and the game was not running and was
not launched. Assets, save, `pregame.cfg`, preferences and the Steam argv/settings
are unchanged. The `656aff4` runtime is kept in `~/devkit-game/_StarFoxEnhanced_prev/`.

## Ending Arwing at 24x with a rear bulkhead (October 3)

Wearer report: no rear of the ship, and the cabin looked open behind the seat.
The in-flight ship (`MYSHIP_4`, 20 faces) is complete: every LOD points to it,
and its body ends just behind the canopy. The bigger problem was scale. At 12x
the Arwing was about 4.7 m long and the cabin was wider than its wingspan, so the
cabin walls hid the wings. The cartridge holds the cutscene Arwing used in the
intro and ending (`MY_DEMOS`, 48 faces / 56 triangles, with a canopy and a body
behind it; `HIPOLYARWING` is 0 and assembles nothing else). The wearer chose 24x.

- `cockpit_ship_scale` is 24 (an Arwing of about 9.4 m); the world scales with it.
  The seat is (0, 0.95, 0.75) m: in the cutscene ship's canopy, about 0.3 m
  above its top.
- In the cockpit rig, `SourceModels` draws `MY_DEMOS` for the player and the repair
  flash when their live shape is one of `MYSHIP_4/L/R/B`. It keeps the live pose,
  palette, hit-flash colour table and shading. The chase view keeps the in-flight
  ship on the GPU source path. The flash's blink shapes are untouched (9 visible /
  11 hidden phases, as before).
- `MY_DEMOS` has no damage variants. When the live ship is narrower than
  `MYSHIP_4` (a lost wing), `cockpit_ship_packet` trims the hull to the live x extent.
- Three new authored boxes (`rear_bulkhead_lower/upper/rim`, 36 triangles) close
  the cabin behind the seat at z 0.60–0.68 m, from the floor to about 0.3 m below
  the eye, flaring with the side walls. The G-diffusers and wings stay visible
  above and beside them.
- `subtract_box` leaves polygons that cannot touch a cut-out whole.

Cartridge tests (Original and EX): the cockpit hull is the 56-triangle cutscene
ship and the chase view stays on the compute path; a `MYSHIP_L` ship trims the
hull's +X edge to 2.34 m while −X stays past 3 m; head clearance, instrument
visibility, repair-flash placement and registration still pass, with 120 rear
triangles. Production CPU views:
`build/cockpit-mockups/ending-arwing-24x-production-2026-10-03.png`. Headset-unverified.

### Rear roll hoop (October 3)

Wearer request: the window frame should physically connect to the rear bulkhead.
The flared upper struts of the source frame end in free space at about (±2.04,
0.86, 0.32) m. Three new 6 cm beams (`rear_hoop_left/right/bar`, 36 triangles,
silver frame materials) are authored in the unflared rear-module space, where
runtime flaring is affine. Two posts rise from the bulkhead rim's top corners
(flared ±1.12, -0.29, 0.63) to the strut tips, and a crossbar joins the tips above
and behind the head. The rig test checks that both joints are within 8 cm and
that every rear vertex is at least 0.5 m from the eye and behind z = -1.2 m.
The rear packet is 156 triangles. Production CPU views:
`build/cockpit-mockups/roll-hoop-production-2026-10-03.png`. Headset-unverified.

### Ending Arwing and roll hoop delivery (October 3)

Workflow `37095130428` passed for `1d68555` (ending Arwing at 24x, rear bulkhead,
roll hoop). The ARM64 package was uploaded over Wi-Fi (192.168.1.31) as in the
earlier deliveries. All package files match by SHA-256 (`starfox_steamframe`
`bf7b909b…8da1`). The device metadata names a clean `1d68555`, no AppleDouble
files were added, and the game was not running and was not launched. Assets,
save, `pregame.cfg`, preferences and the Steam argv/settings are unchanged. The
`eafbc1c` runtime is kept in `~/devkit-game/_StarFoxEnhanced_prev/`.

## Refresh rate setting (October 3)

Wearer question: does the Frame's 120 Hz system setting apply? It did not. The
port requested the highest offered rate at or below 90 Hz, and the compositor log
for 13:15 shows "Trying to match desired rate of 90" from offered 72, 80, 90, 96,
108, 120 and 144 Hz.

VR PRESENTATION now has REFRESH RATE: 90 HZ (default), 120 HZ or SYSTEM, saved as
preferences v9 (byte 28; v1–v8 load as 90 Hz). `SFX_VR_REFRESH_RATE` still
overrides it and shows as "<n> HZ ENV". The application requests again whenever
the choice changes. SYSTEM calls `RefreshRate::release()`, which stops requests
and the governor; a rate already requested in this session cannot be withdrawn
through XR_FB_display_refresh_rate, so SYSTEM fully applies from the next launch.
The packaged `vrpreferences.json` minimum of 90 Hz is unchanged.

The governor now steps down one offered rate at a time after two low focused
10 s windows (for example 120 → 108 → 96 → 90 → 80 → 72), restarting its window
after each request. It reports `fell_back()` once at the 72 Hz floor. Refresh
decisions and every `[vr-perf]` line (now with `display=<Hz>`) also go to
`~/.local/share/StarFoxEnhanced/vr-session.log`, rewritten each launch, because
stdout is not kept on the Frame. Tests cover the menu cycle, persistence,
v8 migration, the ENV label, 90 → 80 → 72 and 120 → 108 steps, release, and the
floor. 120 Hz performance is unmeasured.

### Refresh setting delivery (October 3)

Workflow `37097198587` passed for `1b5cb83`. The package was uploaded over
Wi-Fi as before. All files match by SHA-256 (`starfox_steamframe`
`52f03745…47bf`). The device metadata names a clean `1b5cb83`, the game was not
running and was not launched, and user data and Steam settings are unchanged.
The `1d68555` runtime is kept in `~/devkit-game/_StarFoxEnhanced_prev/`. Saved
v8 preferences load as REFRESH RATE 90 HZ.

## Opening credits text (October 4)

Wearer report: the opening credits text rolled with head tilt. That text is the
attract intro's scaled-text objects (flags 0x40, up to 12 at once). Their glyphs
were billboards: the shader sized them (bit 27) and then added the corners in each
eye's view space (bit 2), so they followed head roll. The shader's game-plane
option (`group_b.z == 1`) only covered non-glyph sprites. `text_packet` now
computes the same size on the CPU (`trunc(size*256/depth) * depth/256`, no
sprite cap) and places each glyph as a flat quad in the game camera's plane, with
flags 1024 | sRGB only. No shader change was needed. A cartridge test (Original
and EX) boots TITLEMAP into the intro and checks every glyph for the flat quad,
the cleared billboard and sizing bits, and the GPU side length; it fails with the
old billboarded glyphs. Headset-unverified.

### Credits text delivery (October 4)

Workflow `37131842021` passed for `b7bcd63`. The package was uploaded over Wi-Fi
as before. All files match by SHA-256 (`starfox_steamframe` `a77cefbe…e4c4`). The
device metadata names a clean `b7bcd63`, the game was not running and was not
launched, and user data and Steam settings are unchanged. The `1b5cb83` runtime
is kept in `~/devkit-game/_StarFoxEnhanced_prev/`.

## Unattended diagnostics (October 4)

Goal: measure frame time at 90 and 120 Hz with nobody wearing the Frame. Off
the head, SteamVR reports `shouldRender=false` and the session is not FOCUSED,
so the port skipped every frame and paused the game. Four env overrides (table
above) change that, and all of them are off unless set. Unset, the code paths
are the same as before.

- `SFX_VR_FORCE_RENDER=1`: `OpenXrSession::begin_frame` still locates views when
  the runtime does not want the frame, never throws for that, and falls back to
  `synthetic_stereo_views()` when they are invalid. Such frames are marked
  `StereoFrame::forced` and submitted with their projection and panel layers.
  The OpenXR spec lets an app submit layers when `shouldRender` is false; the
  runtime may just not show them. If the runtime refuses the layers anyway,
  the session ends that frame empty, keeps going, and logs `[vr] runtime refused
  forced layers` once. The game counts as focused for simulation and menus, so
  the 20 Hz game runs and audio plays, with empty controls. A worn headset
  still uses real views and real input.
- `SFX_VR_DIAG_YAW=<deg>` turns only the scene cameras, about the eyes' midpoint
  and after the position anchor. The submitted layer keeps the real pose, so
  the compositor shows the turned view straight ahead. The menu panel is not
  turned.
- `SFX_VR_AUTOSTART=LEVEL1_1` picks that level from the cartridge's level list
  (the same list as the CHEATS level row) and closes the startup menu on the
  first frame, so the saved settings apply as on START. It does not switch
  between Original and EX.
- `SFX_VR_EXIT_AFTER=<s>` sets the QUIT TO STEAM request once that many seconds
  have passed since the first submitted in-game frame (`session.request_exit`,
  then the bounded 2 s wait). It never fires while the startup menu is open.

With any of them set, one `[vr] diagnostic overrides: ...` line goes to stdout
and `vr-session.log` at startup. A `[vr-perf]` window with any forced frame
ends in ` forced=1`.

Unattended capture, for example 120 Hz on Corneria for 60 s: set the env for the
title's launch (for example as a prefix in the Devkit launcher arguments, or a
temporary wrapper that exports them and runs `LAUNCH-STEAM-FRAME.sh`), launch
through Steam as usual, then read
`~/.local/share/StarFoxEnhanced/vr-session.log`.

```
SFX_VR_FORCE_RENDER=1 SFX_VR_AUTOSTART=LEVEL1_1 SFX_VR_EXIT_AFTER=60 \
SFX_VR_REFRESH_RATE=120 SFX_VR_TIMING_GPU=1 ./LAUNCH-STEAM-FRAME.sh
```

Add `SFX_VR_DIAG_YAW=90` (or 180) for turned views. Tests cover the synthetic
views (IPD, orientation, FOV, projection), the yaw turn, forced frames against
the injected session (standby, untracked, failed location, refused layers,
forcing off), the renderer with a forced and turned frame, env parsing with
everything unset, and autostart name matching. The new session cases pass on
the Mac; `starfox_vr_session_check` still aborts later, at the existing
locate-failure case, as before this change.

Untested on the device: whether SteamVR on the Frame accepts and paces layers
while `shouldRender` is false (WipEout VR's `force_render` also submits them,
and its unattended run on October 2 held 72 fps, which is encouraging), whether it honours a
refresh request in standby, whether it throttles `xrWaitFrame` while the
headset is off the head (then the numbers are not the worn numbers), and how
the env reaches the process through the Devkit launcher.

### Arming an unattended run (October 4)

Steam launches the title with fixed arguments, so `LAUNCH-STEAM-FRAME.sh` now
reads a one-shot `~/.local/share/StarFoxEnhanced/vr-diagnostics.env`. It exports
only `SFX_VR_*=value` lines and deletes the file before the game starts, so a
later normal launch never inherits diagnostic mode. Arm a run by writing, for
example, `SFX_VR_FORCE_RENDER=1`, `SFX_VR_AUTOSTART=LEVEL1_1`,
`SFX_VR_EXIT_AFTER=120` and `SFX_VR_REFRESH_RATE=120` on separate lines, then
launch through `steam.pipe` while holding the Frame lock.

### Review fixes (October 4)

An independent read-only review of 28b1d37..bd4e2da found no crash, NaN or
math error, and confirmed that the smoothing correction and the credits glyphs
are exact. Fixed:
- With the Follow ease, the seat offset now pivots with the eased attitude. The
  world turned by the eased attitude while the eye's seat offset used the source
  one, so content at the ship origin drifted off the cabin's ship during rolls
  (up to about 1.3 m at the 90° cap). Steering still uses the source attitude
  per tick, by design.
- `RefreshRate::request` clears `fell_back_`, so choosing a rate after the floor
  is governed again.
- SYSTEM requests 0 Hz ("no preference" in XR_FB_display_refresh_rate), so the
  system rate applies at once. `current()` keeps updating for the `[vr-perf]`
  `display=` field.
- The step-down starts below the lower of the reported and requested rates, so a
  runtime that ignores a request does not get the same rate again.
- Doc correction: frames keep arriving while paused, so the Follow ease does not
  snap on pause; it settles in about 0.3 s. It snaps on cuts and on the first frame.
Not changed: the lost-wing trim assumes the live ship's pose scale is 1, as seen
in the cartridge test; clipped hull vertices interpolate only position, colour
and UV, which is enough for MY_DEMOS's flat faces.

## First unattended frame-time capture (October 4)

`11ad19b` was installed and run unattended with `SFX_VR_FORCE_RENDER=1`,
`SFX_VR_AUTOSTART=LEVEL1_1`, `SFX_VR_EXIT_AFTER=120`, `SFX_VR_REFRESH_RATE=90` and
`SFX_VR_TIMING_GPU=1`, launched through `steam.pipe` while holding the Frame lock.
The launcher consumed the one-shot file, the level started, every window was
forced, and the run exited on time. Log: `build/frame-devkit/runs/11ad19b-forced-LEVEL1_1-90hz.log`.

- With the headset in standby the runtime offered only 90 Hz, so 120 Hz cannot
  be measured unattended.
- At 90 Hz the app settled at a steady 45 fps (half rate, 0–2 missed frames per
  window after the first 30 s). CPU per frame was about 2 ms (logic about 1.2).
  GPU per frame (both eyes summed) was 11.4–16.5 ms against the 11.1 ms budget.
  Per-eye CPU submit-to-fence was about 11 ms for eye 0 and 7–9 ms for eye 1.
- To hold 90 Hz, GPU time must drop by roughly 20–30%. Caveat: standby may
  lower GPU clocks, so worn numbers may be better. A worn [vr-perf] reading is
  still needed.
- `SFX_VR_RESOLUTION_SCALE` was added to measure how GPU time follows eye-buffer
  size (2160x2160 by default, from `preferResolution` in `vrpreferences.json`).

### Resolution comparison and RENDER RESOLUTION setting (October 4)

`6df05f1` was run unattended twice on LEVEL1_1 at 90 Hz (forced render, 90 s each).
Logs: `build/frame-devkit/runs/6df05f1-forced-LEVEL1_1-90hz-scale{1.00,0.75}.log`.

| Eye buffers | GPU per frame (both eyes) | fps | Missed per 10 s window |
| --- | --- | --- | --- |
| 2160x2160 (100%) | 11.4–16.2 ms | 45 (half rate) after 30 s | 0–2 at half rate |
| 1512x1512 (75%) | 6.9–10.6 ms | 59–90, mostly 81–90 | 0–75, rising in heavier scenes |

GPU time follows pixel count closely: 56% of the pixels gave about 64% of the GPU
time. At 75% the run held 90 fps in lighter stretches but still missed frames
when GPU time reached 9–10.6 ms. Holding 90 Hz throughout will need about 70%,
or shader work. CPU stayed at 1–2 ms. Standby may lower GPU clocks, so worn
numbers may be better.

VR PRESENTATION now has RENDER RESOLUTION: 100% (default), 90%, 80% or 75%, saved
as preferences v10 (byte 29; v1–v9 load as 100%). Eye buffers are created at
launch, so the application reads the saved choice before creating swapchains, and
the row shows NEXT LAUNCH until a relaunch applies it.
`SFX_VR_RESOLUTION_SCALE` (0.5–1) overrides it and shows as "<n>% ENV".

### Per-eye GPU and pre-pass timing (October 4)

A read-only investigation found that the only GPU work exclusive to eye 0 is the
compute pre-pass (`VulkanSourceScene::record_compute`). That covers connected-grid
compute every frame, plus up to five serial dispatches with a full barrier after
each, for every dirty source model. It also found that the eyes are fully
serialised: eye 1 is not acquired until eye 0's fence completes. Submit-to-fence
CPU time also includes queueing and clock ramp-up, so the eye 0 gap may not be
GPU work. With `SFX_VR_TIMING_GPU=1`, `[vr-perf]` now appends
`gpu_eyes=<eye0>/<eye1>ms` and `pre=<ms>`. `pre` is the GPU time from eye 0's
command start to the end of its pre-pass (a third timestamp query). If gpu0 − gpu1
≈ pre, the gap is compute: batch the model dispatches by stage to cut about 5N
barriers to 5. If gpu0 ≈ gpu1, the gap is serialisation: submit eye 1 without
waiting for eye 0's fence.

### Per-eye result (October 4, morning)

`e06ac88` was installed and run unattended on LEVEL1_1 at 90 Hz for 90 s. Log:
`build/frame-devkit/runs/e06ac88-forced-LEVEL1_1-90hz-pereye.log`. The runtime
now recommended 2016x2016 eye buffers, not the 2160x2160 seen overnight.
- `gpu_eyes` 5.5–7.9 / 5.9–8.4 ms: both eyes cost the same on the GPU.
- `pre` (eye 0's compute pre-pass) is 0.10–0.31 ms. It is not the eye 0 gap.
- Eye 0 submit-to-fence CPU time is still about 10–12 ms against 6–9 ms for eye 1.
  The extra 3–5 ms is waiting, not GPU work. It comes from the serial
  submit-wait-submit eye loop and queueing behind the compositor at frame start.
- The summed GPU time (11.4–16.2 ms) still exceeds the 11.1 ms budget at 100%, so
  90 Hz at full resolution stays GPU-bound. At 75–80% the GPU fits, and removing
  the serial wait (submitting eye 1 without waiting for eye 0's fence) is the next
  fix. The compute-barrier batching is not worth doing.

### Overlapped eye submission (October 4)

Untested on the device. Covered by the host build and injected-fake unit tests only.

`SFX_VR_OVERLAP_EYES=1` stops the eyes waiting on each other. Eye 1 gets its own
`VulkanEyeCommands` (command pool, buffer, fence and timestamp query pool), so a
frame goes: acquire eye 0, record and submit it, acquire eye 1, record and submit
it, then wait for eye 0's fence and release its image, wait for eye 1's fence and
release its image, then the UI panel and `xrEndFrame`. The two submissions sit on
the queue back to back. With the variable unset there's one command object and the
old acquire-submit-wait-release loop runs as before. The mode shows in the
`[vr] diagnostic overrides` line as `overlap_eyes=1`.

- Each image is still released only after its own fence, in eye order. OpenXR would
  allow releasing right after submission, since `device.queue()` is the graphics
  binding's queue (queueIndex 0 of its family), but the frame waits for both fences
  before `xrEndFrame` anyway, so releasing early gains nothing. It also keeps the
  rule that no image goes back while its fence is uncertain.
- Both fences are observed before the UI panel and `xrEndFrame`, so the next frame's
  per-frame block (game tick, scene, sprite, HUD and background uploads, model
  updates, compute inputs) never starts with eye work in flight, same as before. The
  block now checks this and stops with "Eye work still in flight at a new frame" if
  it ever happens.
- Nothing shared is written between eye 0's submit and eye 1's recording. Eye 1's
  callback skips the per-frame block (same display time) and its recording only
  binds buffers and pushes constants. The compute pre-pass is still recorded in eye
  0 only, and the barriers that make its output visible to eye 0's draws also cover
  eye 1, because a barrier's second scope includes later submissions on the same
  queue. Each eye has its own colour and depth images.
- If eye 1 fails while eye 0 is in flight, the renderer waits for eye 0's fence
  before returning both images and ending the frame with no layers. A fence error
  still keeps both images for device teardown, and teardown waits for the queue to
  go idle if either eye is pending.
- Frames with ray-traced shadows stay serial, because eye 1's ray producer reruns
  the shared compute pre-pass. Ray tracing is off by default and not available on
  the Frame. The UI panel was already its own submission after both eyes and is
  unchanged.

Timing in this mode:

- `eye=a/b` is still each eye's CPU submit-to-fence time, but it reads differently.
  Eye 0's includes recording eye 1, because its fence is only checked after that.
  Eye 1's includes queueing behind eye 0. Don't add them up.
- `gpu_eyes` is still each command buffer's own top-to-bottom timestamp span. The
  two spans can overlap on the GPU, so their sum (and `gpu=`) can overstate it.
- `span=<ms>`, only in this mode, is eye 0's submit to eye 1's fence, averaged over
  the window: the CPU wait both eyes cost together. The serial equivalent is about
  eye0 + eye1.
- `--profile-csv` columns are unchanged and carry the same meanings.

To check on the device, run the 75% LEVEL1_1 forced capture with and without
`SFX_VR_OVERLAP_EYES=1` and compare fps, missed and `span` against the serial `eye`
sum. If it helps but isn't enough, the next step is calling `xrEndFrame` before the
fences and waiting at the start of the next frame's per-frame block instead, so the
game tick and uploads overlap the GPU too.

### Overlap results (October 4)

`b7b89bf` ran unattended on LEVEL1_1 at 90 Hz in four 90 s runs. Logs:
`build/frame-devkit/runs/b7b89bf-forced-LEVEL1_1-90hz-{serial,overlap}-{1.0,0.75}.log`.

| Mode | Eye buffers | fps per 10 s window | Mean fps | Missed total | Notes |
| --- | --- | --- | --- | --- | --- |
| serial | 2016² | 50, 55, 46, then 45 | 47 | 85 | GPU 11.4–16.2 ms |
| overlap | 2016² | 50, 57, 52, 48, then 45 | 48 | 138 | span 15–20 ms; no gain |
| serial | 1512² | 57, 84, 90, 85, 81, 69, 55, 85 | 76 | 371 | |
| overlap | 1512² | 67, 87, 90, 88, 86, 77, 59, 87 | 80 | 317 | span 10–13.5 ms |

Overlap is correct (no failures or corruption in the logs) and helps a little at
75%: about 5% more frames and 15% fewer misses. It doesn't help at 100%, which is
GPU-bound. At 75% the span from eye 0's submit to eye 1's fence (10–13.5 ms) is
still about 3 ms above the summed GPU time (7–10.5 ms). That remaining wait sits
outside the eye work, most likely queueing behind the SteamVR compositor, which
overlap cannot remove. The heavier stretch of Corneria (windows 5–7) still drops
to 55–77 fps at 75%. Holding 90 Hz there needs either less GPU work per pixel
(shader cost) or about 65–70% resolution. Overlap stays opt-in.

## Wear result (October 4)

The user wore the Frame build `b7b89bf`, as relayed by the "VR game testing and
feedback" session: cockpit mode at a 90 fps target, default settings (overlap
off, RENDER RESOLUTION 100%).
- The opening "Nintendo presents" credits text is now stable when the head
  tilts (b7bcd63): accepted.
- 3D looks good, and the station-scramble cutscene "looks amazing".
- Scale looks right: the cutscene Arwing at 24x, the canopy seat, the rear
  bulkhead and the roll hoop (ddee4ff, 1d68555).
- Verdict: "this feels great". No issues reported.

Status: played in headset and accepted, for the 24x cockpit scale with the
cutscene hull and canopy seat, cockpit motion smoothing and the Follow ease,
and the opening credits. Still open: worn frame-time numbers ([vr-perf]
while worn), the RENDER RESOLUTION and REFRESH RATE settings in the headset, and
the opt-in overlap mode.

## Pointer always on top (October 4)

Untested on the device. Covered by the host build and injected-fake unit tests only.

The pause-mode sandbox pointer used to be drawn into the eye pass, before sprites,
cabin and HUD, and the paused SNES screen is a quad layer submitted after the eye
layer, so the compositor drew the panel over the beam. Under the cross-port pointer
rule (it composites over everything), the pointer now has its own projection layer:

- A second pair of per-eye swapchains at half the eye buffer size, same format
  preference as the eyes, created the first time the sandbox opens. No depth.
- After both eyes and the UI quad, each pointer eye is cleared to (0,0,0,0) and
  draws only the beam and an end dot, using the existing opaque line and triangle
  pipelines (no shader change). They write the vertex colour, alpha included, and the
  pointer vertices carry alpha 1. Cleared transparent black plus opaque pixels is
  already premultiplied, so the layer sets `BLEND_TEXTURE_SOURCE_ALPHA` and not
  `UNPREMULTIPLIED_ALPHA`, and the compositor's filtering of the half-size image
  gives clean edges.
- The layer goes last, after the quad: eye layer, UI quad, pointer. It uses the same
  views, poses and space as the eye layer and the same scene camera the beam was
  drawn with before, so the beam still lines up with the world.
- The end dot is an eye-facing disc at the beam's end (the hit, or 128 m on a miss),
  about 0.9° across, using the existing billboard (4) and disc (4096) vertex flags.
- When the sandbox isn't active nothing changes: no pointer swapchain work, the same
  two layers and the same eye pass, minus the pointer draw that used to be in it.
- Composition keeps its progress across pending retries, so a released UI or
  pointer image is never acquired twice in a frame. A failure cancels both and
  returns every image.

To check on the device: pause, aim at the paused screen and past its edges, and
grab something. The beam and dot should stay visible over the panel, cockpit and
HUD; the beam should start at the controller and not shimmer badly at half size. The
`[vr] pointer layer WxH, format N` line appears the first time the sandbox opens.
Also watch frame time while paused, since the pointer adds two small submissions.
The pause sandbox opens on every pause, so the pointer layer is created on the
first pause of each session. Its creation is non-fatal: on any failure the
session logs `[vr] pointer layer unavailable; drawing the pointer in the eye
pass: ...`, closes the partial objects, and draws the beam in the eye pass as
before, rather than ending the game.

### Pointer layer delivery (October 4)

Workflow `37208910890` passed for `a096d16` (pointer on its own top layer, with
the eye-pass fallback). The package was installed on the Frame over Wi-Fi while
Star Fox was not running; all package hashes match and nothing was launched.
`b7b89bf` is kept in `~/devkit-game/_StarFoxEnhanced_prev/`. To check in the
headset: pause in a level and point either controller through the pause panel.
The beam and the end dot should stay visible on top of the panel, the cockpit
and the HUD. Also check whether vr-session.log contains a pointer-layer fallback line.
