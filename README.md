# Xemu Motorola DSP56362 (EP) Low-Level Emulation (LLE) Audio Subsystem

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](#license--attribution)
[![Architecture: Original Xbox MCPX APU](https://img.shields.io/badge/Hardware-MCPX%20APU%20DSP56362-green.svg)](#system-architecture)
[![Audio Backend: SDL3 5.1 Multi-Channel](https://img.shields.io/badge/Audio-SDL3%205.1%20Surround-orange.svg)](#audio-channel-ordering)

A cycle-accurate, pure-C Low-Level Emulation (LLE) implementation of the **Motorola Symphony DSP56362 Encoding Processor (EP)** co-processor for the [Xemu](https://xemu.app) Original Xbox emulator.

This fork restores authentic, hardware-accelerated 6-channel DirectSound3D surround sound directly to modern audio interfaces with zero compression loss and zero latency.

---

## The Short Version: Why Use This Fork?

If you have a 5.1 surround sound system, an AV receiver, or spatial audio headphones, this fork provides the definitive Xbox audio experience:

*   **Pristine Lossless Audio:** We extract uncompressed 6-channel 32-bit floating-point audio directly from the console's internal DSP mixer stages. You hear the clean, uncompressed game audio before it gets mangled by 2001-era lossy compression.
    
*   **Sub-Millisecond Audio Latency:** Traditional emulation pipes encode audio into an AC-3 bitstream and decode it back on your PC, adding 30–50 ms of lag. This fork taps audio in real time (< 5 ms).
    
*   **Improved Performance:** Eliminates the audio thread hangs, periodic micro-stutters, and 250 ms `dsound.sys` driver timeout watchdog stalls present when running without an active EP.
    
*   **Balanced 2D/3D Soundstage:** Dialogue, stereo soundtrack music, and pre-rendered FMV cutscenes are additively summed into the Front-Left and Front-Right satellite channels while keeping positional 3D sound effects localized across the center, surround, and subwoofer channels.

---

## Quick Start: 3-Step Setup

1.  **Acquire the DSP Firmware:** Obtain `dolby_ep.bin` from your physical console or the [Internet Archive](https://archive.org/details/dolby_ep).
    
2.  **Configure the ROM in GUI:** Launch Xemu, navigate to **Settings → System → Files** (or **Settings → Audio**), and select your `dolby_ep.bin` in the **DSP EP ROM** file picker.
    
3.  **Set Xbox Dashboard to Surround:** Ensure your virtual Xbox EEPROM is set to **Dolby Digital** in the original Xbox Dashboard audio settings.

---

## Architectural Deep Dive

### The Hardware Topology

The original Xbox MCPX Audio Processing Unit (APU) integrates two separate DSP cores derived from the 24-bit Motorola DSP56300 family:

1.  **Global Processor (GP)**: Handles voice processing, environmental reverberation, dynamic range compression, and 2D master front mixing.
    
2.  **Encode Processor (EP) - Motorola DSP56362**: A dedicated audio co-processor running at 100 MHz, designed to perform real-time ATSC A/52 matrixing and multi-channel encoding.

---

## System Architecture

```
┌────────────────────────────────────────────────────────────────────────┐
│                        Xemu MCPX APU Subsystem                         │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
    ┌───────────────────────────────┴───────────────────────────────┐
    ▼                                                               ▼
┌───────────────────────┐                               ┌───────────────────────┐
│  Voice Processor (VP) │                               │ Global Processor (GP) │
│  64 3D Voices + HRTF  ├───────────── Mixbins 0..5 ───►│ Master Effects & Mix  │
└───────────────────────┘                               └───────────┬───────────┘
                                                                    │
                                                  Multichannel DMA Stride ($004000)
                                                                    │
                                                                    ▼
                                                        ┌───────────────────────┐
                                                        │  Encode Processor     │
                                                        │  (EP / Motorola       │
                                                        │   DSP56362 LLE Core)  │
                                                        │  - Factory Y-ROM      │
                                                        │  - Bit-14 Zero-Sink   │
                                                        └───────────┬───────────┘
                                                                    │
                                                  Discrete 5.1 LPCM (L, R, C, LFE, LS, RS)
                                                                    │
                                                                    ▼
                                                        ┌───────────────────────┐
                                                        │    SDL3 AudioStream   │
                                                        │  (WASAPI / Direct)    │
                                                        └───────────────────────┘
```

### Full-Silicon Memory Mapping

The Motorola DSP56362 Harvard architecture splits memory across three independent 24-bit spaces:

*   **Program RAM (P-RAM, `0x0000`–`0x7FFF`):** Backed by 32,768 words (0x8000) to fully mirror the 64 KB `NV_PAPU_EPPMEM` hardware aperture. Segmented microcode loads across `P:0x0000` (Vector Table), `P:0x0180` (Transform Kernel), and `P:0x0300` (Encoder Loop).
    
*   **On-Chip Factory Y-ROM (`Y:0x0800`–`Y:0x0FFF`):** Silicon-accurate integration of the 2,048-word factory-masked data ROM (`ep_yrom.h`). Contains mathematical constants from the public ATSC A/52 specification: 33 biquad filter sections, Kaiser-Bessel Derived (KBD) MDCT window tables, twiddle factors, bit allocation matrices, and CRC-16 generators. Read-only; core writes are silently dropped.
    
*   **Internal Peripheral Space (`$FFFFC0`–`$FFFFDF`):** Models the Symphony HDI08 host interface (`HCR`, `HSR`, `HPCR`, `HBAR`, `HORX`, `HOTX`), ESSI serial interfaces, and the internal DSP DMA controller.
    
*   **The Bit-14 Zero-Generator Hole:** Internal DSP DMA transactions addressing bit 14 (`addr & 0x4000`) target an unmapped silicon aperture. Reads return `0x000000` and writes are discarded, allowing resident microcode command `$0c` to generate IEC 61937 stuffing bursts without consuming internal memory.

## Comparison: Lossless Direct PCM Tap vs. Lossy S/PDIF Round-Trip

| Feature | Upstream S/PDIF Round-Trip | `Xemu-Symphony` (This Fork) |
| :---: | :--- | :--- |
| **Audio Pipeline** | DSP encodes lossy AC-3 $\rightarrow$ Virtual S/PDIF $\rightarrow$ Host CPU decodes via `liba52` | **Lossless Direct Tap:** Discrete 6-channel PCM tapped directly from mixer RAM |
| **Audio Fidelity** | Lossy 640 kbps compression artifacts | **100% Mathematical Precision (32-bit Float)** |
| **Output Latency** | ~32–50 ms (Packet aggregation + host ring buffer) | **< 5 ms (Real-time subframe pacing)** |
| **DSP Core Engine** | Rust JIT compiler (x86_64 host-locked) | **Pure C Software Interpreter (Architecture-Agnostic)** |
| **Dependencies** | Requires Rust toolchain, Cargo, and liba52 | **Zero external language dependencies (Strict C99/C11)** |
| **Host Portability** | Limited by JIT backend emitters | Compiles anywhere GCC/Clang runs (x86_64, ARM64, Apple Silicon) |

---

## Building

### Prerequisites

*   **Operating System**: Windows 10/11 (64-bit), Linux (x86\_64 / ARM64), or macOS
    
*   **Toolchain**: GCC or Clang supporting C11 / C++17
    
*   **Build System**: Meson and Ninja (MSYS2 MINGW64 recommended on Windows)

### Emulator Compilation

Launch the **MSYS2 MINGW64** shell and execute:

```bash
cd /z/xemu
./build.sh
```

The compiled binary will be generated at `./dist/xemu.exe`.

### Standalone Microcode Verification Harness Build

To compile the standalone verification utility without building the entire emulator:

```bash
gcc -O2 -s -o tools/ep_harness tools/bench_ep.c
```

---

### Firmware & Digital Preservation

The Motorola DSP56362 firmware (`dolby_ep.bin`) is factory microcode burned directly into silicon at chip fabrication. In keeping with software preservation principles, this repository does not distribute proprietary binary firmware.

*   **Digital Library Archive:** The microcode asset, accompanied by hardware bus traces, ep_harness registers, and  Motorola family manuals, is preserved for historical research at the [Internet Archive DSP56362 Collection](https://archive.org/details/dolby_ep).
    
*   **Asset Loader:** Configure the firmware path permanently via the Xemu GUI or directly in `xemu.toml`:
    
    Ini, TOML
    
    ```
    [sys.files]
    ep_rom_path = "C:/Software/Xemu/dolby_ep.bin"
    ```

### Validating Your Firmware Dump

Run the standalone verification utility to verify the file topology and hardware reset vector:

```bash
./ep_harness.exe dolby_ep.bin
```

**Expected Output for Authentic Microcode**:
```text
Motorola DSP56362 Microcode Verification Utility
[+] Successfully loaded 3804 bytes (951 24-bit words) from: tools/dolby_ep.bin
[+] Firmware Topology Analysis:
    |- Vector Table Length : 0x00C8 words (Destination: P:0x0000)
    |- Transform Kernel    : 0x017F words (Destination: P:0x0180)
    \- Data Tables / Tail  : 0x01AE words (Destination: P:0x0300)
[+] Hardware Reset Vector : 0x050C08
[+] Status: Validated authentic Xbox Dolby Digital AC-3 interactive encoding microcode.
```

---

## Audio Channel Ordering

The internal monitor stream outputs audio in standard discrete DirectSound 5.1 surround ordering:

| Index | Channel Identifier | Speaker Destination |
| :---: | :--- | :--- |
| **0** | Front Left (`FL`) | Left Front Satellite |
| **1** | Front Right (`FR`) | Right Front Satellite |
| **2** | Center (`FC`) | Center Dialogue Channel |
| **3** | Low-Frequency Effects (`LFE`) | Subwoofer |
| **4** | Surround Left (`SL`) | Left Rear Surround |
| **5** | Surround Right (`SR`) | Right Rear Surround |


---

## Roadmap
1.  **Beta 2 Release (Current):** Stabilize pure-C Motorola DSP56362 LLE execution across retail library titles.
    
2.  **Dynamic Range Management:** Implement soft-knee saturation curves across multi-channel apertures to prevent clipping on aggressive 0 dBFS Bink cutscene audio.
    
3.  **Cross-Platform Release Builds:** Automated deployment of macOS universal binaries via GitHub Actions.

---

# AI Disclosure
This project leverages iterative artificial intelligence tooling for structural scaffolding, silicon regression analysis, and cross-compilation validation. Complete technical audits and retrospective methodologies are cataloged in the disclosure log.

---

## License & Attribution

This project is licensed under the **GNU General Public License v2.0 (GPLv2)** to maintain full compatibility with upstream QEMU and Xemu.

*   **Motorola DSP56362 LLE Subsystem & Pure C Engine**: Copyright (c) 2026 Will Bonnett
    
*   **Xemu APU / Emulator Architecture**: Copyright (c) 2020-2026 Matt Borgerson, espes, and Xemu contributors
    
*   **Motorola DSP56300 Interp Core Basis**: Copyright (c) 2001-2008 ARAnyM developer team, Thomas Huth
    
*   **ATSC A/52 Tables**: Public Domain / Mathematical Constants under ATSC Standard A/52A
