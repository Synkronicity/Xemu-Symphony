# Operational Directives for Autonomous Agents & AI Assistants

This repository (`Xemu Symphony`) is a downstream fork of Xemu implementing a complete, hardware-accurate pure-C99/C11 MCPX Audio Processing Unit (VP, GP, EP, DSP DMA) and Intel ICH AC'97 Audio Codec Interface (ACI), alongside a 2:2:2 LLE/HLE audio output matrix.

**Mandatory Pre-Read:** Before proposing changes to `hw/xbox/mcpx/`, `hw/audio/ac97.c`, or `hw/xbox/xbox_pci.c`, read `docs/MCPX_SYMPHONY_ARCHITECTURE_AND_PORTING.md`. Treat that specification as the canonical hardware ground truth.

---

## 1. Architectural Invariants (Hard Non-Negotiables)

### Core Language & State Boundaries
* **Pure ISO C Emulation Core:** All emulation translation units (`hw/xbox/mcpx/apu/`, `hw/xbox/mcpx/aci.c`, `hw/audio/ac97.c`) must remain strict ISO C99/C11. Never introduce Rust toolchains, FFI layers, runtime JIT emitters, or C++ constructs into the emulation core. C++17 is restricted exclusively to UI layers (`ui/xui/`).
* **Live Core vs. Snapshot State:** In `struct DSPState`, `dsp->c_core` (`dsp_core_t *`) is the live executing DSP56362 instance. `dsp->core` (`DspCoreState`) is a passive VMState save/load snapshot synced only via `dsp_sync_to_vm()` / `dsp_sync_from_vm()`. Always route live reads/writes through the `dsp_*` API or `dsp->c_core`.
* **Default Lossless LPCM Pipeline:** Internal 6-channel mixing runs in 32-bit float (`ch6[32][6]`: `0=FL, 1=FR, 2=FC, 3=LFE, 4=RL, 5=RR` at 48 kHz) and outputs directly to host audio (`surround_buf[256][6]` or `frame_buf[256][2]`). Opt-in preservation modes (user-supplied `dolby_ep.bin` AC-3 or Pro Logic II `Lt/Rt`) must never become dependencies of the default path, and never introduce `liba52` or virtual S/PDIF round-trips.

### Voice Processor (`hw/xbox/mcpx/apu/vp/`)
* **Unconditional 3D HRTF Routing:** Hardware 3D voice spatialization (`use_hrtf = (v < MCPX_HW_MAX_3D_VOICES) && !stereo && (hrtf_handle != HRTF_NULL_HANDLE)`) must NEVER be gated on host headphone settings (`g_config.audio.hrtf`). Voices `0..63` unconditionally route into `d->vp.hrtf_submix[0..3]` via `(b >> 1)` hemisphere indexing.
* **Pure Hardware PIO (`fe_method`):** Never invoke host audio driver APIs from guest vCPU MMIO handlers (`NV1BA0_PIO_VOICE_LOCK/ON/OFF`). Any fault inside MMIO unwinds `d->vp.voice_locked` and permanently ghost-locks voices.
* **Fail-Safe Descriptors:** Null, zero-length, or out-of-bounds SGE/SSL descriptors must emit silence cleanly without asserting.

### Global Processor, Encode Processor & DSP56362 Core (`hw/xbox/mcpx/apu/dsp/`)
* **Memory Topology & Clean Zero-Init:**
  * `DSP_PRAM_SIZE = 32768` (`0x8000` words, 24-bit masked poison `0x00CACACA` only).
  * `DSP_XRAM_SIZE = 65536` (`0x10000` words, zero-initialized; mixbuffer at `X:$1400–$17FF` mirrored at `X:$0C00–$0FFF`). Never `memset` X-RAM or Y-RAM with `0xCA`.
  * `DSP_YRAM_SIZE = 2048` (zero-initialized). On the EP (`!is_gp`), `Y:$0800–$0FFF` maps the read-only 2,048-word ATSC A/52A `ep_yrom.h` table (writes silently discarded).
  * On-chip peripherals (`$FFFF80–$FFFFFF`, including Triple Timers `TCR0/1/2` at `$FFFFE3/$E7/$EB`) must remain dual-mapped in both X and Y spaces.
  * Core bootstrap isolation: only the GP (`is_gp == true`) auto-loads `0x800` words from scratch RAM on reset; never run that on the EP (`NV_PAPU_EPPMEM`).
