# Xemu Symphony

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](#license--attribution)
[![Release: General Release 1](https://img.shields.io/badge/Release-release1-brightgreen.svg)](#roadmap--release-history)
[![Audio: True 5.1 Surround & 3D Headphones](https://img.shields.io/badge/Audio-5.1%20LPCM%20%7C%203D%20Binaural%20HRTF-orange.svg)](#quick-start-pick-your-setup)
[![Architecture: MCPX APU DSP56362](https://img.shields.io/badge/Hardware-MCPX%20APU%20LLE%20%2B%20HLE-green.svg)](#under-the-hood-for-developers--preservationists)

**Xemu Symphony** is a hardware-accurate audio and preservation fork of the [Xemu](https://xemu.app) Original Xbox emulator.

Standard Xbox emulation has historically struggled with the console's custom NVIDIA MCPX Audio Processing Unit (APU) and its dual Motorola DSP56362 digital signal processors—forcing players to choose between missing surround sound, loud static/tinnitus bugs in Low-Level Emulation (LLE), silent sound effects, or complicated external workarounds. **Xemu Symphony fixes Original Xbox audio out of the box.**

---

## Why Use Xemu Symphony Instead of Standard Xemu?

* **True 5.1 Surround Sound Out of the Box (Zero Extra ROM Files Needed):**
  Standard Xemu cannot output discrete 5.1 surround sound. Xemu Symphony includes a built-in, clean-room open-source replacement for the Xbox's internal Encode Processor (`symphony_ep_rom.h`) and ATSC A/52 factory Y-ROM (`ep_yrom.h`) that taps **uncompressed, lossless 6-channel floating-point audio** directly from the console's mixer and sends it straight to your Windows/Linux audio device with zero compression loss and under 5 ms latency.

* **Built-In 3D Surround Sound for Regular Stereo Headphones:**
  Playing on normal stereo headphones or IEMs? You don't need a dedicated sound card or third-party virtual audio cables (like HeSuVi or EqualizerAPO) to hear enemies behind you in *Halo 2*. Xemu Symphony features a built-in **5.1-to-2.0 Binaural 3D HRTF Downmixer** (`hle_binaural.h`) that converts true 6-channel Xbox surround sound into 360-degree spatial headphone audio in real time.

* **Fixes Long-Standing Xbox Audio Bugs (Static, Tinnitus & Missing SFX):**
  Resolves core Motorola DSP56362, DMA, and Voice Processor bugs that have plagued upstream Xemu for years—including rhythmic white-noise static and missing ring/environment sound effects in *WWE Raw 2* and *Whiplash*, missing commentary and arena reverb in EA SoundSys games (*Knockout Kings 2002*, *FIFA*), master-bus ducking in *Quantum Redshift*, and streaming crashes in *RPM Tuning*.

* **One-Click Surround Setup (No Xbox Dashboard Required):**
  You don't need to track down an Xbox Dashboard disc or hex-edit files just to turn on 5.1 surround sound. Flip your virtual Xbox's system EEPROM between **Stereo (2.0)**, **Dolby Surround (Lt/Rt)**, and **Surround (5.1)** with one click inside **Settings -> Audio**.

* **Fast "HLE Reverb + GP Heartbeat" Mode for Steam Deck, Handhelds & Older PCs:**
  Emulating two 100 MHz Motorola DSP chips at 100% cycle accuracy takes CPU power. If you're playing on a handheld or lower-spec CPU, uncheck **Enable Real-Time DSP** to activate our **HLE Studio Reverb** engine (`hle_freeverb.h`). A built-in hybrid GP handshake and 1-in-32 subframe heartbeat keeps game audio drivers synchronized with zero freezes at ~3% of normal DSP CPU cost.

* **Curated x87 FPU & GPU Performance Fixes Included:**
  Includes five surgical, community-vetted upstream PRs that resolve guest x87 80-bit floating-point precision loss (which previously caused missing audio in titles like *The Lord of the Rings: The Third Age*) alongside lock-free NV2A GPU fence polling and native S3TC compressed texture uploads.

---

## Quick Start: Pick Your Setup

Open Xemu Symphony, go to **Settings -> Audio**, and match these dropdowns to what you're listening on:

| What You're Using | Listening Device | Xbox System Audio Mode (EEPROM) | What You Get |
| :--- | :--- | :--- | :--- |
| **Stereo Headphones / IEMs**<br>*(Recommended for most headphone users)* | `Headphones (Stereo Only)` | `Surround (5.1)` | **3D Binaural Virtual Surround:** Converts all 6 Xbox surround channels into 3D spatial headphone audio. |
| **5.1 Home Theater (AVR), 5.1 Soundbar, or 5.1 Sound Card (SBX/HeSuVi)** | `Speakers (Including AVR / 5.1 DAC / VSS)` | `Surround (5.1)` | **Lossless 6-Channel 5.1 LPCM:** Sends discrete Front L/R, Center, Subwoofer, and Rear L/R directly to your system. |
| **Desktop Stereo Speakers / TV Speakers** | `Speakers (Including AVR / 5.1 DAC / VSS)` | `Stereo (2.0)` | **Authentic 2001 Speaker Stereo:** Original Xbox stereo mix with Sensaura front-speaker spatial widening. |
| **Stereo Headphones (Pure 2.0 Mode)**<br>*(For games with native headphone mixes like Doom 3)* | `Headphones (Stereo Only)` | `Stereo (2.0)` | **Pure Uncolored Stereo:** Clean 2-channel stereo with speaker cross-talk cancellation (XTC) bypassed. |

> **Note:** Whenever you change **Xbox System Audio Mode (EEPROM)**, Xemu Symphony automatically updates your EEPROM file (`0x98`) and recalculates its 32-bit one's-complement checksum (`0x60`). Restart the title once for the new EEPROM setting to take effect. Switching between **Speakers** and **Headphones** or toggling **Real-Time DSP (LLE vs. HLE)** works **live in-game** without restarting!

### When Should I Use LLE Mode vs. HLE Mode?
* **LLE Mode (`Enable Real-Time DSP` ON):** Runs authentic downloadable Motorola DSP56362 Global Processor microcode. Recommended on desktop CPUs and required for games that route sound effects or custom arena acoustics exclusively through GP effect sends (*WWE Raw 2*, *Whiplash*, *Quantum Redshift*, *Knockout Kings 2002*).
* **HLE Mode (`Enable Real-Time DSP` OFF):** Replaces heavy GP DSP execution with our lightweight C Freeverb engine (`hle_freeverb.h`) and unlocks sliders to customize *Reverb Room Size*, *High-Frequency Damping*, and *Wet Level* on the fly. Ideal for Steam Deck and handhelds.

---

## Comparison: Standard Xemu vs. Xemu Symphony

| Feature | Standard Xemu | **Xemu Symphony (`release1`)** |
| :--- | :--- | :--- |
| **5.1 Surround Sound** | Not supported | **Lossless 6-Channel 32-bit Float LPCM out of the box** |
| **External Audio Firmware (`dolby_ep.bin`)** | N/A | **Not required** (Built-in open-source `symphony_ep_rom.h` + `ep_yrom.h`) |
| **3D Headphone Surround** | Inaccurate stereo HRTF gating | **Unconditional 4-Corner VP 3D Submix + 5.1-to-2.0 Binaural Convolver** |
| **LLE I3DL2 Circular DMA Delay Lines** | Byte-shears on `2 mod 4` wraps (static in *WWE Raw 2* / *Whiplash*) | **Hardware-accurate `~(item_size - 1)` word alignment (zero static)** |
| **GP Mixbuffer (`X:$1400-$17FF`) Handling** | Scrapes GP scratch bins into surround channels | **Isolates FL/FR master returns (`b < 2`) with complementary headroom** |
| **Enabling Surround in Xbox EEPROM** | Requires booting into Xbox Dashboard | **One-click toggle inside Settings -> Audio** |
| **Low-Spec / Handheld Audio Mode** | Disabling DSP strips reverb or hangs games | **Fast HLE Freeverb + Hybrid GP Handshake & 1-in-32 Heartbeat** |
| **DSP Engine Architecture** | Rust JIT / unoptimized C fallback | **Pure C11 Core with 32-Byte L1 Pre-Decoded IR Cache (`dsp_decoded_op_t`)** |

---

## Under the Hood (For Developers & Preservationists)

Full technical documentation and our emulator-agnostic porting specification are available in [`docs/MCPX_SYMPHONY_ARCHITECTURE_AND_PORTING.md`](docs/MCPX_SYMPHONY_ARCHITECTURE_AND_PORTING.md).

### 1. The MCPX APU Signal Pipeline

The Original Xbox's NVIDIA MCPX Southbridge Audio Processing Unit (APU) processes 32-sample subframes at 48,000 Hz (`666.67 us` per subframe, 8 subframes / 256 samples per `5.33 ms` Encode Processor frame) across three hardware stages:
1. **Voice Processor (VP — `hw/xbox/mcpx/apu/vp/vp.c`):** Hardware synthesizer that decodes up to 256 voices (PCM / Xbox IMA ADPCM), applies per-voice SVF filters and 31-tap HRIR 3D voice convolution (`d->vp.hrtf_submix[0..3]`), and accumulates audio into **32 internal Mixbins** (`mixbins[0..31]`).
2. **Global Processor (GP — First Motorola DSP56362 Core):** Executes game-uploaded DirectSound effects microcode (I3DL2 environmental reverberation, chorus, flange, and 2D stereo submixing at `X:0x1400..0x143F`).
3. **Encode Processor (EP — Second Motorola DSP56362 Core):** Coordinates the 6-channel surround frame clock, GP-to-EP Scratch RAM DMA rings, and output framing, backed by a 2,048-word factory Y-Data ROM (`Y:0x0800..0x0FFF`).

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                         Xemu Symphony MCPX APU Pipeline                      │
└───────────────────────────────────────┬──────────────────────────────────────┘
                                        │
Voice Processor (VP: 256 Voices -> 32 Mixbins) + Unconditional 4-Corner 3D HRTF Submix (0..3)
                                        │
             ┌──────────────────────────┴──────────────────────────┐
             ▼ (Real-Time DSP = ON: LLE)                             ▼ (Real-Time DSP = OFF: HLE)
┌─────────────────────────────┐                       ┌─────────────────────────────┐
│  Global Processor (GP DSP)  │                       │   HLE Studio Reverb Engine  │
│  Motorola DSP56362 Core +   │                       │  4-Tap I3DL2 Early Refl +   │
│  Word-Aligned Circular DMA  |                       | 8-Comb / 4-AllPass Freeverb │
|  + Front-Only Delta (b < 2) │                       │   + 1-in-32 GP Heartbeat    |
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

### 2. Key Hardware Reverse-Engineering Breakthroughs

* **Word-Aligned Circular Scratch DMA (`dsp_dma.c`):** DirectSound I3DL2 reverb microcode configures 8 parallel 32-bit (`format = 2`, `item_size = 4` bytes) circular delay lines (`buf_id = 0xE`) in system RAM with `2 mod 4` byte lengths (e.g., `0x31AC2`, `0x3220A`, `0x3104E`, `0x12FD6`, `0x118D6`) and unaligned base addresses. Unmasked byte wrapping split 4-byte samples (`2 bytes + 2 bytes`) across wrap boundaries every ~1.06 seconds, shifting the writeback pointer by 16 bits and reading adjacent sample LSBs into the 24-bit DSP sign byte. Enforcing hardware `~(item_size - 1)` alignment eliminates LLE static completely.
* **GP Mixbuffer Scratch Isolation (`gp_ep.c`):** `X:0x1400..0x17FF` is the VP-to-GP input mixbuffer. During execution, DirectSound GP microcode writes wet stereo returns into `X:0x1400..0x143F` (`b = 0..1`, `FL/FR`) and reuses `X:0x1440..0x14BF` (`b = 2..5`, `FC, LFE, RL, RR`) as temporary IIR filter scratch RAM. Isolating LLE GP delta extraction (`gp_fx = gp_ch - mixbins`) to `b < 2` with complementary headroom scaling (`0.35f` wet / `0.65f` 3D submix) prevents scratch data from corrupting LFE and surround channels.
* **Unconditional VP 3D HRTF Submix Routing (`vp.c`):** On Xbox silicon, the Voice Processor's 3D HRTF unit (`v < 64 && !stereo && hrtf_handle != 0xFFFF`) is an internal 4-quadrant hardware spatializer (`d->vp.hrtf_submix[0..3]`) that is always active regardless of whether the host output is headphones or 5.1 speakers. Decoupling `use_hrtf` in `vp.c` from the host headphone toggle restores rear-channel 3D voices and prevents dry 3D voice overdrive in 6-channel discrete mode.
* **The Open-Source `symphony_ep_rom.h` Micro-Kernel & `ep_yrom.h`:** Implements a clean-room 584-word resident EP executive (`P:0x0000` and `P:0x0180`) that maintains 100% hardware register, mailbox, and DMA compatibility (including the Bit-14 `0x4000` DMA zero-generator hole) while yielding via `WAIT` (`0x000086`) so 6-channel float PCM can be tapped losslessly. For preservationists running an external 7,264-word retail `dolby_ep.bin`, `ep_yrom.h` provides the complete 2,048-word ATSC A/52A factory Y-Data ROM (`Y:0x0800..0x0FFF`).

---

## Curated Non-Audio Fixes Included in Mainline (`release1`)

While Xemu Symphony's development is strictly dedicated to the MCPX APU and Motorola DSP56362 audio subsystem, certain upstream Xbox emulation bugs outside the APU directly manifest as "broken audio" (when corrupted x87 FPU math zeroes out 3D voice distance/attenuation in guest `dsound` drivers) or cause severe frame-time stalls.

Mainline `release1` integrates **only** the following five vetted community commits outside the audio stack:
1. `target/i386: Preserve x87 load precision` (*Korze Warrior*) — Fixes 80-bit x87 load precision loss that caused missing audio streams in titles like *The Lord of the Rings: The Third Age*.
2. `Translator: end the block after fldcw only when the precision bit changes; stop using pc-relative blocks` (*Community PR*) — Eliminates translation block thrashing on guest FPU control-word toggles.
3. `i386/tcg: stub rdpmc to return 0 instead of raising #UD` (*EqUiNoX*) — Prevents guest crashes on performance-counter reads.
4. `nv2a: Add lock-free fast path for fence polling reads` (*Nick Spargo / Tovarichtch*) — Eliminates Big QEMU Lock (BQL) contention during GPU fence polling.
5. `nv2a/gl: upload S3TC textures without decompressing them` (*Nick Spargo / Tovarichtch*) — Avoids CPU-side S3TC texture decompression stalls.

---

## Building from Source

### Prerequisites
* **OS:** Windows 10/11 (64-bit), Linux (x86_64 / ARM64), or macOS
* **Toolchain:** GCC or Clang supporting ISO C11 and C++17
* **Build Environment (Windows):** MSYS2 MinGW-w64 (`MINGW64` shell)

### Compile & Package
    cd /z/xemu
    ./build.sh

The compiled executable will be generated at `./build/qemu-system-i386w.exe` (and packaged in `./dist/xemu.exe`).

---

## Roadmap & Release History

* [x] **Alpha 1 – Alpha 4:** Initial 6-channel surround tap, EEPROM audio parser, and Motorola DSP56362 24-bit AGU / stack / loop accuracy fixes.
* [x] **Beta 1 – Beta 5:** Clean-room ATSC A/52 `ep_yrom.h` generator, open-source `symphony_ep_rom.h` micro-kernel (zero external ROM dependencies), and 32-byte pre-decoded DSP IR cache.
* [x] **Beta 6 – Beta 8:** 2:2:2 Audio Matrix, 5.1-to-2.0 Binaural HRTF headphone downmixer, HLE Freeverb studio reverb engine, hybrid GP heartbeat, and VP LUT/SGE/ADPCM caching.
* [x] **Beta 9 & General Release 1 (`release1` — Current):** Word-aligned circular scratch DMA (`dsp_dma.c`), GP mixbuffer scratch isolation (`b < 2`), unconditional VP 3D HRTF submix routing, curated x87/NV2A fixes, and portable developer documentation.

---

## AI Disclosure

This project utilizes iterative AI-assisted engineering for forensic disassembly analysis, live hardware telemetry verification, and cross-platform C/C++ implementation. Every architectural change is deterministically verified against Motorola DSP56300/DSP56362 silicon documentation and retail Xbox hardware behavior.

---

## License & Attribution

This project is licensed under the **GNU General Public License v2.0 (GPLv2)**.

* **Xemu Symphony APU Architecture, `symphony_ep_rom.h` Micro-Kernel, HLE Freeverb & Binaural HRTF Engines:** Copyright (c) 2026 Will Bonnett
* **Xemu Emulator & Base MCPX Implementation:** Copyright (c) 2020-2026 Matt Borgerson, espes, Jannik Vogel, and Xemu contributors
* **Motorola DSP56001 Interpreter Foundation:** Copyright (c) 2001-2008 ARAnyM developer team, Thomas Huth
* **ATSC A/52 Mathematical Constants (`ep_yrom.h`):** Public Domain / ATSC Standard A/52A