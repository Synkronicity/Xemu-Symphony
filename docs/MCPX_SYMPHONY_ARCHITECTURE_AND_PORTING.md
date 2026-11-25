# Xemu Symphony - The Developer's Guide to Original Xbox MCPX Audio & Motorola DSP56362 Emulation

**Author:** Will Bonnett (Xemu Symphony)  
**Copyright:** © 2026 Will Bonnett. Documentation licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).  
**Code License Note:** Standalone clean-room headers (`symphony_ep_rom.h`, `ep_yrom.h`, `hle_binaural.h`, `hle_freeverb.h`) are licensed under MIT (GPL-2.0 compatible); modifications to Xemu/QEMU files remain GPL-2.0-or-later.

**Intended Audience:** Upstream Xemu maintainers, clean-sheet Original Xbox emulator developers (C / C++ / Rust), and engineers building or verifying audio subsystems without a background in audio DSP design.

## Why This Document Exists

If you are trying to emulate Original Xbox audio today - whether you are reviewing a pull request for upstream Xemu, wiring an alternative audio backend into a clean-sheet C++ emulator, or building a fresh implementation with AI coding tools - public documentation will steer you straight into several subtle traps. For over two decades, Original Xbox emulation was locked to 2-channel stereo because emulators tapped only a narrow slice of the Global Processor while leaving the Encode Processor stubbed or unclocked. Meanwhile, the existing open-source MCPX codebases carried silent assumptions about scratch RAM, circular DMA alignment, hardware 3D routing, and AC'97 interrupts that trigger 1.5 kHz tinnitus, periodic static bursts, and cutscene deadlocks on retail games.

Xemu Symphony is a ground-up overhaul of the MCPX Audio Processing Unit (APU) and AC'97 Audio Codec Interface (ACI) written entirely in portable ISO C99/C11. This guide explains:

1.  **How the Xbox audio hardware actually fits together** in plain systems-engineering terms.

    
2.  **Why Symphony retired the Rust JIT DSP** and why this pure-C architecture is a stronger long-term foundation for upstream Xemu.
    
    
3.  **How the 2:2:2 Audio Matrix works** so emulators can serve 5.1 home theaters, stereo headphones, desktop PCs, and power-constrained handhelds from one codebase.
    
    
4.  **The seven silicon behaviors that break naive emulators** - documented with exact register addresses, math, and retail game proofs.
    
    
5.  **How to drop Symphony's core into any non-Xemu emulator** (C or C++, x86_64 or ARM64/Apple Silicon, SDL3 or `miniaudio`/`IAudioClient3`).
    

## 1. The Mental Model: How Original Xbox Audio Works

You do not need to be an audio engineer to understand the Xbox sound system. Think of it as **two completely separate PCI sound devices** sharing one output stage, running at a fixed hardware sample rate of **48,000 Hz**:


