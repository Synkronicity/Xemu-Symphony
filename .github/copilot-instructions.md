# Repository Guidelines: Xemu Symphony (`release3`)

Xemu Symphony is a hardware-accurate, audio-focused downstream fork of Xemu implementing a complete pure-C99/C11 NVIDIA MCPX Audio Processing Unit (VP, GP, EP, DSP DMA), Intel ICH AC'97 Audio Codec Interface (ACI), and a 2:2:2 LLE/HLE audio pipeline (discrete 5.1 LPCM, 5.1-to-2.0 Binaural HRTF, HLE Freeverb, and opt-in Pro Logic II / retail AC-3).

Before reviewing or modifying audio code, read `AGENTS.md` and `docs/MCPX_SYMPHONY_ARCHITECTURE_AND_PORTING.md` as the canonical hardware ground truth.

## Language, Dependencies & Build System
- **Emulation Core:** Strict ISO C99/C11 across `hw/xbox/mcpx/apu/`, `hw/xbox/mcpx/aci.c`, and `hw/audio/ac97.c`. Never introduce C++, Rust toolchains, FFI bridges, runtime JIT emitters, or `libsamplerate` into the emulation core.
- **UI Layer:** C++17 is restricted exclusively to `ui/xui/`.
- **Build System:** Meson and Ninja orchestrated via `./build.sh` (or `./build.sh -p win64-cross` under Docker). Never suggest Makefiles or CMake for Xemu builds.
- **Hygiene:** Must compile cleanly with zero warnings under GCC and Clang (`-Wall -Wextra -Werror=implicit-function-declaration`), Unix LF line endings, and `clang-format` compliance. Do not modify or strip copyright / MIT-or-GPLv2 dual-license headers.

## Non-Negotiable MCPX & DSP56362 Invariants
- **Voice Processor (`hw/xbox/mcpx/apu/vp/`):**
  - Uses a standalone, zero-allocation pure-C99 8-point Lagrange resampler (`resample.h`, 257-row quantized table) with zero `libsamplerate` dependency.
  - Keep hardware 3D HRTF routing (`allow_3d && v < MCPX_HW_MAX_3D_VOICES && !stereo && hrtf_handle != HRTF_NULL_HANDLE`) decoupled from host headphone settings (`g_config.audio.hrtf`), with `allow_3d = false` for `NV_PAPU_TVL2D` and `true` for `TVL3D`/`TVLMP`.
  - Never call host audio APIs inside guest vCPU MMIO handlers (`fe_method`); treat null or OOB SGE/SSL descriptors as clean silence.
- **Motorola DSP56362 Core (`hw/xbox/mcpx/apu/dsp/`):**
  - `dsp->c_core` is the live executing core; `dsp->core` is strictly a VMState save/load snapshot.
  - Memory sizes: 32,768-word P-RAM (`0x00CACACA` 24-bit masked poison), 65,536-word zero-init X-RAM (`X:$1400–$17FF` mixbuffer mirrored at `X:$0C00–$0FFF`), 2,048-word zero-init Y-RAM, plus EP read-only factory Y-ROM (`Y:$0800–$0FFF`, ATSC A/52A `ep_yrom.h`). Peripherals (`$FFFF80–$FFFFFF`) are dual-mapped in X and Y spaces.
  - Preserve 24-bit masking (`& 0x00FFFFFF`), asymmetric `SSH`/`SSL` stack semantics, `DO`/`DOR` `LC == 0` bypass, `DO FOREVER` (`SR[FV]`), `emu_macr` accumulate-before-round order, lazy 32-byte L1-aligned IR opcache (`dsp_decoded_op_t`) with 2-word write invalidation (`address` and `address - 1`), and finite `DO` loop (`SR[LF] && !SR[FV]`) exemption in spin-wait yielding.
- **GP / EP Mixing & Micro-Kernel (`gp_ep.c`, `symphony_ep_rom.h`):**
  - Default EP firmware is the built-in clean-room 584-word `symphony_ep_rom.h` micro-kernel (35–50 cycles/subframe). Never make proprietary `dolby_ep.bin` or `liba52` mandatory.
  - Restrict LLE GP wet-delta extraction (`gp_fx = gp_ch - mixbins`) strictly to `b < 2` (`FL`, `FR`) scaled by `0.35f`, paired with `0.65f` 4-corner VP HRTF submix gain (`1.0f` in HLE). Never scrape `X:$1440–$14BF` (`b = 2..5`) back into `FC, LFE, RL, RR`.
  - Apply the IEEE-754 `double` 4-section cascaded 120 Hz elliptic LFE lowpass (`ep_yrom[0..19]`) to `ch6[i][3]` in 5.1 / Binaural modes before output routing while leaving optional retail AC-3 raw feeds untouched.
  - Keep HLE mode's hybrid GP handshake gate active (`GP_MAX_SUBFRAME_CYCLES = 100000`, 64-subframe reset arm, 8-subframe MMIO re-arm, 1-in-32 heartbeat).
- **DSP DMA (`dsp_dma.c`) & AC'97 ACI (`aci.c`, `ac97.c`, `xbox_pci.c`):**
  - Enforce `~(item_size - 1)` word alignment on circular scratch DMA (`buf_id = 0xE`) and Bit-14 (`0x4000`) zero-generator aperture handling.
  - Preserve LPC IRQ 6 routing (`0x68`/`0x6C`, `PCI_INTERRUPT_LINE = 6`), sparse `SO_INDEX` (`7`) level-triggered IRQ aggregation in `update_sr()`, `NABM_POLVI`/`SOLVI` re-arming when `civ == lvi`, standalone sequencer-off AC'97 draining, and additive `FL`/`FR` summing in `monitor.c`.
  - EEPROM audio flags live at `0x98` (fallback `0x9C`) with a 32-bit one's-complement checksum at `0x60` over `0x64..0xBF`.

## Style & Review References
- Code style: `/docs/devel/style.rst`
- Full invariants & checklists: `/AGENTS.md` and `/CONTRIBUTING.md`
- Architecture & external porting guide: `/docs/MCPX_SYMPHONY_ARCHITECTURE_AND_PORTING.md`