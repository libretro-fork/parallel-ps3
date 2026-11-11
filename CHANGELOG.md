# Changelog of the libretro core

What changed in the libretro core of this fork, newest first. Changes that come with a merge of upstream RPCS3 are in upstream's own history; a merge is noted here with the upstream build it brought in.

## Unreleased

- The core builds without CMake too: `make` at the top of the tree, as libretro's other cores build (Makefile, with the source lists in Makefile.common). It builds what the CMake build builds, from the same sources with the same flags: LLVM (21 or newer, found through llvm-config, linked statically), RPCS3's prebuilt ffmpeg (downloaded as the CMake build does, or `SYSTEM_FFMPEG=1`), GLEW statically; the config database, the overlays' icons, the VBlank list and RPCS3's game patches (fetched from rpcs3.net, `EMBED_PATCHES=0` to leave them out) are compiled in by a small C tool (rpcs3/libretro/make/embed.c) whose output is the CMake build's, byte for byte. The core exports its libretro functions and nothing else. `make check` loads it with every symbol resolved; CI builds it this way on Linux with LLVM 22. The macOS (`platform=osx`) and Windows (`platform=win`, MinGW-w64) sections follow the CMake build but have not been built yet.
- The Linux core no longer leaves a function of ffmpeg's undefined: the CMake build linked the prebuilt ffmpeg libraries with libavutil before libswscale, so `ff_init_half2float_tables`, which swscale calls and avutil has, was never taken in. The core loaded anyway, as RetroArch resolves functions when they are first called, and would have stopped at the first scaling that reached it; a loader that resolves everything at once (`RTLD_NOW`) refused it. avutil is now linked last.
- RPCN's messages (rooms, matchmaking, messages, scores, TUS) are read and written by the core's own code instead of the protobuf library, which is no longer built, nor is the Abseil library it fetched from GitHub at configure time (the protobuf submodule, `USE_SYSTEM_PROTOBUF` and the `protobuf_*` options of the CI scripts are gone). The code is made from RPCS3's `np2_structs.proto` by a script beside it (`rpcs3/Emu/NP/generated/np2_structs_gen.py`), with the names protobuf gave it, so RPCS3's code that fills and reads the messages is as it was. What goes to the server is the same, byte for byte, and what comes from it is read the same: CI reads 16,000 messages the protobuf library wrote, read in other ways it accepts, or broken, and every one must come out as protobuf made of it (RPCS3's own protobuf, at the commit it pinned, agreed with them all). A reply is read where it lies in the packet - its strings and bytes are not copied out - and a request is written straight into the packet that is sent, where it was written into a string, copied into a buffer and copied again. Not tried against the RPCN server yet.
- Text is drawn by libretro-common's rtt instead of stb_truetype, and stb is no longer in the tree (its submodule is gone): the overlays' text (the home menu, messages, trophies) and the fonts games draw with cellFont. Glyphs are placed exactly as before - CI packs whole pages of a TrueType and a CFF (CJK) font both ways and every glyph's place, size and advance is the same - and they come out closer to the true shape of the font: rtt follows curves more finely than stb did, about four times closer on average and three times in the worst texel, and still draws a page in about half the time. A font a game opens is kept in a table of the core's instead of in the game's memory after its CellFont, which stb's state overwrote; its glyphs are drawn straight into the game's surface instead of into a buffer allocated for each one and copied. Not tried with a game yet.
- Save states no longer go through a file: Save State has RPCS3 write the machine's state into memory the core keeps, and Load State boots from RetroArch's buffer through that same memory, instead of writing `<title>_libretro.SAVESTAT.zst` to disk and reading it back. A save copies the state once, into RetroArch's buffer; a load copies it once, out of it. The memory is kept for the session and reused by the next save, so it is not taken again each time. Not tried with a game yet.
- RPCN: dropping the connection (a failed login, a reconnect) no longer frees it under the reader and writer threads: it waits for both to have left it, which they do within 50 ms. Before, the connection could be freed while the reader was still in it. The two threads are told apart from it with atomics instead of a mutex each, and the packets to send go through a lock-free queue instead of a mutex-guarded list. The writer thread no longer ends for good after a failed send, which left later connections with nothing to send their packets. Not tried against the RPCN server yet.
- PNG decoding for games (cellPngDec) takes its decoder from a pool of eight that closed streams give back, instead of making one, with its 64 KB file buffer and its row memory, at every open and freeing it at every close. Games open PNGs as they load; once the decoders have decoded an image of a size, opening another takes no memory. The pool is lock-free and is emptied when the emulation ends. CI's PNG comparison now decodes all its 9440 cases with one decoder, reset in between, as the pool uses them.
- YAML goes through libretro-common's ryaml (reading) and ryaml_writer (writing) instead of yaml-cpp, which is no longer built (its submodule is gone): the configuration and per-game configurations, the patches and the patch configuration, games.yml, the disc image cache, the players' history, music playlists and the PRX validation files. A document is read where it lies - from the file's buffer, or from the string a setting comes in - instead of being copied into a tree of its own. The settings database (`config_database.dat`), which is JSON, is read with libretro-common's rjson in one pass that stops at the game's entry. Files written before read as they did: CI reads an excerpt of RPCS3's patch.yml (anchors and the `load` entries that name them, values on the line below their keys, escapes) and 300 documents written by yaml-cpp's emitter, and checks every tree against yaml-cpp's, then writes each again as the core now does and reads it back. What the core writes is laid out as before, two spaces a level; a double is written with the fewest digits that read back the same. Not tried with a game yet.
- RPCN's connection to its server is encrypted by libretro-common's own TLS client instead of wolfSSL, which is no longer built (its submodule and the `USE_SYSTEM_WOLFSSL` option are gone). RPCN reads on one thread and writes on another at the same time, which wolfSSL did through its "write dup"; libretro-common's client now has a duplex mode for this with no lock, where a key update the server asks for is answered by the writing side. The server's certificate is still not checked. The handshake waits on the socket for what it needs instead of spinning, for 10 seconds at most, and may now be TLS 1.3 where the server offers it. The error RPCN shows when this fails reads "TLS Error" instead of "WolfSSL Error". CI runs a reader and a writer thread on one connection against a local OpenSSL server, the way the RPCN client does, under AddressSanitizer and ThreadSanitizer. Not tried against the RPCN server yet.
- BC1, BC2 and BC3 (DXT) textures, for a GPU that cannot sample them itself (some phones), are decoded on the CPU by libretro-common's rdds block decoders instead of RPCS3's copy of bcdec, which is gone. The texels are the same (BGRA, straight into the upload buffer as before), except the interpolated alpha of BC3: bcdec cut each step of the ramp down, rdds rounds it to the nearest value, which raises it by one on about a quarter of those texels. A GPU that decodes BCn itself (most desktop ones) is not affected. CI checks the decoders against bcdec at the commit RPCS3's copy came from, over random blocks and every BC3 alpha ramp. Not tried with a game yet.
- SoundTouch is gone, and with it RPCS3's Time Stretching (Enable Time Stretching and its threshold, in the config and the home menu's audio settings). The core already kept it off: it only worked on top of cellAudio's own buffer, which the core also keeps off, and RetroArch's dynamic rate control keeps the sound in step with what it plays. The game's sound goes straight into the buffer RetroArch reads from, as it did. A config or database entry that still sets the two options is ignored.
- Save states. RetroArch's Save State and Load State now work, through RPCS3's own savestates: a save stops the emulation, writes the whole machine to a zstd-compressed state and boots back into it, which takes a few seconds and a few hundred MB; a load boots the state. The state is read from RPCS3's file straight into RetroArch's buffer. Its size is that of the state itself, which varies, not a fixed maximum. They are for saving and loading only: making one every frame is out of reach, so rewind, run-ahead and netplay stay off (the .info file says "basic"). Saving fails, as in RPCS3, while SPU threads cannot be paused where a state can be taken (SPU Savestates-Compatible Mode helps), while a video decoder is open or while the game is saving; RPCS3 says which on screen and the game carries on. Not tried with a game yet.
- HTTP goes through libretro-common's net_http instead of libcurl, which is no longer built (its submodule and the `USE_SYSTEM_CURL` option are gone): the clans server's requests, over https with libretro-common's own TLS client (no certificate checked, as before), and UPnP's. net_http keeps its connections and DNS cache for one thread, so every transfer runs on one network thread that callers hand their request to over a lock-free queue and wait on their own completion for; there is no lock. A clans request now gives up after 30 seconds instead of waiting for ever. The clan ticket and member data are base64-encoded with libretro-common's encoder instead of wolfSSL's (which stays, for RPCN). CI posts a clans request through net_http, built as the core builds it, to a server on the runner and checks the request and the answer, including an HTTP error page, which is still taken as the answer. Not tried against a clans server yet.
- JPEG and GIF decoding for games (cellJpgDec, cellGifDec) and the overlays' images (icons, backgrounds) go through libretro-common's rjpeg, rgif and rpng instead of stb_image, which is now built only for stb_truetype (the overlays' fonts). The game's buffer is decoded where it lies instead of being copied first, and the pixels go straight into the game's output buffer in the format it asked for, with no image-sized staging copy; a GIF is decoded in a single pass, and the decoder's memory and the read buffer for a file are taken once on each thread that decodes and kept. CI compares the three decoders with stb_image 2.30, the version RPCS3 pins, over PNGs, JPEGs and GIFs of every kind: the same pixels, except where stb_image was wrong - it swapped red and blue in a GIF's background colour, and in a 4:2:2 JPEG it weighed the last chroma column the wrong way round. RGB output from cellJpgDec was three-byte pixels cut from four-byte ones (scrambled); it is now RGB. A broken file is still refused with the same error, but may leave part of the output buffer written. Not tried with a game yet.
- PNG decoding for games (cellPngDec) goes through libretro-common's rpng_rows instead of libpng, which is no longer built. The pixel conversions games ask for (palette to RGB, transparency to alpha, adding, removing or moving alpha, 16 to 8 bits, RGB to gray) are done as the libpng the module used does them; CI compares the two byte for byte over every PNG colour type, bit depth and output format, interlaced or not, for both whole-image and row-by-row (progressive) decoding. A broken PNG now returns an error to the game instead of stopping the emulator. RGB-to-gray output of a PNG with colour chunks (gAMA, sRGB, cHRM, mDCV) comes out as libpng 1.6.59's did too, the version RPCS3 built: the channels weighted by the file's chromaticities, mixed in linear light when the file has a gamma, through the same tables, built once when the header is read. A row that needs no conversion is now handed on where the decoder left it instead of being copied first. Not tried with a game yet.
- UPnP port forwarding (Network > UPnP) goes through libretro-common's net_upnp instead of miniupnpc, which is no longer built. The router found before, saved in `upnp.yml`, is used again without a new search, and a port the router has taken is tried as the next one up, as before. Not tried with a real router by anyone yet.
- XML goes through libretro-common's rxml instead of pugixml: trophy configurations, the redump database check and the clans server's requests and responses. The trophy and redump files are read straight into the buffer the parser keeps, without the copy they went through before. Not tried against a clans server by anyone yet.
- RPCS3's own compression goes through libretro-common's Deflate instead of zlib: the compressed log (`rpcs3_detailed.log.gz`, still complete up to its last line after a crash), gzip savestates, IRD files, PKG/SDAT and SELF decompression and the IRD checksums. Files written before read as they did, and what the core writes is standard gzip (checked in CI). zlib is no longer built at all (the in-tree LLVM and curl builds are told not to use it).
- Savestates are compressed and read with libretro-common's Zstandard instead of the zstd library, which is no longer built. Savestates made before load as they did, and the ones the core writes are standard Zstandard frames, which the reference zstd reads (checked in CI). They come out a little larger: on one 16 MB sample 9% larger than zstd's, written at the same speed.
- Namco System 357/369 arcade games: new option Core > Arcade USIO Mode, Fighting Games (default, as standalone) or Shooter Games, which the arcade board reads its buttons by. RetroArch's light guns, or the mouse RetroArch makes one of, are the PS3's mice now, one per port, which the shooters (Deadstorm Pirates, Dark Escape) aim with; the core used to have no mouse at all. In Shooter Games ports 1-2 and 3-4 trade places, as those games take the board's players 3 and 4 as their players 1 and 2, and the gun's trigger, second trigger and start reach the board as it expects. The controls list names what Select, L1, L2 and R2 are on the arcade board: Test, Card, Coin and Service. Not tried with an arcade game yet.
- Linux: content loaded again in the same RetroArch, after closing it, starts from scratch: the core was never really unloaded, so the second start ran on the first one's state and, with OpenGL, drew a flipped and broken picture. RPCS3's logging kept two buffers per thread whose destructors tied the core to the thread that used them until it ended, and RetroArch's main thread logs too and never ends; they are plain buffers now.
- Linux, Android and macOS: a crash is no longer the core's to end. RPCS3's fault handlers (segfault, illegal instruction, broken pipe) are taken out when the core is unloaded and the ones there were before put back, as on Windows already; left in, a later crash anywhere in RetroArch jumped into code that was gone. A fault the core does not handle goes to the previous handler instead of ending RetroArch from inside the core, so the system's crash report (Android's tombstone) is written and names the fault (sco8487).
- The firmware is found whatever the case of its name: a `PS3UPDAT.pup` was not installed on Linux and Android, where only `PS3UPDAT.PUP` was looked for (demifiend9).
- A GPU that lacks a Vulkan feature RPCS3 requires (shaderUniformBufferUnsizedArray, on Mali phones) no longer hangs RetroArch: the core tries RPCS3's Vulkan device before the boot, and refuses the content with a message when it cannot be made. A game whose boot still fails part way has the threads the boot started stopped and its disc image let go. RetroArch used to stay on "Unloading core..." until it was killed (sco8487).
- Linux and macOS: the core raises the limit of open files as standalone RPCS3 does (4096 on Linux). With the frontend's, often 1024, the NVIDIA Vulkan driver ran out of file descriptors as the first game started, and the game froze at once with "Out of host memory (VK_ERROR_OUT_OF_HOST_MEMORY)" (demifiend9, RTX 4050). The log says the limit the core got.
- Reset works for a game loaded from a disc image (`.iso`): it used to close the content with "The game could not be restarted", because stopping the emulator unmounts the image and the reset booted the game from it without mounting it again.
- Android x86_64 (buildbot): ffmpeg is built with its x86 assembly, as the buildbot's Android image now has nasm; it decodes the games' videos faster there.
- The `libretro-latest` release is gone: libretro's buildbot builds the core for every platform it had, and installs it through RetroArch's Online Updater, while the release had fallen behind. Test builds stay as artifacts of the CI workflow.
- Android: a game behind a `saf://` path, as the Play Store RetroArch gives for folders it was granted, is read through RetroArch's VFS for directories too: listing, stat, creating, removing, renaming and copying, not only opening files. That is what an unpacked game in such a folder needs. Ordinary paths are untouched. Not tried on a real SAF path by anyone yet.
- New option Disk Cache Size (1 GB by default, 5 GB, Wipe on Exit or Unlimited as before) for the games' cache on `dev_hdd1`: with a size RPCS3's own disk cache limit is used, deleting the caches used longest ago when a game is loaded and when it is closed; Wipe on Exit deletes all of it at both points (NNshi).
- The settings database in `GuiConfigs/config_database.dat` is replaced by the core's only when it is missing or carries an older `timestamp` than the core's; it was replaced whenever its bytes differed, so entries edited by hand did not stay (NNshi).
- New option GPU > Aspect Ratio, 16:9 (default) or 4:3, as RPCS3's own setting: with 4:3 a game with a native 4:3 mode renders for it, and the core tells RetroArch the picture is 4:3 (unless Stretch to Display Area names another ratio); the picture fills the frame, so Stretch to Display Area does not have to be set for it. Takes effect when the game is started (NNshi).
- Frame Pacing is an option again (Emulator clock by default, or RetroArch: one PS3 refresh per frame RetroArch shows). It had been removed on one tester's result, which he could not repeat (NNshi).
- Pause and Frame Advance with the Vulkan renderer: once RetroArch stops asking for frames, the game waits after the frame it is drawing, and each Frame Advance step lets it draw exactly one frame and shows that one, drawn with the input of that step. The game used to run on for a tenth of a second and a step let 4-6 frames through (NNshi).
- Sound after unpausing: the first frames after a pause handed RetroArch up to four frames' worth of sound at once, which with RetroArch's audio sync on made the frame rate drop and swing for seconds. A pause is no longer made up for (NNshi).
- Linux: the core no longer links libGL, libGLX and libX11, so it loads on a system without X11. It links libOpenGL for OpenGL itself, and the GLX and Xlib functions it shares RetroArch's context with on an X11 session are found at run time, in RetroArch's own libraries.
- VBlank Frequency has a new choice, From List: a game named in `vblank_unlock.txt` runs with a 120 Hz VBlank - shown at 60 frames a second when it is a 30 fps game, at 120 when it is a 60 fps one - and any other game stays at 60 Hz. The list ships with the core and is written to `system/rpcs3/` when it is not there; the core never overwrites it, so titles can be added. It comes from the RPCS3 wiki's list of VBlank-compatible games and NNshi's testing.
- Frame Pacing is removed, option and code: the game's frames are timed by RPCS3's own clock, as in standalone. The other choice, one VBlank per frame RetroArch shows, added input lag and was better in no case tested.
- Save Data Slot is removed: the game's save list is always shown in the picture. The core picks the first entry itself only when the list cannot be shown.
- Default Resolution's tooltip warns against using it to upscale; Resolution Scale is for that.

