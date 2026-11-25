# Xemu Symphony — Technical Documentation & Architecture

**General Release 3 (`release3`)** — Hardware-accurate pure-C99 emulation of the Original Xbox NVIDIA MCPX Audio Processing Unit (VP, GP, EP, DSP DMA) and Intel ICH AC'97 Audio Codec Interface (ACI), delivering lossless 6-channel 5.1 LPCM, 5.1-to-2.0 Binaural HRTF headphone surround, and a ~3% CPU HLE Freeverb mode with zero proprietary firmware or `libsamplerate` dependencies.

---

## Core Specifications (`release3`)

* **Voice Processor (`vp.c`):** 256 hardware voices, standalone pure-C99 8-point Lagrange resampler (257-row quantized table, zero `libsamplerate`), 2D-list handle `0..63` guard, unconditional 4-corner 31-tap HRIR 3D spatializer, per-voice ADPCM/SGE caching, and fail-safe null-descriptor handling.
* **Dual Motorola DSP56362 Cores (`dsp/`):** Pure ISO C99/C11 with 32-byte L1-aligned lazy pre-decoded IR cache (`dsp_decoded_op_t`), native 64-bit Q23×Q23→Q47 arithmetic, 32K P-RAM / 64K zero-init X-RAM / 2K zero-init Y-RAM, asymmetric `SSH`/`SSL` stack semantics, and zero Rust/JIT dependencies.
* **Encode Processor & LFE Conditioning:** Built-in clean-room 584-word `symphony_ep_rom.h` micro-kernel (35–50 cycles/subframe, >99.6% reduction), public-domain ATSC A/52A factory Y-ROM (`ep_yrom.h`, `Y:$0800–$0FFF`), and IEEE-754 `double` 8th-order 120 Hz elliptic LFE lowpass.
* **2:2:2 Audio Matrix:** Live switching between LLE DSP56362 and HLE Freeverb (`hle_freeverb.h` + hybrid GP 1-in-32 heartbeat), Discrete 5.1 LPCM vs. 5.1-to-2.0 Binaural HRTF (`hle_binaural.h`) vs. 2.0 Stereo, plus opt-in Pro Logic II `Lt/Rt` and retail `dolby_ep.bin` AC-3 preservation modes.
* **AC'97 ACI (`aci.c`, `ac97.c`):** LPC IRQ 6 silicon defaults, sparse `SO_INDEX` (7) level-triggered IRQ aggregation, single-descriptor `civ == lvi` LVI re-arming, standalone sequencer-off draining, and additive `FL`/`FR` monitor summing.

---

## Documentation & Navigation

* [Downloads & Releases (`release3`)](https://github.com/Synkronicity/Xemu-Symphony/releases)
* [MCPX APU & DSP56362 Technical Specification and Porting Guide](https://github.com/Synkronicity/Xemu-Symphony/blob/master/docs/MCPX_SYMPHONY_ARCHITECTURE_AND_PORTING.md)
* [README & 2:2:2 Quick Start Matrix](https://github.com/Synkronicity/Xemu-Symphony/blob/master/README.md)
* [AI Disclosure & Engineering Methodology](https://github.com/Synkronicity/Xemu-Symphony/blob/master/AI_DISCLOSURE_LOG.md)
* [DSP56300 Forensics Corpus](https://github.com/Synkronicity/Xemu-Symphony/tree/master/dsp56300-corpus)
* [Architectural Invariants & Agent Directives (`AGENTS.md`)](https://github.com/Synkronicity/Xemu-Symphony/blob/master/AGENTS.md)
* [Contribution Guidelines (`CONTRIBUTING.md`)](https://github.com/Synkronicity/Xemu-Symphony/blob/master/CONTRIBUTING.md)
* [Developer Style Guide](devel/style.rst)
* [Code of Conduct](devel/code-of-conduct.rst)

---

## Key Silicon Invariants

1. **GP Mixbuffer Scratch Isolation (`gp_ep.c`):** Restricts LLE wet-delta extraction (`gp_fx = gp_ch - mixbins`) strictly to `b < 2` (`FL`/`FR`) at `0.35f` with `0.65f` 4-corner VP HRTF submix gain, blocking I3DL2 scratch data in `X:$1440–$14BF` (`b = 2..5`) from corrupting Center, LFE, and Rear channels.
2. **Word-Aligned Circular Scratch DMA (`dsp_dma.c`):** Enforces `~(item_size - 1)` alignment across base, offset, and size for circular scratch descriptors (`buf_id = 0xE`) and models the Bit-14 (`0x4000`) zero-generator aperture.
3. **Live EEPROM Checksum (`monitor.c`):** Reads/writes Xbox audio mode at offset `0x98` (fallback `0x9C`) and recalculates the 32-bit one's-complement checksum at `0x60` across `0x64..0xBF`.