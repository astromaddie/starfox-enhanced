# Star Fox Enhanced on Steam Frame

This is the native Steam Frame VR port of [Star Fox Enhanced](https://github.com/kandowontu2/starfox-enhanced), the C++ port of the original SNES game with both **Original Star Fox** and **Star Fox EX** modes. The Frame build runs on Linux ARM64 with OpenXR and Vulkan.

**EXPERIMENTAL.** I made this Frame fork for personal use, and it's still a work in progress. Bring your own supported Star Fox ROM.

The port keeps the game's two experiences and adds tracked stereo views, Frame controller input, a runtime VR menu, and cockpit and chase camera options. The cockpit uses a hybrid Arwing interior, combining the SNES Arwing shape with a cabin interior. The headset view includes the game's HUD and supports optional OpenXR hidden-area masks when the runtime provides them.

## What the Frame fork adds

- Native Linux ARM64 play on the headset, with OpenXR head tracking and Vulkan stereo rendering.
- Cockpit and chase views, including the hybrid Arwing cabin, adjustable world size and camera motion smoothing.
- Frame controller bindings, button navigation and a right-hand laser for the runtime menu.
- View-button recentring and height calibration, haptic strength settings, and Quit to Steam.
- HUD and display controls, with render-resolution and refresh-rate choices.
- Rendering improvements for the Frame, including hidden-area masks, overlapped eye submission and hardware-sampled backdrops.
- Persistent game preferences and saves outside the install folder.

## Frame controls

The left stick steers. On the Frame bindings, right A is brake, B is bomb, X is fire and Y is boost; the left and right bumpers roll left and right. Right Menu is the game's Start input; left View is Select and also handles the Frame's system actions.

Hold **Menu + View** for half a second to open the VR settings menu. Use the controller buttons or the right-hand laser pointer to navigate it. Hold **View** for one second to recenter direction and position while keeping the height anchor; hold it for three seconds to reset height too. The menu includes camera, world scale, HUD, haptics and display settings.

The Frame preferences target **2160×2160 per eye** and a 90 Hz minimum. The VR menu offers 90 Hz, 120 Hz and SYSTEM, plus a render-resolution setting. SteamVR's per-app settings control the display rate.

## Build the Frame package

Build from a **Linux host** with Clang, LLD, CMake, Ninja, Python 3, curl, tar, sha256sum and readelf. `tools/build_steam_frame.sh` downloads and verifies the pinned Sniper ARM64 sysroot, builds the flat diagnostic and VR targets, and stages the package:

```sh
git clone --branch main https://github.com/astromaddie/starfox-enhanced.git
cd starfox-enhanced
tools/build_steam_frame.sh
```

The package is written to `build/steam-frame-cross/package/`. The script accepts a source root, build directory and package directory as optional positional arguments. Its build environment and package contents are described in `tools/package/STEAM-FRAME-DIAGNOSTICS.txt` and `docs/STEAM-FRAME-HANDOFF.md`.

## Game data

Bring a supported retail Star Fox ROM. The game data is generated locally from the ROM; the ROM and main asset bundle are not included here. The supported revisions and setup steps are listed in the [upstream project guide](docs/README_UPSTREAM.md).

Star Fox Enhanced was created by Kandowontu2 and contributors; this repository carries that project into the Steam Frame VR target. See [Credits](CREDITS.md) and [Third-party notices](THIRD_PARTY_NOTICES.md) for attribution and licenses. The [saved upstream README](docs/README_UPSTREAM.md) retains its general platform, feature and build information.

The [Frame implementation notes](docs/STEAM-FRAME-HANDOFF.md) cover the camera, cockpit, storage and renderer changes. Keep the upstream and third-party license notices with the code. They don't grant permission to redistribute the Star Fox ROM or commercial game assets.
