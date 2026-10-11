# Steam Frame build and install

This repository has a native Linux cross-build for the standalone Steam Frame target: a Linux ARM64 OpenXR package. The script does not use Docker or require an ARM64 build host. Windows users can run it from Linux under WSL2. Mac users need a Linux VM or another Linux environment for this build script; it rejects macOS hosts. Windows desktop and PCVR outputs are separate targets and are not the standalone Frame package.

## Build host and output

Use a Linux shell with Bash, Git, Clang and LLD, CMake, Ninja, Python 3, curl, tar, `sha256sum`, and `readelf`. On Windows, install [WSL](https://learn.microsoft.com/windows/wsl/install) with an Ubuntu distribution and keep the source checkout in that distribution's filesystem. Open Ubuntu and install the host tools there:

```sh
sudo apt update
sudo apt install -y git build-essential clang lld cmake ninja-build python3 curl ca-certificates tar coreutils binutils
```

`coreutils` provides `sha256sum`, and `binutils` provides `readelf`. Docker is not used by this build.

```sh
git clone --branch main https://github.com/astromaddie/starfox-enhanced.git
cd starfox-enhanced
tools/build_steam_frame.sh
```

The script fetches and verifies the pinned Steam Runtime Sniper ARM64 sysroot, cross-builds a flat diagnostic and the Frame VR player, validates the package and writes it to `build/steam-frame-cross/package/`. It takes optional source, build and package directories as positional arguments. It includes `starfox_steamframe` and `LAUNCH-STEAM-FRAME.sh`; it does not include the ROM, generated `Starfox-Assets.BIN`, soundtrack, saves or graphics drivers.

The native build script accepts any Linux host, including Frame Desktop, if all listed tools are already available. On Frame Desktop, run `tools/build_steam_frame.sh` from a writable checkout; it builds the Linux ARM64 Frame package with the pinned Sniper sysroot. This repository does not provide a SteamOS toolchain or instructions for installing system packages there. Otherwise build on a Linux PC or WSL2 host, then copy the package to the Frame.

## Prepare your game data

Use a supported, unmodified retail Star Fox ROM with the matching `starfox_asset_builder` from a desktop build. The builder generates `Starfox-Assets.BIN`; the application checks its format, revision and embedded checksums. This user-generated bundle has no single repository-pinned file hash. Keep the ROM and bundle private. After transferring the bundle to the Frame, copy it to the exact runtime path in a Frame Desktop terminal:

```sh
mkdir -p "$HOME/.local/share/StarFoxEnhanced"
cp "$HOME/game-data/Starfox-Assets.BIN" "$HOME/.local/share/StarFoxEnhanced/Starfox-Assets.BIN"
```

The same directory stores saves, preferences and the shader cache. See the [upstream project guide](README_UPSTREAM.md) for supported ROM revisions and the [packaged start guide](../tools/package/STEAM-FRAME-START-HERE.txt) for controls and data layout.

## First install and Steam launch

For a first install from another computer, follow Valve's [Steam Frame development setup](https://partner.steamgames.com/doc/steamhardware/steamframe/setup) to enable Developer Mode and pair the host. In SteamOS Devkit Client, connect to the Frame by IP, then upload `build/steam-frame-cross/package/`. Set **Name** to `StarFoxEnhanced`, **Local Folder** to that package directory, **Start Command** to `LAUNCH-STEAM-FRAME.sh`, and **Runtime** to **Steam Linux Runtime 3.0 ARM64 (Sniper)**. The [Valve upload guide](https://partner.steamgames.com/doc/steamhardware/steamframe/loadgames) describes those fields. Uploading creates the Steam library entry; start it from there so Steam supplies the OpenXR runtime and controller input.

If the package is already on the Frame, keep the whole package in a writable folder under your home directory, add `LAUNCH-STEAM-FRAME.sh` through Steam's **Add a Non-Steam Game** flow, and start it from the Steam library. Copy your generated asset bundle into the data path first. Start it from Steam so Steam supplies SteamVR and controller input. Keep the port's experimental status and acceptance notes in the [Frame handoff](STEAM-FRAME-HANDOFF.md).