1.  **The MCPX APU (PCI 0:5.0, MMIO `0xFE800000`)** - Handles game sound effects, 3D positioning, environmental reverb, and 5.1 surround mixing. Internally, it is a three-stage assembly line:
    
    
    *   **Voice Processor (VP):** A fixed-function hardware mixer that plays up to 256 simultaneous audio channels ("voices") - 64 capable of hardware 3D spatialization and 192 standard 2D voices. It reads compressed (Xbox IMA ADPCM) or raw PCM samples from guest RAM, changes their pitch, applies filters/envelopes, and pours the resulting audio into 32 shared scratchpad buffers called "mixbins" (`mixbins[32][32]`). Think of mixbins as a 32-bus studio patchbay: some buses hold final speaker channels (`FL, FR, FC, LFE, RL, RR`), while others act as effect sends (like Mixbin 10 for room reverb).
        
        
    *   **Global Processor (GP):** A programmable 24-bit Motorola DSP56362 core (`d->gp.dsp`). Games upload custom microcode (usually Microsoft's `dsound.lib` I3DL2 reverb engine) into the GP. Every subframe, the VP hands its 32 mixbins to the GP via a shared memory window (`X:$1400–$17FF`), and the GP adds room echo, chorus, or environmental effects.
        
        
    *   **Encode Processor (EP):** A _second_ programmable 24-bit Motorola DSP56362 core (`d->ep.dsp`). On a real 2001 Xbox, its job was to take the 6 uncompressed surround channels and compress them in real time into a 640 kbps Dolby Digital (AC-3) bitstream.
        
        
2.  **The AC'97 Audio Codec Interface / ACI (PCI 0:6.0)** - A standard Intel ICH-style PCI DMA sound controller. Many games use this completely separate path for streaming background music, FMV cutscenes, and non-DirectSound playback.
    

### The Counterfactual Insight: Why the EP Exists (and Why You Can't Just Delete It)

In 2001, HDMI did not exist. The only single-cable digital audio link to a surround receiver was TOSLINK optical S/PDIF, which topped out around 1.5 Mbps - enough for 2 channels of uncompressed 48 kHz PCM, but not 6. Microsoft and NVIDIA put an entire second DSP (the EP) on the die solely to squeeze 6 channels through that optical cable via lossy Dolby Digital compression.

An emulator running on a modern OS has no optical bandwidth ceiling. Compressing 6 discrete channels into lossy AC-3 only to link `liba52` or `FFmpeg` and decode them back to PCM wastes massive CPU cycles and degrades audio quality. **Symphony taps all 6 channels cleanly before AC-3 compression** (`FL, FR, FC, LFE, RL, RR`).

However, you cannot simply leave the EP turned off. When a game sees 5.1 enabled in the Xbox EEPROM, `dsound.lib` actively checks the EP's shared memory mailbox (`P:$0001–$0007`), waits for its DMA controller, and monitors its Output FIFO cursor (`NV_PAPU_EPOFCUR0`). If the EP stops advancing, games stall, mute, or drop to 8–15 FPS spinning on driver watchdogs. Symphony solves this by running a tiny, clean-room 584-word DSP56362 micro-kernel (`symphony_ep_rom.h`) on the EP that satisfies every guest handshake in 35-50 cycles and yields immediately, eliminating both the proprietary Dolby ROM requirement and 99.6% of the EP's CPU cost.


### Master Timing & End-to-End Signal Flow

Everything in the APU runs on a strict two-level clock at 48,000 Hz:
|     <br>Quantum                               	|     <br>Samples     	|     <br>Duration     	|     <br>Rate      	|     <br>Role                                                                                                                                           	|
|-----------------------------------------------	|---------------------	|----------------------	|-------------------	|--------------------------------------------------------------------------------------------------------------------------------------------------------	|
| **APU Subframe**<br>(`NUM_SAMPLES_PER_FRAME`) 	|    <br>32           	|    <br>666.67   µs   	|    <br>1,500   Hz 	|    <br>Atomic step for VP voice mixing, GP execution (`X:$1400–$17FF`), and 6-channel spatial assembly.                                                	|
| **EP Frame**<br>(`8 * 32, d->ep_frame_div`)   	|    <br>256          	|    <br>5,333.33   µs 	|    <br>187.5   Hz 	|    <br>Aggregates 8 subframes (`off = (ep_frame_div % 8) * 32`), sums AC'97 streaming PCM, flushes to host audio, and paces emulation `(throttle()`).  	|

```
+-----------------------------------------------------------------------------+
|  PCI 0:5.0: MCPX APU (48 kHz, 32-sample subframes @ 1,500 Hz)               |
+-----------------------------------------------------------------------------+
|  [1. Voice Processor (vp.c)]                                                |
|      256 voices (64 3D HRTF + 192 2D) -> float mixbins[32][32]              |
|      3D voices unconditionally spatialized -> vp.hrtf_submix[0..3]          |
|                                   |                                         |
|  [2. Global Processor (gp_ep.c)]  v                                         |
|      LLE Mode: Stage mixbins -> GP X:$1400-$17FF -> step d->gp.dsp          |
|                Extract wet delta (gp_ch - mixbin) * 0.35f for FL/FR ONLY    |
|      HLE Mode: hle_freeverb.h (Schroeder/Moorer + 4-tap I3DL2)              |
|                + hybrid GP handshake heartbeat (keeps dsound.lib alive)     |
|                                   |                                         |
|  [3. 6-Ch Assembly]               v                                         |
|      ch6[32][6] (FL, FR, FC, LFE, RL, RR)                                   |
|      + 4-corner VP HRTF submixes ({0,4,1,5}) * 0.65f (LLE) or 1.0f (HLE)    |
|                                   |                                         |
|  [4. 2:2:2 Output Matrix]         v                                         |
|      5.1 Discrete  -> surround_buf[256][6] (16-bit PCM)                     |
|      Headphones    -> hle_binaural.h (ITD + Head-Shadow + 6.5kHz Pinna)     |
|      Pro Logic II  -> hle_binaural.h (1kHz 90-deg All-Pass Lt/Rt)           |
|      2.0 Stereo    -> frame_buf[256][2] (16-bit PCM)                        |
|                                   |                                         |
|  [5. Encode Processor (d->ep.dsp)]|  (Runs 584-word symphony_ep_rom.h       |
|      Satisfies mailbox/DMA/FIFO0  |   in 35-50 cycles; lossless sink)       |
+-----------------------------------+-----------------------------------------+
                                    |  Every 8th subframe (256 samples)
                                    v  + Additive clamp16() into FL & FR
                            [Host Audio Sink]  <--- [PCI 0:6.0: AC'97 ACI]
                      (SDL3 / miniaudio / WASAPI)   (48 kHz 64 KB ring buffer)
```

## 2. The Case for Upstream Xemu: Why Pure ISO C Replaces the Rust JIT

For upstream Xemu maintainers evaluating this PR, the single largest structural change is **retiring the external Rust FFI JIT (`dsp56300_emu_ffi`, `dsp_jit.c`, `struct DSPOps`)** in favor of a single, flattened pure-C99/C11 DSP56362 interpreter (`dsp_core_t *c_core`).

That decision was not out of spite - it came directly out of profiling and silicon debugging across retail titles:

### 1. Eliminating Redundant Work Beats JIT-Compiling It

The original performance argument for a DSP JIT assumed the emulator had to execute ~12,800 cycles per subframe (19.2 million DSP instructions/second) of Dolby Digital MDCT and psychoacoustic bit-allocation code on the EP, plus heavy polling loops on the GP.

*   By replacing the proprietary 7,264-word `dolby_ep.bin` image with the clean-room 584-word `symphony_ep_rom.h` micro-kernel, EP execution drops from **12,800 cycles/subframe to 35–50 cycles/subframe** - a **>99.6% reduction** (under 75,000 instructions/second).
    
*   Centralized spin-wait detection immediately yields the core when guest microcode stalls in a peripheral polling loop (such as `jclr #1, X:<<$FFFFC3, *`), eliminating up to 19.2M wasted polling instructions/second.
    
*   Total host audio thread load drops from **45–70% of a CPU core down to <4%** in pure C, with zero JIT compilation pause or executable-memory (`W^X`) overhead.
    

### 2. Fixing the Legacy C Interpreter's Real Bottlenecks

Upstream's fallback C interpreter felt slow primarily because it inherited 2003-era Atari/Hatari code designed for 32-bit CPUs. Symphony modernized the four actual hot paths:

*   **32-Byte Cache-Aligned Pre-Decoded IR Cache (`dsp_decoded_op_t`):** Because DSP56362 P-RAM is bounded to 32,768 words (`0x8000`), a flat table of 32-byte pre-decoded structs occupies exactly **1.0 MB per core (2.0 MB total)** - fitting cleanly in modern L2/L3 caches. Previously, `pram_opcache` was gated on `cur_inst < 0x100000`, which skipped **70–85% of all DSP instructions** (parallel moves) and forced two indirect function calls plus full bitfield unpacking on every cycle. Symphony lazily pre-decodes all instructions on first execution into fused handlers with zero per-cycle bitfield parsing, and invalidates both `address` and `address - 1` on P-RAM writes to stay coherent across two-word instructions.
    
*   **Native 64-Bit Fixed-Point Math:** Replaced the legacy four-way 12-bit multiplication split (`dsp_mul56`) with native signed 64-bit multiplication (`(a * b) << 1` for Q23 x Q23 → Q47).
    
*   **Branchless Linear AGU Fast-Path:** Over 90% of DSP memory accesses use Linear Addressing (`M_n == 0xFFFFFF` or `m_low == 0xFFFF`). Checking linear mode inline avoids the multi-branch modulo/bit-reverse ladder on the dominant path.
    
*   **Zero vtable / FFI Marshalling Overhead:** Removing `struct DSPOps` lets GCC and Clang inline register mutations and memory accesses directly into `dsp56k_execute_instruction()`.

### 3. Single Source of Truth for Silicon Accuracy & Save States

Maintaining a C interpreter _and_ a separate Rust JIT meant every newly discovered Xbox silicon quirk had to be implemented twice - and the Rust JIT lacked the Bit-14 DMA zero-generator hole, 64K X-RAM expansion, and factory Y-ROM intercepts. It also caused state desynchronization across QEMU VMState save/load transitions and broke cross-compilation toolchains. Flattening everything onto `dsp->c_core` (with `dsp->core` strictly synced on VM save/load via `dsp_sync_to_vm` / `dsp_sync_from_vm`) gives upstream one bisectable, warning-free C codebase with zero Rust toolchain requirements.

|      Metric                          	|      Legacy Baseline (Hatari C + Retail EP)     	|      Symphony Pure-C Core + `symphony_ep_rom.h`     	|
|--------------------------------------	|-------------------------------------------------	|-----------------------------------------------------	|
|     EP Subframe Cycles               	|     12,800 cycles                               	|     35–50 cycles                                    	|
|     EP Instruction Rate (48 kHz)     	|     19,200,000 instrs/sec                       	|     <75,000 instrs/sec (>99.6% reduction)           	|
|     Host Audio Thread CPU Load       	|     45–70% of one core                          	|     <4% of one core                                 	|
|     Parallel Move Cache Miss Rate    	|     70–85% uncached                             	|     0% after lazy fault-in                          	|
|     External Proprietary ROM         	|     Mandatory 7.2 KB `dolby_ep.bin`             	|     None (built-in clean-room 584-word ROM)         	|
|     External Build Dependencies      	|     Rust toolchain + `dsp56300_emu_ffi`         	|     Pure ISO C99/C11 only                           	|

## 3. The 2:2:2 Audio Matrix: Serving Speakers, Headphones, Desktops, and Handhelds

A common mistake in Xbox emulators is hardcoding a single audio profile - for example, optimizing exclusively for high-end desktops using headphones, or treating "non-DSP mode" as a stripped-down fallback that drops all room reverb.

Symphony organizes audio rendering around three independent 2-state switches - the **2:2:2 Matrix**:

1.  **Processing Core: LLE (Dual DSP56362) vs. HLE (`hle_freeverb.h`)**
    
    *   **LLE Mode (`use_dsp = true`):** Steps both Motorola DSP56362 cores (`d->gp.dsp` and `d->ep.dsp`) per subframe for authentic guest reverb and DSP effects.
        
    *   **HLE Mode (`use_dsp = false`):** Bypasses steady-state GP DSP execution to minimize CPU/battery draw on handhelds and low-power hosts, while preserving full environmental audio via `hle_freeverb.h` - a pure-C Schroeder/Moorer reverb engine (8 parallel low-pass comb filters, 4 series all-pass diffusers with +25-sample right-channel spread, and a 4-tap I3DL2 early-reflection delay line at 3.5 ms, 7.8 ms, 12.4 ms, and 18.5 ms). It taps DirectSound's primary I3DL2 room send (`mixbins[10]`) at `1.0f` and auxiliary effect sends (`mixbins[6..9, 11..30]`) at `0.35f`. Crucially, HLE keeps a hybrid GP handshake gate alive so titles never freeze waiting on GP DMA responses.
        
2.  **Hardware Endpoint: Surround Sound vs Stereo**
    
    *   **Surround Sound:** Outputs either discrete 6-channel LPCM (`surround_buf[256][6]`) or routes the internal 6-channel soundstage through `hle_binaural.h`, a virtual 5.1 speaker array modeled directly in C99:
    
        *   **Woodworth Spherical-Head ITD:** 13-sample delay (≈270 μs) for front speakers (±30∘) and 35-sample delay (≈729 μs) for rear speakers (±110∘).
            
        *   **Head-Shadow Low-Pass Filters (ILD):** Single-pole contralateral LPFs at 2.2 kHz (α=0.25, gain `0.48`) for fronts and 1.2 kHz (α=0.145, gain `0.34`) for rears.
            
        *   **Rear Pinna Spectral Notch:** A 2nd-order biquad notch at 6,500 Hz (Q=2.0, -6 dB) on rear channels to provide front-to-back localization cues and prevent the rear field from collapsing forward.
            
        *   **LFE Conditioning:** 120 Hz single-pole LPF (α=0.0155, gain `0.45`) summed in-phase into both ears.

    *   **Stereo:** Outputs either clean, unprocessed 2.0 stereo (`L = FL + 0.7071*FC + 0.7071*RL`, `R = FR + 0.7071*FC + 0.7071*RR`) or the original front-facing stereo (`frame_buf[256][2]`) which was configured to use Sensaura's Transaural Crosstalk Cancellation (XTC) algorithm which injects anti-phase signals that cancel properly in open air between speakers, but cause harsh 500 Hz–4 kHz comb filtering and hollowed-out center dialogue on headphones.
            
3.  **Historical Preservation: Optional Dolby Digital (AC-3) Encoding and Dolby Pro Logic II Matrixed Stereo**
    
    *  For external surround receivers, supports adding the original EP P-ROM binary `dolby_ep.bin` to enable AC-3 encoding as it existed on retail hardware (must be provided separately for licensing reasons), or select Dolby Pro Logic II `Lt/Rt` matrix encoding which uses a 2nd-order 1 kHz 90∘ all-pass phase-shift network on rear channels to simulate surround sound with stereo front-facing speakers. Most users should not enable these as they are obsolete and worsen sound quality and performance, it is only offered for historical curiosity if you happen to have the hardware.
        

### Live EEPROM Switching Without Corrupting the Checksum

The Xbox stores its audio output flags in EEPROM User Settings at byte offset **`0x98`** (with fallback at `0x9C`, _not_ `0x60` or `0x90` -- `0x60` is the checksum and `0x90` is the Language ID):

*   `0x00000000` - Stereo
    
*   `0x00000001` - Dolby Surround (`Lt/Rt`)
    
*   `0x00010002` (or `0x00010000`)- Dolby Digital 5.1
    

Changing `0x98` without updating the 32-bit one's-complement checksum at **`0x60`** (computed across the 92 bytes from `0x64` to `0xBF`) triggers Xbox Error 07/08. `monitor.c` exposes `xemu_eeprom_get_audio_mode()` and `xemu_eeprom_set_audio_mode()` to recalculate the checksum atomically and switch modes live.


## 4. The Seven Hardware Gotchas Every Xbox Emulator Hits

Most of these behaviors are absent from legacy Xbox Scene documentation. If you are implementing an MCPX APU from scratch - or auditing an existing one - verify every item below.

### Gotcha 1: The GP Overwrites Bins 2–5 (`X:$1440–$14BF`) With Scratch Math

*   **Symptom:** Continuous 1.5 kHz / 3.0 kHz electronic whine (tinnitus) on Center and LFE channels, plus static in rear channels, whenever LLE DSP is enabled in 5.1 mode.
    
*   **Why It Happens:** `X:$1400–$17FF` (mirrored at `X:$0C00–$0FFF`) is the VP-to-GP _input_ mixbuffer. Once Microsoft's `dsound.lib` GP microcode reads the dry audio at the start of a subframe, it reuses `X:$1440–$14BF` (bins 2–5: `FC, LFE, RL, RR`) as temporary I3DL2 filter scratch RAM. Only `X:$1400–$143F` (bins 0–1: `FL, FR`) receives valid in-place output. Because subframes run at 48000/32\=1500 Hz, reading scratch state back as audio injects a 1.5 kHz waveform (with a strong 3.0 kHz harmonic) into Center/LFE.
    
*   **Exact Silicon Rule (`gp_ep.c`):** Snapshot `orig_mixbuf[32][32]` before stepping the GP. After GP execution, extract the wet effect delta (`gp_fx = gp_ch[b][i] - mixbins[b][i]`) strictly when `b < 2 && gp_modified_bin[b] && gp_has_audio[b]`. Channels `b = 2..5` always take clean dry `mixbins[b][i]`. To prevent +6 dB double-summing when phase-aligned GP output meets the VP's 3D submixes, scale `gp_fx` by `GP_LLE_FX_GAIN = 0.35f` and scale the 4-corner VP HRTF submixes by the exact complement `0.65f` (`1.0f` in HLE) so total gain stays at unity (`0.35 + 0.65 = 1.00`).
    

### Gotcha 2: Hardware VP 3D HRTF Must Never Be Gated

*   **Symptom:** Missing rear surround voices and loud, unattenuated 3D effects bleeding into front/center/LFE when switching from stereo to surround.
    
*   **Why It Happens:** Upstream Xemu gated `use_hrtf` inside `vp.c` on `&& g_config.audio.hrtf`. On real MCPX silicon, the VP's 31-tap HRIR spatializer (`hrtf_filter_process()`) is how the hardware routes mono 3D voices (slots `0..63` with `hrtf_handle != 0xFFFF`) into the four corner submix bins (`d->vp.hrtf_submix[0..3]`) using `(b >> 1)` hemisphere indexing. Turning it off dumps raw 3D voices straight into `mixbins[0..3]` and starves the rear corners (`corner_to_ch6 = { 0, 4, 1, 5 }`).
    
*   **Exact Silicon Rule (`vp.c`):** Keep `use_hrtf = (v < MCPX_HW_MAX_3D_VOICES) && !stereo && (hrtf_handle != HRTF_NULL_HANDLE)` unconditionally active regardless of host stereo/surround settings. When folding `hrtf_submix[0..3]` into `ch6`, deduplicate rear corners for stereo-configured games that map front and rear quadrants to the same mixbin.
    

### Gotcha 3: Circular Scratch DMA Must Mask Unaligned Byte Descriptors

*   **Symptom:** Loud white-noise static bursts every ~1.06 seconds on reverb-routed sound effects.
    
*   **Why It Happens:** DirectSound's I3DL2 microcode programs 32-bit (`format = 2`, `item_size = 4` bytes) circular scratch DMA buffers (`buf_id = 0xE`) with odd base addresses (e.g., `0x7A02B`) and buffer sizes congruent to `2 mod 4` (e.g., `0x31AC2` = 203,458 bytes). A naive byte-level ring copy splits a 4-byte sample `2 bytes + 2 bytes` at the wrap boundary, phase-shifting the write pointer by 16 bits relative to the read pointer. On every subsequent transfer, the low bytes of sample N+1 land in the sign byte (`bits 23:16`) of sample N, slamming the audio to full-scale noise rails every 203458/(48000×4)≈1.06.
    
*   **Exact Silicon Rule (`dsp_dma.c`):** Real Motorola DSP56300 DMA hardware ignores sub-word address bits. Mask `scratch_base`, `scratch_offset`, and `scratch_size` with `item_align_mask = ~(item_size - 1)` (clamping `aligned_scratch_size >= item_size`) and write the aligned offset back to descriptor word `block_addr + 4`.
    

### Gotcha 4: The Bit-14 (`0x4000`) Zero-Generator Hole & Factory Y-ROM (`Y:$0800–$0FFF`)

*   **Bit-14 Zero-Generator (`dsp_dma.c` / `gp_ep.c`):** Any DSP DMA descriptor where `(dsp_offset & 0x4000) != 0` or `dsp_offset >= 0x4000` hits an internal hardware zero-generator / bit-bucket aperture. `DSP -> Memory` reads must return `0x000000` (used by EP Cmd `0x0C` to zero-pad Output FIFO 0), and `Memory -> DSP` writes must be silently dropped - never passed to guest RAM page translation.
    
*   **Factory EP Y-ROM (`ep_yrom.h`):** On the Encode Processor (`!is_gp`), `Y:$0800–$0FFF` (2,048 words) is read-only factory mask ROM containing ATSC A/52A windowing, MDCT/FFT twiddle, and bit-allocation constants. Writes to `Y:$0800–$0FFF` on the EP must be silently discarded.


### Gotcha 5: Motorola DSP56362 Core & Memory Invariants

Every clean-sheet DSP56362 implementation must enforce these silicon rules:

*   **Memory Sizes & Zero-Init:** `DSP_PRAM_SIZE = 32768` (poisoned with 24-bit masked `0x00CACACA`), `DSP_XRAM_SIZE = 65536` (zero-initialized - never `memset` with `0xCA`, which leaves dirty upper bits `0xCA000000` and injects `-0.415` DC into delay lines), and `DSP_YRAM_SIZE = 2048` (zero-initialized). Peripherals (`$FFFF80–$FFFFFF`), including Triple Timers `TCR0/1/2` (`$FFFFE3`, `$FFFFE7`, `$FFFFEB`), are mapped identically in **both** X and Y spaces.
       
*   **24-Bit AGU (`R0..R7`, `N0..N7`, `M0..M7`):** Strictly 24-bit (`& 0x00FFFFFF`), resetting `M0..M7` to `0xFFFFFF`. Must support all four modes: linear (`0xFFFFFF` / `m_low == 0xFFFF`), bit-reverse (`0x0000`), standard modulo (`0x0001..0x7FFF`), and multiple wrap-around modulo (`(m_low & 0xC000) == 0x8000`).
        
*   **`SSH` / `SSL` Stack Pointer Asymmetry:** Writing `SSH` pre-increments `SP` and writes `stack[0][SP]` only; reading `SSH` reads `stack[0][SP]` and post-decrements `SP`. Reading or writing `SSL` accesses `stack[1][SP & 0x0F]` in-place without changing `SP`. Popping an empty stack must not assert/crash.
    
*   **Hardware Loops & Spin-Wait Exemption:** `DO`/`DOR` with `LC == 0` skips the loop body immediately (`pc = (la + 1) & 0xFFFFFF`) without pushing the stack or setting `SR[LF]`. `DO FOREVER` (`0x000100`) sets both `SR[LF]` (bit 15) and `SR[FV]` (bit 16); finite `DO`/`DOR` clears `SR[FV]`. **Critical:** A 1-instruction finite `DO` loop resets `pc` to `entry_pc` every iteration - your spin-wait detector _must_ exempt `in_finite_do = (SR[LF] && !SR[FV])` (and wait 16 consecutive hits before yielding) or it will truncate 64-tap FIR filters after 4 iterations.
    
*   **`MACR` Order:** `emu_macr` must accumulate (`dsp_add56`) _before_ convergent rounding (`dsp_rnd56`), never before addition.
    
*   **GP vs. EP Bootstrap Isolation:** Only the GP (`is_gp == true`) auto-loads `0x800` words from scratch RAM on reset; running that on the EP clobbers `NV_PAPU_EPPMEM` (`P:$0000`).
    

### Gotcha 6: HLE Mode Still Needs a GP Heartbeat (and a 100k-Cycle Ceiling)

*   **Symptom:** Mid-race freezes in _Quantum Redshift_ when entering tunnels or crossing water in HLE mode, or truncated DSP execution in LLE mode.
    
*   **Why It Happens:** _Quantum Redshift_ downloads a ~54,000-cycle custom GP program and spins the x86 CPU on a DMA handshake word during environment changes. If HLE keeps the GP permanently asleep, the guest CPU hangs forever.
    
*   **Exact Silicon Rule (`gp_ep.c`):** Raise `GP_MAX_SUBFRAME_CYCLES` to **`100,000`**. In HLE mode, arm `s_gp_handshake_subframes` to **64 subframes** on init/`NV_PAPU_GPRST` and **8 subframes** on any guest `gp_read()`/`gp_write()` MMIO access, plus wake the GP for a **1-in-32 subframe heartbeat** (`(ep_frame_div & 31) == 0`, every 21.3 ms). In stereo mode (`!is_5_1_active`), also run `ep_drain_output_fifo0()` (a read-only 128-byte/subframe modulo advance of `NV_PAPU_EPOFCUR0`) so `dsound.sys` never trips its stalled-FIFO watchdog.
    

### Gotcha 7: AC'97 DMA Deadlocks & Silent LPC IRQ Routing

*   **Symptom:** Frozen FMV audio or permanently silenced background music.
    
*   **Four Compounding Causes & Exact Rules:**
    
    1.  **LPC IRQ 6 Routing (`xbox_pci.c`, `aci.c`):** Set PCI 0:6.0 `PCI_INTERRUPT_LINE = 6` and initialize LPC power-on defaults `XBOX_LPC_PIRQ_ROUT (0x68) = 0x00031000` and `XBOX_LPC_INT_IRQ_ROUT (0x6C) = 0x0E065491` so Slot 6 `INTA#` actually reaches PIC IRQ 6.
        
    2.  **Level-Triggered Status (`ac97.c`):** `update_sr()` must evaluate `(SR_BCIS & CR_IOCE)`, `(SR_LVBCI & CR_LVBIE)`, and `(SR_FIFOE & CR_FEIE)` as level-triggered conditions driving `GS_POINT` (`1 << 3`), `GS_SOINT` (`1 << 4`), and `pci_set_irq()` - never edge-toggle XOR masks.
        
    3.  **Single-Descriptor `LVI` Re-Arming (`ac97.c`):** Games often prime AC'97 with a single descriptor (`BD 0`, `LVI = 0`), then re-write `LVI = 0` after `SR_DCH | SR_CELV` halts the channel. Do **not** guard re-arming with `if (civ != lvi)`. Clear `SR_DCH | SR_CELV` unconditionally when `CR_RPBM && SR_DCH`, and if `picb == 0`, reload `civ = lvi; piv = (lvi + 1) % 32; fetch_bd()`.
        
    4.  **Autonomous Clocking & Additive Front Mixing (`aci.c`, `monitor.c`):** Drive AC'97 from an autonomous drift-compensated 1 ms virtual timer at 192,000 bytes/sec (stepping `PO_INDEX = 1` and `SO_INDEX = 7` in lockstep) into a 64 KB ring buffer, then **additively sum** 256 stereo samples with saturating `clamp16()` into `FL` and `FR` every 8th APU subframe.
        

## 5. Porting Symphony Into Clean-Sheet & C++ Emulators

If you are integrating Symphony's APU/DSP subsystem as an independent audio module inside a non-Xemu emulator - for instance, a C++ emulator targeting Apple Silicon (ARM64), Windows, and Linux with `miniaudio` / `IAudioClient3` instead of QEMU and SDL3 - the core is already structured to drop in cleanly.

### 5.1 Zero-Dependency Drop-In Headers

These C99 headers have **zero QEMU or Xemu dependencies** and can be included directly in C or wrapped in `extern "C" { ... }` in C++:

|      File                                                                         |      Purpose                                                                                                  |
|-----------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------|
|     hw/xbox/mcpx/apu/dsp/symphony_ep_rom.h                                        |     Clean-room 584-word EP micro-kernel (P:$0000 Seg 1 +   P:$0180 Seg 2 + P:$0300 rts guards).               |
|     hw/xbox/mcpx/apu/dsp/ep_yrom.h                                                |     2,048-word ATSC A/52A factory EP Y-Data ROM   (Y:$0800–$0FFF).                                            |
|     hw/xbox/mcpx/apu/dsp/hle_binaural.h                                           |     Stateful 5.1-to-2.0 headphone binaural convolver, Pro   Logic II Lt/Rt encoder, and stereo downmixer.     |
|     hw/xbox/mcpx/apu/dsp/hle_freeverb.h                                           |     Zero-DSP Schroeder/Moorer + 4-tap I3DL2 reverberator.                                                     |
|     hw/xbox/mcpx/apu/vp/{hrtf,svf,adpcm,resample,sge,fpconv}.h   & apu_regs.h     |     Standalone Voice Processor filters, Xbox IMA ADPCM   decoder/cache, and hardware register bitfields.      |

### 5.2 Minimal Host Wiring Contract (C / C++)

To run the GP and EP Motorola DSP56362 cores inside your own emulator, you only need to supply two memory-bridge callbacks - one for guest Scatter-Gather Scratch RAM and one for DSP FIFOs:

```
#ifdef __cplusplus
extern "C" {
#endif

#include "hw/xbox/mcpx/apu/dsp/dsp.h"

/* Host callback signatures required by dsp_init():
 * dir == true  -> write from DSP into guest memory / FIFO
 * dir == false -> read from guest memory / FIFO into DSP */
typedef void (*dsp_scratch_rw_func)(
    void *opaque, uint8_t *ptr, uint32_t addr, size_t len, bool dir);
typedef void (*dsp_fifo_rw_func)(
    void *opaque, uint8_t *ptr, unsigned int index, size_t len, bool dir);

/* Instantiate Global Processor (is_gp = true) and Encode Processor (is_gp = false) */
DSPState *gp_dsp = dsp_init(host_ctx, gp_scratch_rw, gp_fifo_rw, true);
DSPState *ep_dsp = dsp_init(host_ctx, ep_scratch_rw, ep_fifo_rw, false);

/* Cold-boot the EP with the built-in 584-word Symphony micro-kernel */
dsp_bootstrap(ep_dsp);

#ifdef __cplusplus
}
#endif
```

**Wiring details to get right on the first pass:**

*   **Scatter-Gather Scratch Callbacks (`gp_scratch_rw` / `ep_scratch_rw`):** Translate the 32-bit byte offset `addr` into 4 KB pages (`page_entry = addr / 4096`, `offset_in_page = addr % 4096`) using the guest page table at `NV_PAPU_GPSADDR` / `NV_PAPU_GPSMAXSGE` (GP) or `NV_PAPU_EPSADDR` / `NV_PAPU_EPSMAXSGE` (EP). Each 8-byte PRD entry stores the page-aligned physical base directly in its first little-endian `uint32_t` (`paddr = prd_address + offset_in_page`). Always validate `page_entry <= max_sge` and guest RAM bounds before copying.
    
*   **FIFO Wrapping (`gp_fifo_rw` / `ep_fifo_rw`):** Guard `if (end <= base) return;` and wrap cursors via `cur = base + ((cur - base) % (end - base))`. Intercept EP Output FIFO 0 writes (`index == 0`) with silent zero frames (`ep_silence`) so the guest driver stays happy while you pull lossless audio from your internal 6-channel buffer.
    
*   **Inspecting Live State:** Always use the public `dsp_*` functions (`dsp_read_memory`, `dsp_write_memory`, `dsp_start_frame`, `dsp_run`) or `dsp->c_core` - never read `dsp->core`, which is only populated during save-state serialization.
    

### 5.3 Swapping the Host Audio Backend (SDL3 → `miniaudio` / `IAudioClient3` / CoreAudio)

Symphony's synthesis pipeline (`vp.c`, `gp_ep.c`, `hle_freeverb.h`, `hle_binaural.h`) has **no dependency on SDL3**. SDL3 lives strictly at the very end of `monitor.c`. To wire Symphony into `miniaudio`, Windows `IAudioClient3`, or macOS CoreAudio:

1.  Run `vp_frame` + `dsp_frame` on every 32-sample subframe (`1,500 Hz`).
    
2.  Accumulate 8 subframes into a 256-sample output block (`48,000 Hz`, `5.33 ms`):
    
    *   **6-channel discrete mode:** `int16_t surround_buf[256][6]` (3,072 bytes) in standard SMPTE/DirectSound channel order: `0=FL, 1=FR, 2=FC, 3=LFE, 4=RL, 5=RR`. (Or pull directly from `float ch6[32][6]` if your backend prefers 32-bit float natively.)
        
    *   **2-channel mode (Stereo / Binaural HRTF / Pro Logic II):** `int16_t frame_buf[256][2]` (1,024 bytes).
        
3.  Drain 256 stereo samples from your AC'97 ring buffer and additively sum them into channels `0` and `1` with saturating `clamp16()`.
    
4.  Push the 256-sample frame into your audio backend's lock-free ring buffer (e.g., `miniaudio` `ma_pcm_rb` or WASAPI `IAudioClient3` render buffer) and pace the APU thread against your ring buffer's queued depth (2 frames low watermark, 6 frames high watermark).
    

### 5.4 Apple Silicon (ARM64) & Cross-Platform Notes

*   **Zero x86-Specific Dependencies:** The pure-C DSP core uses standard fixed-width integer types (`int64_t`, `uint32_t`, `uint8_t`) with no x86 inline assembly, no SIMD intrinsics, and no JIT executable-memory (`MAP_JIT` / `pthread_jit_write_protect_np`) dance required on macOS Apple Silicon.
    
*   **Cache & Multiplier Fit:** The 64-bit Q23 × Q23 → Q47 multiply maps cleanly to native 64-bit integer multiply instructions on both ARM64 and x86_64, and the 2 MB pre-decoded IR cache (`dsp_decoded_op_t` aligned to 32 bytes) fits comfortably inside Apple Silicon's large L2 caches.
    
*   **Compiler Discipline:** All C units are built to compile cleanly with `-Wall -Wextra -Werror=implicit-function-declaration` under both Clang (Apple Clang / LLVM) and GCC.
    

### 5.5 Guardrails for "Vibecoded" Emulator Builds

If you are using an LLM to help port or build an Xbox audio subsystem from scratch, pin these six hard constraints in your prompt or test suite - without them, models trained on older public code will reliably reintroduce upstream bugs:

1.  **Never call host audio APIs from the guest vCPU MMIO thread (`fe_method`).** Keep PIO handlers (`NV1BA0_PIO_VOICE_LOCK/ON/OFF`) strictly to register/bitmap updates; any host-thread fault or exception inside MMIO unwinds `voice_lock` and permanently "ghost-locks" hardware voices.
    
2.  **Never gate VP `use_hrtf` on headphone output mode.** Hardware 3D voice routing to `hrtf_submix[0..3]` must run unconditionally.
    
3.  **Never copy GP `X:$1440–$14BF` (`b = 2..5`) back into `FC, LFE, RL, RR`.** Restrict GP wet-delta extraction to `b < 2` (`FL, FR`).
    
4.  **Never trust unaligned byte lengths in circular scratch DMA (`buf_id = 0xE`).** Always mask base, offset, and size with `~(item_size - 1)`.
    
5.  **Never trip spin-wait halting inside finite `DO` loops.** Check `in_finite_do = (SR[LF] && !SR[FV])` and verify `MACR` accumulates _before_ rounding.
    
6.  **Never drop `civ == lvi` AC'97 re-arm writes or forget LPC `0x68`/`0x6C` IRQ 6 defaults.**


**Author:** Will Bonnett (Xemu Symphony)  
**Copyright:** © 2026 Will Bonnett. Documentation licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).  
**Code License Note:** Standalone clean-room headers (`symphony_ep_rom.h`, `ep_yrom.h`, `hle_binaural.h`, `hle_freeverb.h`) are licensed under MIT (GPL-2.0 compatible); modifications to Xemu/QEMU files remain GPL-2.0-or-later.