* **24-Bit Silicon Arithmetic & Control Flow:**
  * Mask all ALU, AGU (`R0..R7`, `N0..N7`, `M0..M7`), and peripheral registers to 24 bits (`& 0x00FFFFFF`). Preserve all 4 AGU modes (linear fast-path `0xFFFFFF`/`0xFFFF`, reverse-carry `0x0000`, standard modulo `0x0001..0x7FFF`, multiple wrap-around `(m_low & 0xC000) == 0x8000`).
  * Preserve `SSH`/`SSL` stack asymmetry (`SSH` pre-inc push / post-dec pop; `SSL` in-place read/write without modifying `SP`) and non-crashing empty-stack pops.
  * Preserve `DO`/`DOR` zero-count (`LC == 0`) immediate bypass to `(LA + 1) & 0x00FFFFFF`, `DO FOREVER` (`0x000100`) `SR[FV]` semantics, and `emu_macr` accumulating (`dsp_add56`) strictly before rounding (`dsp_rnd56`).
* **32-Byte Pre-Decoded IR Cache (`dsp_decoded_op_t`):** Lazy fault-in only (never eager AOT over empty cold-boot P-RAM). Any P-RAM write MUST invalidate both `address` and `address - 1` to keep 2-word instructions coherent.
* **Bounded Execution & Spin-Wait Exemption:** Respect `GP_MAX_SUBFRAME_CYCLES` (`100000`) and `EP_SUBFRAME_CYCLES` (`12800`). `WAIT` (`0x000086`) / `STOP` (`0x000087`) assert `is_idle = true` and yield immediately. Spin-wait detection (`spin_count >= 16`) MUST exempt finite `DO` loops (`in_finite_do = SR[LF] && !SR[FV]`) so 1-instruction filter loops are never truncated.
* **GP Mixbuffer Scratch Isolation & Complementary Headroom (`gp_ep.c`):** In LLE mode, extract `gp_fx = gp_ch[b][i] - mixbins[b][i]` strictly when `b < 2 && gp_modified_bin[b] && gp_has_audio[b]` (`FL, FR` only). Channels `b = 2..5` (`FC, LFE, RL, RR`) MUST take dry `mixbins[b][i]` to block I3DL2 scratch corruption (`X:$1440–$14BF`, 1.5 kHz tinnitus). Scale `gp_fx` by `GP_LLE_FX_GAIN` (`0.35f`) and 4-corner VP HRTF submixes (`corner_to_ch6 = {0, 4, 1, 5}`) by the complement `0.65f` (`1.0f` in HLE).
* **HLE Mode & EP Micro-Kernel:** HLE mode (`hle_freeverb.h`) must keep the hybrid GP handshake gate active (64 subframes on init/`NV_PAPU_GPRST`, 8 subframes on GP MMIO read/write, 1-in-32 periodic heartbeat). The EP defaults to the clean-room 584-word `symphony_ep_rom.h` (`P:$0000` Seg 1, `P:$0180` Seg 2, `P:$0300–$0304` `rts` guards); in stereo mode (`!is_5_1_active`), `ep_drain_output_fifo0()` advances `NV_PAPU_EPOFCUR0` read-only by 128 bytes/subframe.

