# RPCS3 libretro

[RPCS3](https://rpcs3.net/) (PlayStation 3 emulator) as a libretro core for RetroArch and other libretro frontends.

This repository builds the core and nothing else. The standalone Qt application, its packaging and the input and audio backends a frontend replaces are not part of it; the emulator itself tracks upstream RPCS3.

## Getting the core

The core is built by libretro's buildbot for Linux (x86_64 and aarch64), Windows x64, macOS (Apple Silicon and Intel) and Android (arm64 and x86_64), and is installed from RetroArch: Online Updater > Core Downloader. Test builds of other branches are artifacts of the [Build libretro core](https://github.com/WizzardSK/rpcs3-libretro/actions/workflows/libretro.yml) workflow; their core goes into RetroArch's `cores` directory, together with `parallel_ps3_libretro.info` in its `info` directory.

The PS3 firmware (`PS3UPDAT.PUP`, from playstation.com) goes into RetroArch's system directory as `system/rpcs3/PS3UPDAT.PUP`; the core installs it on first start. Disc keys for redump ISOs go into `system/rpcs3/data/redump/`, named like the ISO with a `.key` or `.dkey` extension.

## Loading games

- A game in folder form is loaded by its `PS3_DISC.SFB` (a dumped disc) or `PARAM.SFO` (an installed or PSN game). Loading its `EBOOT.BIN` works too, but every game's executable has that name, so RetroArch's per-game and per-folder options can't tell the games apart; per-game option overrides only work when the game is loaded by its `.SFB` or `.SFO`.
- Loading a `.pkg` installs it to `dev_hdd0` (one at a time); a game is then started from its `PARAM.SFO` in `dev_hdd0/game/<title ID>/`, and its installed updates and DLC are used by it, a disc game's too. The `.rap` licenses next to the `.pkg` are installed with it; RPCS3 finds them by name, so each has to be named after its content ID (`<content ID>.rap`), as PSN dumps come.
- RPCS3's log is `system/rpcs3/rpcs3_detailed.log`; a crash report goes to RetroArch's save directory as `parallel_ps3_libretro_crash.log`.

## Tips

- **Database Settings Override** (on by default) applies RPCS3's per-game settings database, which covers most games' known issues. For a game that still misbehaves, its page in RPCS3's [compatibility list](https://rpcs3.net/compatibility) says what else it needs; the [Vblank compatible games list](https://wiki.rpcs3.net/index.php?title=Vblank_compatible_games_list) shows which games can run above 60 FPS.
- The **Patch Manager** category lists the loaded game's patches and turns them on per game. It keeps its own settings: standalone RPCS3's `patch_config.yml` is not imported.
- Standalone RPCS3 scales its picture to the window with bilinear filtering; RetroArch scales with nearest neighbour unless told otherwise, which looks sharper and blockier. For the standalone look, turn on Settings > Video > Scaling > Bilinear Filtering (as a core override, so it applies to RPCS3 only), or prepend a `bilinear.slangp` shader.
- A handful of rarely needed options are not in the menu, as standalone RPCS3 keeps them out of its settings dialog, but can be set in the core's `.opt` file (`RPCS3.opt`).
- The PS3 user name is in `dev_hdd0/home/00000001/localusername`; edit it to change it.
- `dev_hdd0` and the other emulated drives can be moved outside RetroArch's folders by editing `vfs.yml` (in `system/rpcs3/`, or `system/rpcs3/config/` on Windows).
- `dev_hdd1` fills up with games' cache over time. Options > Disk Cache Size limits it: to 1 GB by default, or 5 GB (the caches used longest ago go first), wiped whenever a game is closed, or Unlimited. Its contents can also be deleted by hand at any time.
- A game that has built up a large SPU/shader cache can take a minute or more to close. Let it finish.
- **Surround sound:** set Options > Audio Format to Surround 5.1 or 7.1 to match your speakers (Stereo is the default). It is what the emulated PS3 tells games it is connected to, so a game with a surround mix plays it in 5.1/7.1. It needs a RetroArch with multi-channel audio output (master builds since September 2026), RetroArch's Settings > Audio > Output > Output Layout set to the same layout, and an audio device configured for it (on Windows, the speaker setup in the sound settings). With Stereo, games make their own stereo mix.
- PS3 peripherals beyond the DualShock 3 (guitars, PlayStation Move, cameras and the like) are not supported yet.

## Building

The Linux build runs in a container with the toolchain RPCS3's CI uses, through `.ci/build-libretro.sh`; see `.github/workflows/libretro.yml` for the exact invocation, and `.ci/build-windows-clang-libretro.sh` for Windows (MSYS2 clang64). A local build is a regular CMake build of the `rpcs3_libretro` target:

```bash
git clone --recursive https://github.com/WizzardSK/rpcs3-libretro.git
cd rpcs3-libretro
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DUSE_NATIVE_INSTRUCTIONS=OFF -DUSE_LIBEVDEV=OFF
cmake --build build --target rpcs3_libretro
```

LLVM is needed for the PPU/SPU recompilers (`-DLLVM_DIR=...`, or `-DSTATIC_LINK_LLVM=OFF` with a shared `libLLVM` next to the core).

## License

Most files are licensed under the terms of the GNU GPL-2.0-only license; see the LICENSE file for details. Some files may be licensed differently; check the appropriate file headers.