## October 2026

### Upstream

- Merged up to upstream RPCS3 build 20243 (v0.0.43), after builds 20147 and 20149. The core reports upstream's version and build number, with this repository's commit after it, and logs both at start.

### Picture

- Vulkan through RetroArch's own hardware context: the renderer draws on RetroArch's device and hands each frame over as an image, with no copy through system memory. Finished frames are handed over in order, each once, so the picture no longer judders against RetroArch's refresh. The context is kept across RetroArch's video driver rebuilds, and replaced images stay alive until RetroArch has moved on.
- Before the first picture the Vulkan frame is 720p at the Resolution Scale, so the compilation progress is drawn sharp.
- A game no longer holds RetroArch through its PPU compilation at boot.
- Output Scaling and Vulkan Frame Readback are no longer options. Frame Pacing defaults to the emulator's clock.
- macOS: Vulkan through MoltenVK, no OpenGL in the core.

### Sound and pads

- Surround sound: the Audio Format option (Stereo by default, Surround 5.1, Surround 7.1) is what the PS3 reports to games, and with 5.1 or 7.1 the channels go to RetroArch's multi-channel output.
- Rumble through RetroArch's rumble interface.
- Pads report a PlayStation 3 controller, as standalone's handlers do, and the sticks reach full diagonals.