### DSP DMA (`dsp_dma.c`) & AC'97 ACI (`aci.c`, `ac97.c`, `xbox_pci.c`)
* **Word-Aligned Circular Scratch DMA (`buf_id = 0xE`):** Always mask `scratch_base`, `scratch_offset`, and `scratch_size` with `~(item_size - 1)` (clamping `aligned_scratch_size >= item_size`) and write back the aligned offset to `block_addr + 4`.
* **Bit-14 (`0x4000`) Zero-Generator Hole:** Descriptors where `(dsp_offset & 0x4000) != 0 || dsp_offset >= 0x4000` return `0x000000` on `DSP -> Memory` reads and silently discard `Memory -> DSP` writes.
* **AC'97 Silicon Routing & Resumption:** Maintain `PCI_INTERRUPT_LINE = 6` on PCI 0:6.0, LPC defaults `0x68 = 0x00031000` / `0x6C = 0x0E065491`, level-triggered `update_sr()`, autonomous 48 kHz (192,000 B/s) virtual-clock stepping (`PO_INDEX = 1`, `SO_INDEX = 7`), unconditional `SR_DCH | SR_CELV` clearing on `LVI` writes (including `civ == lvi` single-descriptor primer buffers), and saturating `clamp16()` additive summing into FL/FR every 8th APU subframe (`monitor.c`).
* **Live EEPROM Checksum:** Audio mode lives at offset `0x98` (fallback `0x9C`), never `0x60` or `0x90`. Any write to `0x98` must recompute the 32-bit one's-complement checksum at `0x60` over `0x64..0xBF`.

---

## 2. Scoping Discipline & Code Hygiene

* **Zero Unrelated Churn:** Touch only files and functions strictly required by the task. Do not perform drive-by reformatting or unrelated refactors.
* **Preserve Licensing & Dual-License Headers:** Never delete or alter copyright headers. Modifications to Xemu/QEMU files remain GPL-2.0-or-later; standalone clean-room headers (`symphony_ep_rom.h`, `ep_yrom.h`, `hle_binaural.h`, `hle_freeverb.h`) preserve their MIT / GPL-2.0-or-later dual-license notices.
* **No Speculative Shims:** Never invent dummy register values or opcodes without verifying against Motorola `DSP56300FM/AD` / `DSP56367UM` or hardware telemetry. Log rate-limited warnings and fail safely.
* **Scope Boundary:** This fork focuses strictly on the MCPX APU/ACI audio stack (plus explicitly vetted audio-adjacent fixes). Do not bundle speculative NV2A, CPU, or unrelated QEMU changes.

---

## 3. Build & Verification Protocol

* **Primary Build:** `./build.sh` (MSYS2 MinGW-w64 on Windows) or `./build.sh -p win64-cross` (Linux Docker cross-compile).
* **Incremental Build:** `ninja -C build`
* **Compiler Discipline:** Must compile with zero new warnings under GCC and Clang (`-Wall -Wextra -Werror=implicit-function-declaration`) with strict Unix LF line endings and `clang-format` compliance.
* **Y-ROM Parity:** If `tools/gen_yrom.py` is modified, verify `python3 tools/gen_yrom.py` reproduces the exact 2,048-word `ep_yrom.h` table.

---

## 4. Commit Message Standard

{subsystem}: {imperative summary under 72 chars}

[Observed bug or hardware behavior, exact register/telemetry proof, and fix rationale]

Valid subsystem prefixes: `apu/vp`, `apu/dsp`, `apu/dma`, `apu/gp_ep`, `ac97`, `aci`, `monitor/sdl`, `ui/xui`, `tools`, `ci`, `docs`.

## 5\. Agent Pre-Flight Checklist

*   \[ \] Strict ISO C99/C11 in all emulation files (C++17 only in `ui/xui/`).
    
*   \[ \] No Rust, JIT, or external audio library dependencies introduced.
    
*   \[ \] 24-bit masking (`0x00FFFFFF`), 2-word IR opcache invalidation, and `in_finite_do` spin-wait exemption intact.
    
*   \[ \] GP `b < 2` scratch isolation, unconditional VP `use_hrtf`, and `~(item_size - 1)` circular DMA alignment intact.
    
*   \[ \] AC'97 `civ == lvi` re-arming and additive FL/FR monitor summing intact.
    
*   \[ \] Copyright headers, dual-license notices, and Unix LF line endings preserved.
    
*   \[ \] `./build.sh` compiles cleanly with zero new warnings.
    
---

