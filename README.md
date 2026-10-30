# Xemu Symphony

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](#license--attribution)
[![Audio: True 5.1 Surround & 3D Headphones](https://img.shields.io/badge/Audio-5.1%20LPCM%20%7C%203D%20Binaural%20HRTF-orange.svg)](#quick-start-pick-your-setup)
[![Architecture: MCPX APU DSP56362](https://img.shields.io/badge/Hardware-MCPX%20APU%20LLE%20%2B%20HLE-green.svg)](#under-the-hood-for-developers--preservationists)

**Xemu Symphony** is a high-fidelity audio and performance fork of the [Xemu](https://xemu.app) Original Xbox emulator. 

Standard Xbox emulation has historically struggled with the console's custom NVIDIA MCPX audio chip—forcing players to choose between broken/missing surround sound, audio stutters, or complicated external workarounds. **Xemu Symphony fixes Xbox audio out of the box.**

---

## Why Use Xemu Symphony Instead of Standard Xemu?

* **True 5.1 Surround Sound Out of the Box (Zero Extra Files Needed):**
  Standard Xemu cannot output discrete 5.1 surround sound without complex S/PDIF workarounds. Xemu Symphony includes a built-in, open-source replacement for the Xbox's internal Encode Processor (`symphony_ep_rom.h`) that taps **uncompressed, lossless 6-channel audio** directly from the console's mixer and sends it straight to your Windows/Linux audio device with zero compression loss and <5 ms latency.

* **Built-In 3D Surround Sound for Regular Stereo Headphones:**
  Playing on normal stereo headphones or earbuds? You don't need a $300 sound card or complicated third-party tools (like HeSuVi, EqualizerAPO, or Virtual Audio Cable) to hear enemies behind you in *Halo 2*. Xemu Symphony features a built-in **Binaural 3D HRTF Downmixer** that converts true 6-channel Xbox surround sound into 360-degree spatial headphone audio in real time.

* **One-Click Surround Setup (No Xbox Dashboard Required):**
  You don't need to track down an Xbox Dashboard disc or hex-edit files just to turn on 5.1 surround sound. You can flip your virtual Xbox's system EEPROM between **Stereo (2.0)** and **Surround (5.1)** with one click right inside **Settings → Audio**.

* **Fast "HLE Reverb" Mode for Steam Deck, Handhelds & Older PCs:**
  Emulating the Xbox's dual 100 MHz DSP audio chips at 100% hardware accuracy takes CPU power. If you're playing on a handheld or a lower-end CPU, you can flip **Hardware DSP Effects** to **OFF** to activate our fast **HLE Studio Reverb** engine—giving you full 3D positioning, 5.1 surround, and lush environmental reverb with virtually zero CPU cost.

* **No More Surround Audio Stutters or Cutscene Freezes:**
  Fixes long-standing AC'97 digital audio (`SO_INDEX`) DMA bugs and DSP timing stalls that caused games to stutter, drop dialogue, or hang during cutscenes when surround sound was enabled.

---

## Quick Start: Pick Your Setup

Open Xemu Symphony, go to **Settings → Audio**, and match these two dropdowns to what you're listening on:

| What You're Using | Listening Device | Xbox System Audio Mode (EEPROM) | What You Get |
| :--- | :--- | :--- | :--- |
| **Stereo Headphones / IEMs**<br>*(Recommended for most headphone users)* | `Headphones (Stereo Only)` | `Surround (5.1)` | **3D Binaural Virtual Surround:** Converts all 6 Xbox surround channels into 3D spatial headphone audio. |
| **5.1 Home Theater (AVR), 5.1 Soundbar, or 5.1 Sound Card (SBX/HeSuVi)** | `Speakers (Including AVR / 5.1 DAC / VSS)` | `Surround (5.1)` | **Lossless 6-Channel 5.1 LPCM:** Sends discrete Front L/R, Center, Subwoofer, and Rear L/R directly to your system. |
| **Desktop Stereo Speakers / TV Speakers** | `Speakers (Including AVR / 5.1 DAC / VSS)` | `Stereo (2.0)` | **Authentic 2001 Speaker Stereo:** Original Xbox stereo mix with Sensaura front-speaker spatial widening. |
| **Stereo Headphones (Pure 2.0 Mode)**<br>*(For games with native headphone mixes like Doom 3)* | `Headphones (Stereo Only)` | `Stereo (2.0)` | **Pure Uncolored Stereo:** Clean 2-channel stereo with speaker cross-talk cancellation (XTC) bypassed. |

> **Note:** Whenever you change **Xbox System Audio Mode (EEPROM)**, Xemu Symphony automatically updates your EEPROM file and recalculates its security checksum. Simply restart the emulator once for the new EEPROM setting to take effect. Switching between **Speakers** and **Headphones** or toggling **Hardware DSP Effects** works **live in-game** without restarting!

### Need More FPS on a Handheld or Older CPU?
In **Settings → Audio**, turn **Hardware DSP Effects (High Accuracy)** **OFF**. This switches from cycle-accurate Motorola DSP emulation (LLE) to our lightweight **HLE Freeverb** engine, and unlocks sliders to customize your *Reverb Room Size*, *High-Frequency Damping*, and *Wet Level* on the fly.

---

## Comparison: Standard Xemu vs. Xemu Symphony

| Feature | Standard Xemu | **Xemu Symphony** |
| :--- | :--- | :--- |
| **5.1 Surround Sound** | Not supported | **Lossless 6-Channel 32-bit Float LPCM out of the box** |
| **External Audio Firmware (`dolby_ep.bin`)** | N/A | **Not required** (Built-in open-source `symphony_ep_rom.h`) |
| **3D Headphone Surround** | Inaccurate HRTF stereo downmixing | **Built-in Discrete 5.1-to-2.0 Binaural HRTF Convolver** |
| **Enabling Surround in Xbox EEPROM** | Requires booting into Xbox Dashboard | **One-click toggle inside Settings → Audio** |
| **Low-Spec / Handheld Audio Mode** | Disabling DSP strips all game reverb | **Fast HLE Freeverb + I3DL2 Early Reflections Engine** |
| **Output Latency** | ~30–50 ms | **< 5 ms (Real-time subframe pacing)** |
| **DSP Engine Portability** | Rust JIT Interpreter (Stereo only) | **Optimized Pure C Interpreter (LLE + HLE, x86_64 & ARM64 ready)** |

---

## Under the Hood (For Developers & Preservationists)

### 1. The MCPX APU Signal Pipeline

The Original Xbox's NVIDIA MCPX Southbridge Audio Processing Unit (APU) consists of three hardware stages:
1. **Voice Processor (VP — `hw/xbox/mcpx/apu/vp/vp.c`):** Hardware synthesizer that decodes up to 256 voices (PCM / Xbox ADPCM), applies 31-tap HRIR 3D voice filtering, and accumulates audio into **32 internal Mixbins** (`mixbins[0..31]`).
2. **Global Processor (GP — First Motorola DSP56362 Core):** Executes game-uploaded DirectSound effects microcode (I3DL2 environmental reverberation, chorus, flange, and 2D stereo submixing at `X:0x1400` / `X:0x1420`).
3. **Encode Processor (EP — Second Motorola DSP56362 Core):** Coordinates the 6-channel surround frame clock, GP-to-EP Scratch RAM DMA rings, and output framing.

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                         Xemu Symphony MCPX APU Pipeline                      │
└───────────────────────────────────────┬──────────────────────────────────────┘
                                        │
                  Voice Processor (VP: 256 Voices -> 32 Mixbins)
                                        │
             ┌──────────────────────────┴──────────────────────────┐
             ▼ (DSP Effects = ON: LLE)                             ▼ (DSP Effects = OFF: HLE)
┌─────────────────────────────┐                       ┌─────────────────────────────┐
│  Global Processor (GP DSP)  │                       │   HLE Studio Reverb Engine  │
│  Motorola DSP56362 Core +   │                       │  4-Tap I3DL2 Early Refl +   │
│  Symphony EP Micro-Kernel   │                       │  8-Comb / 4-AllPass Freeverb│
└────────────┬────────────────┘                       └────────────┬────────────────┘
             └──────────────────────────┬──────────────────────────┘
                                        ▼
                      6-Channel Internal Float Frame (ch6[0..5])
                        [FL, FR, FC, LFE, RL, RR @ 48 kHz]
                                        │
         ┌──────────────────────────────┼──────────────────────────────┐
         ▼                              ▼                              ▼
┌─────────────────────┐      ┌─────────────────────┐      ┌─────────────────────┐
│ 5.1 Speakers / AVR  │      │ Stereo Headphones   │      │ Vintage Pro Logic   │
│ Discrete 6-Ch LPCM  │      │ 5.1->2.0 Binaural   │      │ 90° All-Pass Lt/Rt  │
│ via SDL3 AudioStream│      │ HRTF Convolver      │      │ Matrix Encoder      │
└─────────────────────┘      └─────────────────────┘      └─────────────────────┘
```

### 2. The Open-Source `symphony_ep_rom.h` Micro-Kernel

On physical 2001 Xbox hardware, the only way to transport 6 channels of audio out of the console was over a single optical S/PDIF cable (capped at 1.5 Mbps), forcing the Encode Processor (EP) to spend ~98% of its 100 MHz clock budget compressing 6-channel PCM into a lossy Dolby Digital (AC-3) bitstream.

Through deterministic disassembly and execution tracing of the 7,264-word factory EP ROM, we isolated the two resident executive segments (`Segment 1` at `P:0x0000..0x00C7` and `Segment 2` at `P:0x0180..0x02FF`) from the six relocatable ATSC A/52 compression overlays (`Overlays #1..#6` dynamically DMA-loaded at `P:0x0300`). 

Because Xemu Symphony taps the 6 discrete channels losslessly in 32-bit floating-point PCM, **`symphony_ep_rom.h`** implements a clean-room 584-word resident micro-kernel that maintains 100% hardware register, status mailbox (`P:0x0001..0x0007`), and DMA syscall (`P:0x0180`) compatibility while short-circuiting the AC-3 compression overlays and yielding via opcode `WAIT` (`0x000086`) in ~20 cycles per subframe.

### 3. Advanced Historical Preservation Modes (Pro Logic Lt/Rt & AC-3 BYOL)

For hardware purists and preservationists, **Settings → Audio → Advanced Settings** provides two legacy 2001 hardware modes:
* **Dolby Surround (Pro Logic Lt/Rt Matrix):** Downmixes the 6 discrete surround channels into a 2-channel matrix-encoded stereo stream ($L_t / R_t$) using $90^\circ$ all-pass phase shifting (`hle_prologic_ltrt_downmix_subframe`) for decoding on vintage analog Dolby Pro Logic receivers.
* **Dolby Digital 5.1 (Bring Your Own License — `dolby_ep.bin`):** Users who wish to run the original 7,264-word Motorola AC-3 encoding microcode for hardware preservation can enable this toggle and select their own `dolby_ep.bin` dump (available for archival research on the [Internet Archive](https://archive.org/details/dolby_ep)).

### 4. Discrete 5.1 Channel Ordering

When **Listening Device** is set to **Speakers** with a **Surround (5.1)** EEPROM, SDL3 outputs standard 6-channel uncompressed LPCM:

| Channel Index | Identifier | Speaker Destination |
| :---: | :--- | :--- |
| **0** | Front Left (`FL`) | Left Front Satellite (3D + 2D Stereo Bus) |
| **1** | Front Right (`FR`) | Right Front Satellite (3D + 2D Stereo Bus) |
| **2** | Front Center (`FC`) | Center Dialogue Channel |
| **3** | Low-Frequency Effects (`LFE`) | Subwoofer (< 120 Hz) |
| **4** | Surround Left (`SL`) | Left Rear Surround |
| **5** | Surround Right (`SR`) | Right Rear Surround |

---

## Building from Source

### Prerequisites
* **OS:** Windows 10/11 (64-bit), Linux (x86_64 / ARM64), or macOS
* **Toolchain:** GCC or Clang supporting ISO C11 and C++17
* **Build Environment (Windows):** MSYS2 MinGW-w64 (`MINGW64` shell)

### Compile & Package
```bash
cd /[drive letter]/xemu
./build.sh
```
The compiled executable and required runtime DLLs will be packaged in `./dist/xemu.exe`.

---

## Roadmap

* [x] **Beta 5:** Clean-room open-source `symphony_ep_rom.h` micro-kernel (zero external ROM dependencies) & DSP56362 C interpreter accuracy fixes.
* [x] **Beta 6 (Current Release Candidate):** Built-in 5.1-to-2.0 Binaural HRTF headphone downmixer, HLE Freeverb studio reverb engine, one-click EEPROM audio switcher, and redesigned Audio UI.
* [ ] **macOS Universal Binaries:** Automated Apple Silicon / Intel macOS release packaging.

---

## AI Disclosure

This project utilizes iterative AI-assisted engineering for forensic disassembly analysis, hardware trace verification, and cross-platform C/C++ implementation. Every architectural change is deterministically verified against Motorola DSP56300/DSP56362 silicon documentation and live retail Xbox game testing.

---

## License & Attribution

This project is licensed under the **GNU General Public License v2.0 (GPLv2)**.

* **Xemu Symphony APU Architecture, `symphony_ep_rom.h` Micro-Kernel, HLE Freeverb & Binaural HRTF Engines:** Copyright (c) 2026 Will Bonnett
* **Xemu Emulator & Base MCPX Implementation:** Copyright (c) 2020-2026 Matt Borgerson, espes, Jannik Vogel, and Xemu contributors
* **Motorola DSP56001 Interpreter Foundation:** Copyright (c) 2001-2008 ARAnyM developer team, Thomas Huth
* **ATSC A/52 Mathematical Constants (`ep_yrom.h`):** Public Domain / ATSC Standard A/52A