### Games and files

- PKG files are installed without being started, into the configured dev_hdd0, and the `.rap` licenses next to a PKG are installed with it.
- RPCS3's game patches (`patch.yml`) and its settings database ship with the core; Database Settings Override says whether the database's per-game settings apply. They, and the in-game overlays' icons, are rewritten when their contents differ.
- Save data is picked from the game's own list, shown in the picture.
- Emulate BD-ROM Read Speed as a core option.

### Core options

- The options carry RPCS3's own names and sections, in the layout NNshi proposed: CPU, GPU, Scheduling, Network, Debug, Core. Each option does what it says; several that were never read are now connected.
- Options for Disable Blit Engine Upscaling, RPCS3's performance overlay and the Resolution Scale Threshold.
- VBlank NTSC Fixup on by default, as in RPCS3.
- PSN Status says where an RPCN account comes from.
- On big.LITTLE ARM CPUs the compiler threads default to the big cores, and only the efficiency cores are left out.

### Builds

- macOS cores for Apple Silicon and Intel, and Android x86_64 next to arm64-v8a.
- The Windows core is `rpcs3_libretro.dll`; Linux links curl in instead of depending on Debian's libcurl-gnutls.
- Recipes for libretro's buildbot: Linux, Windows (cross-built with llvm-mingw), Android and macOS.

