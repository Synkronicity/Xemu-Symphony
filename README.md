# Xemu Symphony

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](#license--attribution)
[![Release: General Release 2](https://img.shields.io/badge/Release-release2-brightgreen.svg)](#roadmap--release-history)
[![Audio: True 5.1 Surround & 3D Headphones](https://img.shields.io/badge/Audio-5.1%20LPCM%20%7C%203D%20Binaural%20HRTF-orange.svg)](#quick-start-pick-your-setup)
[![Architecture: MCPX APU DSP56362](https://img.shields.io/badge/Hardware-MCPX%20APU%20LLE%20%2B%20HLE-green.svg)](#under-the-hood-for-developers--preservationists)

**Xemu Symphony** is a hardware-accurate audio-focused fork of [Xemu](https://xemu.app), an original Xbox emulator built on QEMU, named for the Motorola Symphony family of digital signal processors (DSPs).

Xbox emulation has historically struggled with the console's custom NVIDIA MCPX Audio Processing Unit (APU) and its dual Motorola DSP56362s. Even Microsoft's first party emulator (Xenon Fusion, aka XeFu) has issues with audio, especially progressive desynchronization during cutscenes. Upstream Xemu is even worse, resorting to stubbing out the Encode Processor (`DSP1`) entirely just to get emulation working at all.

The consequences of that go far beyond just broken or missing audio. Many games will refuse to launch or if they do they will randomly crash, issues that can be attributed to an incomplete implementation of the APU. Want discrete surround sound, the one killer feature the Xbox had that none of its contemporaries did? Too bad, upstream Xemu won't work if your EEPROM is set to anything other than "Stereo". Recent dubious architectural decisions about how to approach the Global Processor (`DSP0`) make it clear there are no audio engineers working on Xemu.

So that's where I come in. **Xemu Symphony is the _complete APU_** as it existed on the original hardware. After 25 years we finally have _silicon-accurate, full-pipeline_ low level emulation (LLE) of this critical subsystem. **But that's not all!** If you are running a low power device, like a handheld console, I made sure to also devise a **fully-featured high level emulation (HLE)** rendition of the APU that gives you all the same features at *only 3% of the CPU cost of the LLE option.*

---

## Why Use Xemu Symphony Instead of Standard Xemu?

* **True 5.1 Surround Sound Out of the Box:**
  Standard Xemu cannot output discrete 5.1 surround sound. Xemu Symphony includes a built-in, clean-room open-source replacement for the Xbox's proprietary Encode Processor P-ROM (`symphony_ep_rom.h`) and ATSC A/52 factory Y-ROM (`ep_yrom.h`) that taps ***uncompressed, lossless 6-channel floating-point audio*** directly from the console's mixer and sends it straight to your Windows/Linux audio endpoint with zero compression loss and under 5 ms latency. ***Absolutely no Dolby Digital (AC-3) encoding required!*** After 25 years now you can experience your favorite games as if the S/PDIF optical output was HDMI all along!

* **Built-In 3D Surround Sound for Regular Stereo Headphones:**
  Playing on normal stereo headphones or IEMs? You don't need a dedicated sound card or complicated third party software (like HeSuVi + EqualizerAPO + VB-Cable) to hear enemies behind you in *Halo 2*. Xemu Symphony features a built-in **5.1-to-2.0 Binaural 3D HRTF Downmixer** (`hle_binaural.h`) that converts true 6-channel Xbox surround sound into 360-degree spatial headphone audio in real time.

* **Fixes Long-Standing Xbox Audio Bugs (Static, Ringing, Channel Mismatching, Missing SFX+BGM):**
  Resolves core Motorola DSP56362, DMA, and Voice Processor bugs that have plagued upstream Xemu for years. You see this [list of games with audio issues](https://github.com/xemu-project/xemu/issues/566)? They're all fixed now on my fork. How about this [other list of even worse issues](https://github.com/xemu-project/xemu/issues/380)? That's right, I fixed them. [This also isn't an issue on Symphony](https://github.com/xemu-project/xemu/issues/332). I guarantee you if your game is still broken it's not because of the APU...

* **Curated x87 FPU+SSE & GPU Performance Fixes Included:**
  ...and that's because Symphony also includes five surgical, community-vetted upstream PRs that [resolve guest x87 80-bit floating-point precision loss](https://github.com/xemu-project/xemu/issues/3097) (which previously caused missing audio in titles like [*The Lord of the Rings: The Third Age*](https://github.com/xemu-project/xemu/issues/2541)) alongside lock-free NV2A GPU fence polling and native S3TC compressed texture uploads. Thanks Korze Warrior (`e52132eb71`), EqUiNox (`5a6b9db1a9`), Tovarichtch (`385706396e` + `8f7b43699d`), and whoever among you had Claude send this PR up: `d6f0cc1e9f`. For whatever reason, upstream Xemu is just sitting on these free fixes.

* **One-Click Audio Setup (No Xbox Dashboard Required):**
  You don't need to boot into the dashboard or use the online EEPROM configurator just to turn on 5.1 surround sound. Flip your virtual Xbox's system EEPROM between Stereo (2.0) and Surround (5.1 / Dolby Digital) with one click inside Settings -> Audio.

* **Fast "HLE Reverb + GP Heartbeat" Mode for Steam Deck, Other Handhelds & Older PCs:**
  Emulating two 100 MHz Motorola DSP chips at 100% cycle accuracy takes CPU power. If you're playing on a handheld or lower-spec CPU, uncheck `Hardware DSP Effects` to activate Symphony's HLE Freeverb engine (`hle_freeverb.h`). A built-in hybrid GP handshake and 1-in-32 subframe heartbeat keeps game audio drivers synchronized with zero freezes at ~3% of normal DSP CPU cost without losing out on reverb and surround sound unlike disabling the DSP on upstream Xemu!

---

## Quick Start: Pick Your Setup

Open Xemu Symphony, go to **Settings -> Audio**, and match these dropdowns to what you're listening on:

| What You're Using | Listening Device | Xbox System Audio Mode (EEPROM) | What You Get |
| :--- | :--- | :--- | :--- |
| **Stereo Headphones / IEMs**<br>*(Recommended for most headphone users)* | `Headphones (Stereo Only)` | `Surround (5.1)` | **3D Binaural Virtual Surround:** Converts all 6 Xbox surround channels into 3D spatial headphone audio. |
| **5.1 Home Theater (AVR), 5.1 Soundbar, or 5.1 Sound Card (SBX/HeSuVi)** | `Speakers (Including AVR / 5.1 DAC / VSS)` | `Surround (5.1)` | **Lossless 6-Channel 5.1 LPCM:** Sends discrete Front L/R, Center, Subwoofer, and Rear L/R directly to your system. |
| **Desktop Stereo Speakers / TV Speakers** | `Speakers (Including AVR / 5.1 DAC / VSS)` | `Stereo (2.0)` | **Authentic 2001 Speaker Stereo:** Original Xbox stereo mix with Sensaura front-speaker spatial widening. |
| **Stereo Headphones (Pure 2.0 Mode)**<br>*(For games with native headphone mixes like Doom 3)* | `Headphones (Stereo Only)` | `Stereo (2.0)` | **Pure Uncolored Stereo:** Clean 2-channel stereo with absolutely no speaker cross-talk cancellation (XTC). |

> **Note:** Whenever you change **Xbox System Audio Mode (EEPROM)**, Xemu Symphony automatically updates your EEPROM file (`0x98`) and recalculates its 32-bit one's-complement checksum (`0x60`). Restart the title once for the new EEPROM setting to take effect. Switching between **Speakers** and **Headphones** or toggling **Real-Time DSP (LLE vs. HLE)** works **live in-game** without restarting!

### When Should I Use LLE Mode vs. HLE Mode?
* **LLE Mode (`Hardware DSP Effects` ON):** Runs authentic uploadable Motorola DSP56362 Global Processor microcode. Recommended for recent desktop CPUs and may be required for certain games that route sound effects exclusively through GP effect sends.
* **HLE Mode (`Hardware DSP Effects` OFF):** Replaces heavy GP DSP execution with Symphony's lightweight C-based Freeverb engine (`hle_freeverb.h`) and unlocks sliders to customize *Reverb Room Size*, *High-Frequency Damping*, and *Wet Level* on the fly. Ideal for Steam Deck and handhelds. Due to the variability of how different games use the DSP you will need to adjust the strength of the reverb effect to your liking. Some games will need that drastically lowered (platformers like Conker should have *Reverb Wet Level = 5%*) while others, like racing games, will want that to be slightly punchier (try 55%/28%/35% respectively).

---

## Comparison: Standard Xemu vs. Xemu Symphony

| Feature | Standard Xemu | **Xemu Symphony (`release2`)** |
| :--- | :--- | :--- |
| **5.1 Surround Sound** | Not supported | **Lossless 6-Channel LPCM out of the box** |
| **External Audio Firmware (`dolby_ep.bin`)** | N/A | **Not required** (Built-in open-source `symphony_ep_rom.h`) |
| **3D Headphone Surround** | Inaccurate stereo HRTF gating | **Unconditional 4-Corner VP 3D Submix + 5.1-to-2.0 Binaural Convolver** |
| **LLE I3DL2 Circular DMA Delay Lines** | Byte-shears on `2 mod 4` wraps (static in *WWE Raw 2*, *Whiplash*, and *Tomb Raider*) | **Hardware-accurate `~(item_size - 1)` word alignment (zero static)** |
| **GP Mixbuffer (`X:$1400-$17FF`) Handling** | Scrapes GP scratch bins into surround channels | **Isolates FL/FR master returns (`b < 2`) with complementary headroom** |
| **Enabling Surround in Xbox EEPROM** | Requires booting into Xbox Dashboard | **One-click toggle inside Settings -> Audio** |
| **Low-Spec / Handheld Audio Mode** | Disabling DSP strips reverb or hangs games | **Fast HLE Freeverb + Hybrid GP Handshake & 1-in-32 Heartbeat** |
| **DSP Engine Architecture** | "DSPOps" Rust JIT backend / C frontend | **Pure C99 Core with 32-Byte L1 Pre-Decoded IR Cache (`dsp_decoded_op_t`)** |

---

## Under the Hood (For Developers & Preservationists)

Full technical documentation and emulator-agnostic porting guide available in [`docs/MCPX_SYMPHONY_ARCHITECTURE_AND_PORTING.md`](docs/MCPX_SYMPHONY_ARCHITECTURE_AND_PORTING.md).

### 1. The MCPX APU Signal Pipeline

The original Xbox's NVIDIA MCPX southbridge Audio Processing Unit (APU) processes 32-sample subframes at 48,000 Hz (`666.67 us` per subframe, 8 subframes / 256 samples per `5.33 ms` Encode Processor frame) across three hardware stages:
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
             ▼ (Hardware DSP Effects = ON: LLE)                    ▼ (Hardware DSP Effects = OFF: HLE)
┌─────────────────────────────┐                       ┌─────────────────────────────┐
│  Global Processor (GP DSP)  │                       │   HLE Studio Reverb Engine  │
│  Motorola DSP56362 Core +   │                       │  4-Tap I3DL2 Early Refl +   │
│  Word-Aligned Circular DMA  │                       │ 8-Comb / 4-AllPass Freeverb │
│  + Front-Only Delta (b < 2) │                       │   + 1-in-32 GP Heartbeat    │
└────────────┬────────────────┘                       └────────────┬────────────────┘
             └──────────────────────────┬──────────────────────────┘
                                        ▼
                      6-Channel Internal Float Frame (ch6[0..5])
                        [FL, FR, FC, LFE, RL, RR @ 48 kHz]
                                        │
         ┌──────────────────────────────┼──────────────────────────────┐
         ▼                              ▼                              ▼
┌─────────────────────┐      ┌─────────────────────┐      ┌─────────────────────┐
│ Discrete 6-Ch LPCM  │      │ 5.1 -> 2.0 Binaural │      │Vintage Pro Logic II │
│ via SDL3 AudioStream│      │ HRTF Convolver or   │      │Stereo Matrix or 5.1 │  
│                     │      │ Standard Stereo     │      │Dolby Digital Encoder│
└─────────────────────┘      └─────────────────────┘      └─────────────────────┘
```

### 2. Key Hardware Reverse-Engineering Breakthroughs

* **Word-Aligned Circular Scratch DMA (`dsp_dma.c`):** DirectSound I3DL2 reverb microcode configures 8 parallel 32-bit (`format = 2`, `item_size = 4` bytes) circular delay lines (`buf_id = 0xE`) in system RAM with `2 mod 4` byte lengths (e.g., `0x31AC2`, `0x3220A`, `0x3104E`, `0x12FD6`, `0x118D6`) and unaligned base addresses. Unmasked byte wrapping split 4-byte samples (`2 bytes + 2 bytes`) across wrap boundaries every ~1.06 seconds, shifting the writeback pointer by 16 bits and reading adjacent sample LSBs into the 24-bit DSP sign byte. Enforcing hardware `~(item_size - 1)` alignment eliminates LLE static completely.
* **GP Mixbuffer Scratch Isolation (`gp_ep.c`):** `X:0x1400..0x17FF` is the VP-to-GP input mixbuffer. During execution, DirectSound GP microcode writes wet stereo returns into `X:0x1400..0x143F` (`b = 0..1`, `FL/FR`) and reuses `X:0x1440..0x14BF` (`b = 2..5`, `FC, LFE, RL, RR`) as temporary IIR filter scratch RAM. Isolating LLE GP delta extraction (`gp_fx = gp_ch - mixbins`) to `b < 2` with complementary headroom scaling (`0.35f` wet / `0.65f` 3D submix) prevents scratch data from corrupting LFE and surround channels.
* **Unconditional VP 3D HRTF Submix Routing (`vp.c`):** On Xbox silicon, the Voice Processor's 3D HRTF unit (`v < 64 && !stereo && hrtf_handle != 0xFFFF`) is an internal 4-quadrant hardware spatializer (`d->vp.hrtf_submix[0..3]`) that is always active regardless of whether the host output is headphones or 5.1 speakers. Decoupling `use_hrtf` in `vp.c` from the host headphone toggle restores rear-channel 3D voices and prevents dry 3D voice overdrive in 6-channel discrete mode.
* **The Open-Source `symphony_ep_rom.h` Micro-Kernel:** Implements a clean-room 584-word resident EP executive (`P:0x0000` and `P:0x0180`) that maintains 100% hardware register, mailbox, and DMA compatibility (including the Bit-14 `0x4000` DMA zero-generator hole) while yielding via `WAIT` (`0x000086`) so 6-channel float PCM can be tapped losslessly. 
* For the sake of preservation, those that possess the original 7,264-word retail `dolby_ep.bin` proprietary EP P-ROM can make use of Symphony's included cleanly generated `ep_yrom.h` that provides the complete 2,048-word ATSC A/52A factory Y-Data ROM (`Y:0x0800..0x0FFF`) built from public domain math, allowing you to encode Xemu Symphony's discrete 5.1 surround sound into authentic Dolby Digital (AC-3) output to a decoder of your choice. Only for historians, those curious about how the Xbox originally sounded in 2001, and deeply sick people that like Dolby's psychoacoustic modeling and are willing to lose performance and sound quality for it. I additionally also offer, again for historical preservation purposes, the ability to encode Dolby Pro Logic II for academics and the morbidly curious since only the truly depraved would willingly subject themselves to this when you have so many better options to pick from.
* **And so much more as detailed in [docs/MCPX_SYMPHONY_ARCHITECTURE_AND_PORTING.md](docs/MCPX_SYMPHONY_ARCHITECTURE_AND_PORTING.md) as well as an upcoming developer diary highlighting the entire journey from academic curiosity to digital archaeology and reverse engineering one of the [last missing ROMs in the Xbox's 25 year history!](https://archive.org/details/dolby_ep)** Stay tuned and keep checking back as I'm a slow writer when I want to write the story right.

---

## Curated Non-Audio Fixes Included in Mainline (`release2`)

While Xemu Symphony's development is strictly dedicated to the MCPX APU and Motorola DSP56362 audio subsystem, certain upstream Xbox emulation bugs outside the APU directly manifest as "broken audio" (when corrupted x87 FPU math zeroes out 3D voice distance/attenuation in guest `dsound` drivers) or cause severe frame-time stalls.

Mainline `release2` integrates **only** the following five vetted community commits outside the audio stack:
1. `target/i386: Preserve x87 load precision` (*Korze Warrior*) — Fixes 80-bit x87 load precision loss that caused missing audio streams in titles like *The Lord of the Rings: The Third Age*.
2. `Translator: end the block after fldcw only when the precision bit changes; stop using pc-relative blocks` (*Community PR*) — Eliminates translation block thrashing on guest FPU control-word toggles.
3. `i386/tcg: stub rdpmc to return 0 instead of raising #UD` (*EqUiNoX*) — Prevents guest crashes on performance-counter reads.
4. `nv2a: Add lock-free fast path for fence polling reads` (*Tovarichtch*) — Eliminates Big QEMU Lock (BQL) contention during GPU fence polling.
5. `nv2a/gl: upload S3TC textures without decompressing them` (*Tovarichtch*) — Avoids CPU-side S3TC texture decompression stalls.

---

## Building from Source

### Prerequisites
* **OS:** Windows 10/11 (64-bit), Linux (x86_64 / ARM64), or macOS
* **Toolchain:** GCC or Clang supporting ISO C11 and C++17
* **Build Environment (Windows):** MSYS2 MinGW-w64 (`MINGW64` shell)

### Compile & Package
    cd xemu
    ./build.sh

The compiled executable will be generated at `./build/qemu-system-i386w.exe` (and packaged in `./dist/xemu.exe`).

---

## Roadmap & Release History

* [x] **Alpha 1 – Alpha 4:** Initial 6-channel surround tap, EEPROM audio parser, and Motorola DSP56362 24-bit AGU / stack / loop accuracy fixes.
* [x] **Beta 1 – Beta 5:** Clean-room ATSC A/52 `ep_yrom.h` generation, open-source `symphony_ep_rom.h` micro-kernel (zero external ROM dependencies), and 32-byte pre-decoded DSP IR cache.
* [x] **Beta 6 – Beta 8:** 2:2:2 Audio Matrix, 5.1-to-2.0 Binaural HRTF headphone downmixer, HLE Freeverb studio reverb engine, hybrid GP heartbeat, and VP LUT/SGE/ADPCM caching.
* [x] **Beta 9 & General Release 1 (`release2` = Current):** Word-aligned circular scratch DMA (`dsp_dma.c`), GP mixbuffer scratch isolation (`b < 2`), unconditional VP 3D HRTF submix routing, curated x87/NV2A fixes, and portable developer documentation.
* [ ] **To Do:** 1. Build and continuously integrate macOS releases. 2. Finish documentation. 3. Improve the Audio Settings menu by replacing the text description box at the bottom with vector graphics depicting the various options and endpoints. 4. Figure out how to seamlessly translate said menu for non-English speakers.


>This fork is downstream of Xemu and uses automated continuous integration on GitHub to automatically pull, merge, and build from any new updates in upstream `master` that do not affect my custom MCPX ACI/AC'97/APU codebase. The rolling Canary Build release (`canary-rolling`) will always be current with the latest standard Xemu updates. The Continuous Build release (`continuous`) will usually contain minor hotfixes and tweaks that do not warrant a full release until I feel like an arbitrary threshold of worthwhile changes has been met. It may not always be current with upstream Xemu like Canary is.

---

## AI Disclosure

Feel free to read all about it [over here](AI_DISCLOSURE_LOG.md), I definitely have some thoughts about this to share now that it's all said and done. Suffice to say this whole project actually started ***because*** an AI told me it wasn't technically possible. So I did it out of spite anyways.

Right off the bat I'll just have you know I had an LLM squirt out the release notes earlier in development (and prior iterations of this readme) because otherwise there wouldn't be anything written because I'm too "in my head" as a writer to get that done without missing important details and getting distracted, especially since I was in the middle of a long development sprint. It just wasn't worth the effort of accidentally getting it wrong. ***Now*** that it's done I'm definitely spending that effort on my own writing so stay tuned for more human-written documentation to come.

---

## License & Attribution

This project is licensed under the **GNU General Public License v2.0 (GPLv2)**.

* **Xemu Symphony APU Architecture, `symphony_ep_rom.h` Micro-Kernel, HLE Freeverb & Binaural HRTF Engines:** Copyright (c) 2026 Will Bonnett (Dual-licensed under MIT or GPL-2.0-or-later)
* **Xemu Emulator & Base MCPX Implementation:** Copyright (c) 2020-2026 Matt Borgerson, espes, Jannik Vogel, and Xemu contributors
* **Motorola DSP56001 Interpreter Foundation:** Copyright (c) 2001-2008 ARAnyM developer team, Thomas Huth
* **ATSC A/52 Mathematical Constants (`ep_yrom.h`):** Public Domain / ATSC Standard A/52A

---

## Special Thanks

#### A huge thanks to all the humans who helped me with this research project turned first-time solo open source development release:

* @Triticum0 For the impressive amount of work you put into studiously testing a laundry list of games and noting all of the edge cases and upstream correlations for me to be aware of. You, as well as my other testers, have helped me shave off probably like a month or two of extra work hunting all of that down myself. No way I would have been able to rapidly iterate releases without that immediate feedback.

---

* @raddad772 Even though you've been working on Emusyne for way longer I do feel a sense of camaraderie that we both started working on our respective Xbox emulators around the same time. It was impressive seeing you go from "lol funny shapez" to an emulator that already decimates its competition. I can't wait to work on it myself so that my work can survive Xemu's incoming extinction event. Also also for these two commits you produced that I cleanly integrated into Symphony:

> 1) mcpx-apu: Don't abort on out-of-range GP/EP scatter-gather access
> 2) apu/vp: Run voices in order when multipass departs from DirectSound

---
* @Mainkill1 Thanks for the several late night conversations to keep me going through yet another all-nighter. It's always a pleasure to hang with other night owls. And of course for your meticulous project management over your own Xemu fork which produced for me these four commits that I cleanly integrated into Symphony:

> 1) True 1-Channel Mono Voice Resampling & Reset for vp/resample.h, vp/vp.c, and vp/vp.h
> 2) Decoded ADPCM Block Cache for vp/adpcm.h, vp/vp.c, and vp/vp.h
> 3) SGE Descriptor RAM Mapping Cache (Lockless Hit Path) for vp/sge.h and vp/vp.c
> 4) Safe DSP Quiesce Before Snapshot Load for apu.c

---

* The Emulator Development (r/emudev) Discord server and all of the impressively smart emudevs and other individuals I met who are definitely doing some awe inspiring work that by comparison I can only claim to be in the same ~~room~~ server as them and nothing more. Big thanks to Adamillo for also always being there to test my builds and to push me towards coming up with performant HLE solutions that moved the state of the art for this fork. Also shout out to Lycoder for always stopping by the otherwise sometimes lonely #xbox channel to check on how we're doing lol

---

* And obviously best for last is my wife, but not just for being my wife but also because as a professional musician she had the right kind of trained ears to help A/B test my work without my biases and confirm for me what one is truly hearing. I swear you will never have to watch that Halo 2 sound test video ever again or walk through the opening level of Doom 3 for the 19th time.