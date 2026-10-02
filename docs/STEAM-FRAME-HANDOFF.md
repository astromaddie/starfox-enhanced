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