## Late September 2026

### Picture

- Vulkan is the default renderer, and the renderer option picks the renderer.
- Resolution Scale works: the scaled picture is carried through to RetroArch on both render paths and its size is reported.
- RPCS3's in-game dialogs are on and see the controller, including those that open before the game's first frame.
- Frame Limit Auto limits to the VBlank Rate, as in RPCS3; ZCULL Accuracy works; the resolution options are RPCS3's.
- A flip that waits forever for an image is reported and got out of, and the log says where the frames stopped when the picture does.

### Sound

- RetroArch gets one frame of audio per retro_run, at the rate the game played it, and is no longer starved of audio.

### Games and files

- Disc images boot: the ISO reader is ported from upstream, the game is found through its PARAM.SFO or PS3_DISC.SFB, and redump keys are found by the ISO's name.
- Games save and load without a save data dialog the core does not have; the save slot picked is the one chosen.
- Reset stops and boots the game the way unload and load do, and a game that restarts itself (exitspawn) keeps running.
- RPCS3's config and caches are under `system/rpcs3/`, and paths resolve the way standalone's do.
- Network options, with Winsock set up on Windows.

### Stability

- Closing content: the emulator finishes stopping before the disc goes away, the log writer thread is stopped in retro_deinit, and RPCS3's exception handlers are taken out on every unload on Windows.
- Windows fixes from NNshi's crash reports.

### The repository

- The Qt application, the standalone's input handlers and audio backends and the 7-Zip SDK are removed: the repository builds the core only.
- The core option texts are translated through libretro's Crowdin project.
- The core info file is published with the builds.

## Mid September 2026

- CI builds the core for Linux (x86_64, arm64), Windows and Android, with the libraries it needs bundled instead of taken from the host.
- A Vulkan renderer to choose besides OpenGL, and rendering without a hardware context through a headless swapchain.
- OpenGL: a context new enough for the extensions RPCS3 checks, GLEW linked in statically, and the RSX thread given a context of its own on X11.
- The core builds against both LLVM and both ffmpeg APIs.

## July 2026

- The first port: the RSX thread gets a real OpenGL context on Unix, core options are no longer discarded at boot, shared EGL contexts are not destroyed on the RSX thread, and the frontend is told when the guest exits on its own.
