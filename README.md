# modHEN: PS5 GTA V Mod & Plugin Loader (Native Orbx Engine)

[![PS5 Firmware](https://img.shields.io/badge/PS5%20FW-13.60%20%7C%20All%20FW-blue.svg)](#)
[![Target Game](https://img.shields.io/badge/GTA%20V-CUSA00419%20%2F%20CUSA00411-orange.svg)](#)
[![Engine](https://img.shields.io/badge/Engine-Native%20Orbx%20v1.1-green.svg)](#)
[![Web Dashboard](https://img.shields.io/badge/Web%20UI-Port%208050-blueviolet.svg)](#)

A high-performance standalone background payload for PlayStation 5 that enables **Native Asset File Replacement (AFR)**, **Add-on DLC vehicles**, an **in-game mod menu**, and **PRX/SPRX plugin loading** inside Grand Theft Auto V (PS4 Version on PS5).

---

## Highlights & Features

### 1. Native Orbx Asset Replacement Engine (Early GOT Hook)
* **Early Boot Interception**: Hooks GTA V's Global Offset Table (`sceOpen`, `sceStat`, `sceRead`, `scePread`, `sceClose`, `fopen`) within the first 400ms of process launch.
* **Instant AFR Redirection**: Redirects any game asset request (`update:`, `common:`, `dlc:`) to custom files placed in `/data/orbx/gta/` on the PS5 internal SSD.
* **Case-Insensitive Radix Lookup**: High-performance binary search indexing with JoAAT hashing for zero IO lag and seamless streaming.
* **Add-On DLC Mounting**: Automatically registers and mounts custom `dlc.rpf` archives listed in `/data/orbx/gta/dlclist.xml`.

### 2. Built-in In-Game Mod Menu (`R1 + D-Pad Down` default)
* **Controller Hook & Custom Keybinds**: Synchronous `libScePad.sprx` hook (`scePadReadState` / `scePadRead`) with dynamic button suppression, **live in-game keybind capture** (`SETTINGS > KEYS`), optional single-button quick hotkeys (Horn Boost, Airbreak, Quick Repair, Quick Heal, Super Brake, Ragdoll), and automatic `/data/modHEN/config.ini` persistence.
* **Accessible Tabbed Navigation**: **8-category top tab strip** switched via `L1 / R1` (`HOME`, `SELF`, `WEAP`, `VEH`, `SPWN`, `WRLD`, `CAM`, `SET`), breadcrumb history stack, Left/Right/Center placement, Compact/Normal/Large scale modes, Keep-Driving-Open mode, and Left-Stick menu navigation.
* **Features**:
  * **Categorized Spawner & Squad**: 6 vanilla/Day-1 vehicle classes (56 models), custom model onscreen keyboard, `addon_cars.txt` DLC vehicles, armed SWAT bodyguards, AI Chauffeur to Waypoint, and Spooner Lite prop spawning/attaching.
  * **Player & Combat Cheats**: Godmode, Airbreak/No-Clip, Never Wanted & Wanted Star Lock, Speed/Swim Multipliers, Entity Opacity, Character Model Changer, Outfit Randomizer, Teleport Gun, Gravity Gun, Launch Gun, and Delete Entity Gun.
  * **Vehicle Trainer & LS Customs**: Full dynamic LSC body mod slots (`GET_NUM_VEHICLE_MODS` / `SET_VEHICLE_MOD`), Custom License Plate keyboard, 26 paint finishes + custom RGB, Rainbow Paint/Neon, Doors/Locks/Extras, True Seatbelt, Vehicle Rockets, Drift Mode, Torque Multiplier, Warp Nearest Vehicle, and Digital MPH/KPH Speedometer HUD.
  * **World, NVE Clouds & Camera/POV**: Fixed NVE Cloud Hats (`clouds.xml`) & 14 NVE Cloud Keyframe presets (`cloudkeyframes.xml`), persistent weather override, Slow-Motion (0.25x–1.0x), Pause Clock, Moon Gravity, Mobile Radio, IPL Interiors (including North Yankton), fixed Camera Context View Modes (`On-Foot`/`Vehicle`, separated Cinematic Cam), Lock First Person, 0.01m Near-Clip fix, Wide-FOV `SKEL_Head` First-Person camera (auto-yields on aim), No Camera Shake, and Freecam Photo Mode.
  * **60 FPS & Telemetry**: Enforced 60Hz flip rate with live frame rate counter and IO redirect statistics.

### 3. HyperHEN-Style `nullfs` Sandbox Mounting
* Loopback-mounts `/data/modHEN/plugins/` directly into the game's sandbox at `/mnt/sandbox/CUSA00419_000/plugins/` using FreeBSD `nmount()`.
* Bypasses PS5 sandbox jailing cleanly without duplicating files on disk.

### 4. Embedded Web Dashboard (Port 8050)
Access `http://<ps5-ip>:8050` from any PC or mobile browser on your local network:
* **Live Status**: Real-time game PID detection, 60 FPS monitoring, IO call/redirect counts.
* **Mod Manager**: Upload custom cars, DLC packs, and edit `dlclist.xml`.
* **File Manager**: Direct file browser for `/data/` with read, write, upload, and delete capabilities.
* **Web Menu Controls**: Trigger cheats, change weather/time, and spawn vehicles remotely from your browser.

### 5. Console Stability & Kernel Protection
* **Focused Process Scanner**: Only checks candidate game processes running `eboot.bin` within `CUSA00410`–`CUSA00419`, completely eliminating CPU overhead and IPC saturation on `SceShellCore`.
* **Clean Game Exit**: 300ms grace period with non-blocking VFS `statfs()` validation prevents VFS lockups so the **PS Button** and control center menu restore instantly upon quitting.
* **Two-Phase Graceful Shutdown**: Handles `SIGTERM` with full socket and thread teardown to allow updating or re-running the payload without triggering PS5 kernel panics.

---

## Directory Layout on PS5 SSD

```text
/data/modHEN/
├── loader.log                   <-- Live daemon log file
├── config.ini                   <-- Engine configuration
└── plugins/                     <-- Primary plugin directory
    ├── orbx.prx                 <-- Optional legacy Orbx PRX
    └── *.prx                    <-- Any additional PRX/SPRX plugins

/data/orbx/gta/                  <-- Asset File Replacement (AFR) mods
├── dlclist.xml                  <-- DLC load order
├── rpf/dlc_addon/               <-- Custom DLC add-on packs (e.g., 27rev, NVE)
│   ├── 27rev/dlc.rpf
│   └── NVE/dlc.rpf
└── data/                        <-- Custom XML, META, and DAT mod files
    ├── timecycle/               <-- Weather & timecycle lighting XMLs
    ├── visualsettings.dat       <-- Shaders, bloom, smoke, and light intensities
    └── hbaosettings.xml         <-- Horizon-Based Ambient Occlusion settings
```

---

## Quick Start Guide

### 1. Build the Payload
Using the included `llvm-mingw` toolchain:
```cmd
build.bat
```
Produces `gta5_plugin_loader.elf` (~1.6 MB).

### 2. Send to PS5
With your PS5 powered on and the ELF loader active (ports 9020/9021):
```cmd
send_loader.bat
```
*(Enter your PS5 IP when prompted; IP is saved automatically to `ps5_ip.txt`)*.

### 3. Launch GTA V
1. Launch Grand Theft Auto V (`CUSA00419` / `CUSA00411`).
2. The loader detects the game launch, mounts the sandbox, and hooks the GOT slots within 400ms.
3. In game, press **`R1 + D-Pad Down`** (customizable in `SETTINGS / KEYS`) to open or close the mod menu; use **`D-Pad Up/Down`** to scroll, **`D-Pad Left/Right`** to adjust `- Value +` presets, **`Cross`** to toggle or open, **`Circle`** to go back, and **`L1 / R1`** to switch category tabs.

---

## Mod Conversion & Packaging Tools (`tools/`)

The repository includes a comprehensive suite of automated converters to translate PC GTA V mods into PS4/PS5 backward-compatibility format:

* **`auto_convert_car.py`**: Automated PC add-on car converter. Translates YFT meshes, downsamples/converts YTD textures, generates PS4-safe `vehicles.meta`, `carcols.meta`, `carvariations.meta`, patches audio hashes, and packages into a clean `dlc.rpf`.
* **`auto_convert_vegetation.py`**: Converts PC vegetation mods (e.g., LWE - Medium Density Vegetation). Strips unsupported PC shader attributes, normalizes tree drawables, and creates compatible LOD layouts.
* **`auto_convert_roads.py`**: Converts PC road overhaul mods and texture packs.
* **`ps4_mod_studio.py`**: All-in-one graphical converter utility and mod manager.
* **`build_fh6_svj_sound.py`**: Synthesizes and packages custom vehicle engine audio and backfire samples into PS4 AWC containers.
* **`ps4_awc_crypt.c`**: High-performance C tool for decrypting and encrypting PS4 GTA V audio archives using the proprietary XXTEA key schedule.
* **`build_nve_renew.py` & `build_all_nve_dlcs.py`**: Converts and packages authentic NaturalVision Evolved graphics, clouds (`v_clouds.rpf`), lights, and water for PS5.
* **`sync_nve_renew_to_ps5.py`**: Fast live uploader to sync timecycles, HBAO, and visual settings directly to the console over HTTP / FTP.
* **`apply_streetlight_fixes.py`**: Tunes night streetlight coronas, volumetric fog cones, and ambient lighting for realistic darkness.

---

## License & Credits
* Developed for PS5 homebrew research and Grand Theft Auto V modding.
* Special thanks to the PlayStation 5 homebrew community, ps5-payload-sdk, and the Orbx engine contributors.
