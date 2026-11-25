# RECON PHASE 117A: DETERMINISTIC DISASSEMBLY & EXECUTION TRACE OF `tools/dolby_ep.bin` AND `gp_ep.c` INTERACTION AUDIT

**Target Branch:** `feature/xemu-dsp56362` (Xemu Symphony)  
**Target Binary:** `tools/dolby_ep.bin` (`29,056` bytes / `7,264` 32-bit LE words, extracted from *Halo 2* `DOLBY` section `0x00563B80`–`0x0056ACFF`)  
**Standalone Disassembler & Tracer:** `tools/trace_ep_rom.c` (compiled cleanly with MinGW-w64 `gcc -std=c11 -Wall -Wextra -Werror=implicit-function-declaration` against `hw/xbox/mcpx/apu/dsp/dsp_dma.c`, `hw/xbox/mcpx/apu/dsp/interp/dsp_cpu.c`, and `hw/xbox/mcpx/apu/dsp/interp/debug.c`)

---

## 1. Executive Summary & Root-Cause Analysis: Why the Naive 32-Word `WAIT` Stub Failed

### 1.1 The Three-Stage Failure Chain in `dsp.c` -> `monitor.c` -> `gp_ep.c`

Line-by-line inspection of `hw/xbox/mcpx/apu/dsp/dsp.c`, `hw/xbox/mcpx/apu/monitor.c`, and `hw/xbox/mcpx/apu/dsp/gp_ep.c` reveals the exact deterministic mechanism by which a 32-word (`128`-byte) `WAIT` stub at `P:0x0000` causes discrete 5.1 surround to collapse into a center-biased matrix mix with electronic flanging:

1. **Stage 1 — Hard Minimum Word-Count Gate in `dsp_bootstrap_ep_firmware()` (`hw/xbox/mcpx/apu/dsp/dsp.c:350-356`):**
   ```c
   size_t total_words = file_size / 4;
   if (total_words < (0xC8 + 0x17F)) {
       fprintf(stderr, "mcpx_apu: EP firmware too small (%ld bytes), skipping.\n", file_size);
       fclose(f);
       return;
   }
   ```
   `dsp_bootstrap_ep_firmware()` enforces a hard minimum size of **`0xC8 + 0x17F = 583` words (`2,332` bytes)**—the exact combined size of Resident Segment 1 (`200` words at `P:0x0000..0x00C7`) and Resident Segment 2 (`383` words at `P:0x0180..0x02FE`). A 32-word stub (`128` bytes, `total_words = 32 < 583`) is **immediately rejected before writing a single word to `d->ep.dsp->core.pram[0x0000]`**. Consequently, `P:0x0000` remains `0x000000` (`NOP`).

2. **Stage 2 — Firmware-Presence Gate in `mcpx_apu_monitor_init()` (`hw/xbox/mcpx/apu/monitor.c:80-98`):**
   ```c
   bool fw_present = false;
   if (d->ep.dsp) {
       uint32_t reset_vec = dsp_read_memory(d->ep.dsp, 'P', 0x0000) & 0x00FFFFFF;
       if (reset_vec != 0x000000) {
           fw_present = true;
       }
   }
   d->is_5_1_active = surround_requested && fw_present;
   int channels = d->is_5_1_active ? 6 : 2;
   ```
   When `mcpx_apu_init()` (`apu.c:515`) calls `mcpx_apu_dsp_init()` (`apu.c:529`) followed by `mcpx_apu_monitor_init()` (`apu.c:532`), `mcpx_apu_monitor_init()` probes `P:0x0000` of the EP. Because `dsp_bootstrap_ep_firmware()` rejected the 32-word stub, `reset_vec == 0x000000`, so `fw_present = false` and **`d->is_5_1_active` is forced to `false`**, initializing the host SDL3 audio stream in **2-channel Stereo (`channels = 2`)**.
   Crucially, however, `mcpx_apu_init()` (`apu.c:481-513`) **already patched the guest Xbox EEPROM (`XC_AUDIO_FLAGS`) in system RAM to `0x00010002` (`XC_AUDIO_FLAGS_SURROUND | XC_AUDIO_FLAGS_ENABLE_AC3`)** whenever the user's config requests 5.1 surround—*regardless* of whether `fw_present` is true!

3. **Stage 3 — Guest 5.1 GP Graph vs. Host 2-Channel Lt/Rt Scrape in `mcpx_apu_dsp_frame()` (`hw/xbox/mcpx/apu/dsp/gp_ep.c:507-579`):**
   Because the guest Xbox EEPROM reports Dolby Digital 5.1 enabled, the guest XDK (`DirectSound`) downloads and executes its 5.1 surround GP DSP effects graph. In this mode:
   - The GP writes the discrete 6-channel surround mix into GP Mixbin X-RAM (`X:0x1500` FL, `X:0x1520` FR, `X:0x1540` C, `X:0x1560` LFE, `X:0x1580` SL, `X:0x15A0` SR).
   - Simultaneously, the GP writes a phase-shifted Dolby Pro Logic (Lt/Rt) matrix downmix / reverb return into the 2D stereo buffers (`X:0x1400` Left, `X:0x1420` Right) where Center is summed in-phase into both channels and Surround is phase-shifted ($\pm 90^\circ$ / inverted polarity between Left and Right).
   - Every audio frame, `mcpx_apu_dsp_frame()` (`gp_ep.c:507-517`) checks:
     ```c
     if (!d->is_5_1_active) {
         for (int s = 0; s < 32; s++) {
             int32_t l_raw = (int32_t)dsp_read_memory(d->gp.dsp, 'X', 0x1400 + s);
             int32_t r_raw = (int32_t)dsp_read_memory(d->gp.dsp, 'X', 0x1420 + s);
             ...
             frame_buf[s][0] = (int16_t)l_val;
             frame_buf[s][1] = (int16_t)r_val;
         }
         return;
     }
     ```
   Because `d->is_5_1_active == false`, `mcpx_apu_dsp_frame()` takes this early `return`, scraping **only** `X:0x1400` and `X:0x1420` into the 2-channel SDL stream and **completely bypassing** the discrete 6-channel `surround_buf` extraction loop (`gp_ep.c:544-579`). Playing the GP's phase-shifted auxiliary/matrix bus (`X:0x1400`/`0x1420`) without the discrete 3D mixbins (`X:0x1500..0x15BF`) produces the exact center-biased collapse and comb-filtered electronic flanging observed.

---

### 1.2 Direct Answers to Task 1 Audit Questions

1. **Does the guest XDK upload its own bootstrap to EP P-RAM via DMA (`ep_bootstrap`) that expects specific subroutines at `P:0x0180` or `P:0x0300` (or `P:0x004000`)?**
   - **Host MMIO P-RAM Window (`0xFE850000`–`0xFE85FFFF`):** On real Xbox hardware (and in `gp_ep.c:453-458`), the guest XDK (`DirectSound` / `FUN_00385614`) writes the resident executive (`P:0x0000..0x00C7` and `P:0x0180..0x02FF`) and the **Runtime Scratch RAM Descriptor Table (`P:0x0100..0x013F`)** into EP P-RAM via MMIO (`NV_PAPU_EPPMEM`), and places the full `dolby_ep.bin` image (including Overlays `#1..#6` at file word offsets `584..7238` and the 24-word Configuration Struct at file word offset `7240`) into system Scratch RAM (`EPSADDR`).
   - **Dynamic Relocatable Overlays at `P:0x0300` (`dsp_offset = 0x2B00`):** The EP resident executive (`P:0x0000..0x00C7`) **never** runs a static monolithic program at `P:0x0300`. Instead, during every AC-3 audio block (`P:0x0001 = 0..5`), the executive at `P:0x004E`, `P:0x006E`, `P:0x007F`, `P:0x0090`, and `P:0x009D` invokes the DMA & Heap Service Kernel at **`P:0x0180`** (`Cmd 0x01..0x06`) to dynamically DMA one of **6 relocatable overlays** from Scratch RAM into **`P:0x0300`** (`DMA dsp_offset = 0x2800 + 0x0300 = 0x2B00`) and then calls its entry point at **`jsr p:$0304`**.
   - **Bit-14 DMA Zero-Generator Hole (`0x4000`):** The EP firmware uses DSP address `0x4000` (`Bit 14 = 1`) in `P:0x02F5..0x02F9` (initializing `DMA_START_BLOCK` `0xFFFFD5` and `DMA_NEXT_BLOCK` `0xFFFFD4` to `0x004000`, where bit 14 is `NODE_POINTER_EOL`) and in `P:0x02A3..0x02AF` (`Cmd 0x0C` S/PDIF zero-padding, setting `dsp_offset = (1 << 14) + r0 = 0x401F`) as well as in `Cmd 0x12`, `0x15`, `0x16`, and `0x19` (`Sub-op B = 0`, called with `R0 = 0x0000` or `0x2B00` and `N4 = 1` -> `dsp_offset = 0x4000`) to zero-fill newly allocated Scratch RAM buffers and Output FIFO 0 padding regions via hardware DMA.

2. **Does `gp_ep.c` read processed samples or state out of EP X-RAM/Y-RAM or Scratch RAM (`EPSADDR`)?**
   - **No.** In Xemu Symphony's Direct Lossless Audio Pipeline (`gp_ep.c:495-580`), `mcpx_apu_dsp_frame()` **never** reads audio samples or state from `d->ep.dsp` (`P:`, `X:`, `Y:`) or from `EPSADDR`/`EPFADDR`. Instead, when `d->is_5_1_active` is `true`, `mcpx_apu_dsp_frame()` taps discrete 6-channel 32-bit float LPCM (`surround_buf`) directly from **GP Mixbin X-RAM (`X:0x1500..0x15BF`)** additively summed with the **GP 2D Stereo bus (`X:0x1400` / `X:0x1420`)** on Front-Left and Front-Right (`gp_ep.c:544-579`). The only host read of `d->ep.dsp` outside MMIO (`ep_read`) is the boot-time check `dsp_read_memory(d->ep.dsp, 'P', 0x0000)` in `mcpx_apu_monitor_init()` (`monitor.c:88`).

3. **Does the guest XDK or GP poll a shared Scratch RAM address (`GPSADDR` / `EPSADDR`) or EP Output FIFO (`EPOFCUR0` / `EPFADDR`) that `dolby_ep.bin` updates via `dsp_dma_run()`?**
   - **What `dolby_ep.bin` updates when fully running on hardware:** Across each 256-sample block, `dolby_ep.bin` updates:
     - **EP P-RAM Status Words (`P:0x0001..0x0007`) and Peak/RMS Meter Words (`P:0x01AB..0x01BA`):** Readable by the guest CPU via the `NV_PAPU_EPPMEM` MMIO window (`ep_read` at `0x50000..0x5FFFF`):
       - `P:0x0001`: Current AC-3 audio block index (`0..5`).
       - `P:0x0002`: Overlay #1 cold-start flag (`1` on first block after reset, cleared to `0` at `P:0x005A`).
       - `P:0x0003`: Overlays `#2/#3/#4` cold-start flag (`1` on first block after reset, cleared to `0` at `P:0x00A9`).
       - `P:0x0004`: Last block execution cycle delta (`X:0xFFFFB3 - P:0x0005`, written at `P:0x00BB`).
       - `P:0x0005`: Subframe start timer snapshot (`X:0xFFFFB3`, written at `P:0x0036`).
       - `P:0x0006`: Watchdog status word (`0xCCCCCC` normal; `0xBBBBBB` if block cycle delta $\ge$ `0x0ACDA0` at `P:0x00C3`).
       - `P:0x0007`: Cumulative subframe/blockheartbeat counter (incremented at `P:0x002B` every frame).
       - `P:0x01AB..0x01AE`: 4-word Lt/Rt (or Lo/Ro) Peak & RMS meter levels written by `Cmd 0x0F` (`P:0x01DE`).
       - `P:0x01AF..0x01BA`: 12-word 6-channel (`FL, C, FR, SL, SR, LFE`) Peak & RMS meter levels written by `Cmd 0x0E` (`P:0x01E1`).
     - **EP Output FIFOs (`EPOFCUR0` / `EPOFCUR1` in `gp_ep.c:89-121`):**
       - `Cmd 0x08` (`buf_id = 1`, `FIFO1`) writes 256 stereo 16-bit downmixed samples (`1,024` bytes) per block to Output FIFO 1, advancing `d->regs[NV_PAPU_EPOFCUR1]`.
       - `Cmd 0x09` and `Cmd 0x0C` (`buf_id = 0`, `FIFO0`) write `3,072` 16-bit words (`6,144` bytes) of IEC 61937 / AC-3 S/PDIF bitstream per 6-block frame to Output FIFO 0, advancing `d->regs[NV_PAPU_EPOFCUR0]`.
     - **Ping-Pong Bank Toggle (`P:0x0116`):** `Cmd 0x0B` (`P:0x01C3..0x01C8`) toggles bit 8 of `P:0x0116` (`0x000000 <-> 0x000100`) at the end of every block (`P:0x00B4`).

---

## 2. Line-by-Line Audit of `gp_ep.c`, `dsp.c`, `dsp_dma.c`, `monitor.c`, and `dsp_cpu.c`

### 2.1 Complete EP Interaction Map in Emulator Source

| File & Function | Lines | Resource Accessed | Exact Operation & Subframe / Boot Timing |
| :--- | :--- | :--- | :--- |
| `apu.c:mcpx_apu_init()` | `481-513` | Guest RAM EEPROM (`XC_AUDIO_FLAGS`) | Patches guest Xbox EEPROM in RAM to `0x00010002` (`SURROUND \| ENABLE_AC3`) if 5.1 is enabled in config, before `monitor_init()` checks EP firmware. |
| `monitor.c:mcpx_apu_monitor_init()` | `80-98` | `d->ep.dsp` `P:0x0000`, `d->is_5_1_active` | Reads `dsp_read_memory(d->ep.dsp, 'P', 0x0000) & 0x00FFFFFF`. Sets `fw_present = (reset_vec != 0)` and `d->is_5_1_active = surround_requested && fw_present`. |
| `dsp.c:dsp_init()` | `375-421` | `DSPState`, `dsp56k_init_cpu()` | Initializes `DSPState` (`is_gp = false` for EP), binds `scratch_rw = ep_scratch_rw`, `fifo_rw = ep_fifo_rw`, `mem_read = c_dma_mem_read`, `mem_write = c_dma_mem_write`. |
| `dsp.c:dsp_bootstrap()` | `322-331` | `dsp56k_reset_cpu()` | Resets DSP CPU core; if `!s->is_gp`, invokes `dsp_bootstrap_ep_firmware(s)`. |
| `dsp.c:dsp_bootstrap_ep_firmware()` | `334-373` | `tools/dolby_ep.bin` -> `P:0x0000..0x00C7`, `P:0x0180..0x02FE`, `P:0x0300..` | Rejects file if `total_words < 583` (`0xC8 + 0x17F`). Copies words `0..199` (`0xC8`) to `P:0x0000..0x00C7`, words `200..582` (`0x17F`) to `P:0x0180..0x02FE`, and remaining `6,681` words (`583..7263`) to `P:0x0300..0x1D18`. Sets `pc = 0x0000`. |
| `dsp.c:dsp_read_peripheral()` | `189-247` | `0xFFFF80..0xFFFFFF` | Handles `HCR` (`0xFFFFC2`), `HSR` (`0xFFFFC3`), `HPCR` (`0xFFFFC4`), `HBAR` (`0xFFFFC5`), `HORX` (`0xFFFFC6`), `HOTX` (`0xFFFFC7`), and DMA registers `0xFFFFD4..0xFFFFD7`. **Note:** `HPCR` and `HBAR` interrupt bits are gated behind `if (dsp->is_gp)`! |
| `dsp.c:dsp_write_peripheral()` | `264-320` | `0xFFFF80..0xFFFFFF` | Handles `HCR`, `HPCR`, `HBAR`, `HOTX`, and DMA registers `0xFFFFD4..0xFFFFD7` (`DMA_NEXT_BLOCK`, `DMA_START_BLOCK`, `DMA_CONTROL`, `DMA_CONFIGURATION`). **Note:** `HBAR` interrupt/EOL clearing is gated behind `if (dsp->is_gp)`! |
| `gp_ep.c:ep_scratch_rw()` | `79-87` | `NV_PAPU_EPSADDR`, `NV_PAPU_EPSMAXS` | Translates EP Scratch RAM DMA accesses via scatter-gather page directory at `d->regs[NV_PAPU_EPSADDR]` (`2048`-byte pages). |
| `gp_ep.c:ep_fifo_rw()` | `89-121` | `NV_PAPU_EPFADDR`, `NV_PAPU_EPOFCUR0..3`, `NV_PAPU_EPIFCUR0..3` | Translates EP Output FIFO (`dir=1`, `index=0..3`) and Input FIFO (`dir=0`, `index=0..1`) circular ring DMA accesses via `EPFADDR` and updates `EPOFCUR` / `EPIFCUR`. |
| `gp_ep.c:ep_read()` | `351-370` | `0x30000..0x3FFFF`, `0x50000..0x5FFFF` | Host MMIO read of EP registers (`d->ep.regs`), EP P-RAM (`0x50000..0x5FFFF` -> `P:0x0000..0x3FFF`), X-RAM (`0x56000..0x5BFFF`), and Y-RAM (`0x5C000..0x5DFFF`). |
| `gp_ep.c:ep_write()` | `442-484` | `0x30000..0x3FFFF`, `0x50000..0x5FFFF`, `NV_PAPU_EPRST` | Host MMIO write to EP P-RAM (`0x50000..0x5FFFF`), X-RAM, Y-RAM, and `NV_PAPU_EPRST`. When `EPRST` is written with bit 0 & bit 1 set, calls `dsp_bootstrap(d->ep.dsp)` only if `P:0x0000 == 0`. |
| `gp_ep.c:mcpx_apu_dsp_frame()` | `519-541` | `d->ep.dsp`, `ep_frame_div` | Runs every 32-sample APU frame (`666.7 µs`). On `(d->ep_frame_div % 8) == 0` (every 256 samples = 1 AC-3 audio block), calls `dsp_start_frame(d->ep.dsp)`. On every subframe (`0..7`), sets `d->ep.dsp->core.is_idle = false` and calls `dsp_run(d->ep.dsp, EP_SUBFRAME_CYCLES)` (`12,800` cycles). |

---

### 2.2 Six Emulator Bugs Uncovered During the Audit of `dsp.c`, `dsp_cpu.c`, and `dsp_dma.c`

Running `tools/trace_ep_rom.c` against the unmodified `dsp.c`, `dsp_cpu.c`, and `dsp_dma.c` uncovered **six distinct emulator bugs** that explain why `tools/dolby_ep.bin` currently stalls in the `P:0x0031..0x0032` spinloop at runtime in Xemu Symphony (while still unlocking discrete 5.1 surround solely because its size `7,264 >= 583` and `P:0x0000 = 0x050C08 != 0` satisfy `dsp_bootstrap_ep_firmware()` and `mcpx_apu_monitor_init()`):

1. **Bug #1 — `HBAR` (`0xFFFFC5`) and `HPCR` (`0xFFFFC4`) Gated by `if (dsp->is_gp)` (`hw/xbox/mcpx/apu/dsp/dsp.c:218-241` & `291-314`):**
   - At `P:0x0032`, `dolby_ep.bin` synchronizes to the subframe start clock via `jclr #1, x:$ffffc5, p:$0031` (polling `INTERRUPT_START_FRAME = 0x02` in `HBAR`), and at `P:0x02DA..0x02DE` it waits for DMA completion via `jclr #7, x:$ffffc5, p:$02da` (polling `INTERRUPT_DMA_EOL = 0x80` in `HBAR`).
   - In `dsp.c:226`, `case DSP_REG_PERIPH_HBAR:` only ORs `dsp->interrupts` and `INTERRUPT_DMA_EOL` when `if (dsp->is_gp)` is true; for the EP (`is_gp == false`), `dsp_read_peripheral()` returns raw `dsp->hbar` (which `P:0x02D2` sets to `0x000080`).
   - Consequently, in **Pass 1** of `tools/trace_ep_rom.c` (exact `dsp.c` behavior), `P:0x0032` sees bit 1 = `0` forever and spins at `P:0x0031..0x0032` (`add #$01, a; jclr #1, x:$ffffc5, p:$0031`) for all `12,800` cycles of every subframe!

2. **Bug #2 — Off-By-One Segment 2 Length (`0x17F` vs `0x180`) in `dsp_bootstrap_ep_firmware()` (`hw/xbox/mcpx/apu/dsp/dsp.c:351, 361-368`):**
   - Resident Segment 2 occupies `P:0x0180..0x02FF`, which is `0x02FF - 0x0180 + 1 = 0x180` (`384`) words (file words `200..583`), where word `583` (`P:0x02FF`) is `0x000000` (`NOP`) and file word `584` (`0xD01B40`) is the header of **Overlay #1**.
   - `dsp_bootstrap_ep_firmware()` copies `0x17F` (`383`) words into `P:0x0180..0x02FE` and starts copying the tail (`tail_offset = 0xC8 + 0x17F = 583`) into `P:0x0300`, shifting everything in `P:0x0300..` right by 1 word (`P:0x0300 = 0x000000`, `P:0x0301 = 0xD01B40`, so `P:0x0304` is `0x050C40` shifted to `P:0x0305`).

3. **Bug #3 — `DSP_OP_FLAG_IMMUTABLE` Set on EP P-RAM (`hw/xbox/mcpx/apu/dsp/interp/dsp_cpu.c:631-633` & `1074`):**
   - `dsp_cpu.c:631` marks every predecoded instruction on the EP (`!dsp_core_is_gp(dsp)`) with `DSP_OP_FLAG_IMMUTABLE`.
   - When `dolby_ep.bin` DMAs Overlays `#1..#6` into `P:0x0300`, `write_memory_raw()` (`dsp_cpu.c:1074`) checks `if (!(dsp->predecode_table[address].flags & DSP_OP_FLAG_IMMUTABLE))` and **refuses to invalidate the predecode cache**, causing the interpreter to execute stale cached instruction handlers from the previous overlay!

4. **Bug #4 — 1-Instruction `DO`/`DOR` Loop Spin-Wait False Positive (`hw/xbox/mcpx/apu/dsp/interp/dsp_cpu.c:726-749`):**
   - `dsp_cpu.c`'s spin-wait detector checks if `pc` stays within `[spin_pc_Base, spin_pc_Base + 2]` for 3 iterations and sets `dsp->is_idle = true`.
   - Any 1-word `DO` or `DOR` block (such as `P:0x021E: dor #$001e, p:$0220` / `P:0x0220: movem a, p:(r0)+` in `Cmd 0x00`) repeats the single instruction at `P:0x0220` 30 times, falsely tripping `dsp->is_idle = true` after 3 iterations and aborting the subframe!

5. **Bug #5 — Missing & Misdecoded DSP56300 Instructions in `dsp_cpu.c` (`hw/xbox/mcpx/apu/dsp/interp/dsp_cpu.c` & `dsp_emu.c.inc`):**
   - **Misdecoded `IFcc` / `IFcc.U` (`0x202000..0x203FFF`):** `dsp_cpu.c` decodes `0010 0000 001U CCCC kkkkkkkk` through `emu_pm_2_2` (which executes the ALU operation **unconditionally** and then executes a bogus register-to-register move into register `0x10 + CCCC`). In `dolby_ep.bin`, this breaks:
     - `P:0x00AF`: `202113` (`clr a ifge`), which resets the modulo-6 AC-3 block counter `P:0x0001` only when `A >= 6`. Because `emu_pm_2_2` executed `clr a` unconditionally, `P:0x0001` was stuck at `0` forever!
     - `P:0x0066` & `P:0x006B`: `202a41` (`tfr x0, a ifeq`), which selects Overlay `#5` (`Config[3] == 1`) vs Overlay `#6` (`Config[3] == 2`).
     - `P:0x03DA`, `P:0x03DE`, `P:0x0402`, `P:0x041E` (Overlay #1), `P:0x07B8`, `P:0x0893`, `P:0x0909`, `P:0x091E` (Overlay #2), and `P:0x04D2`, `P:0x04EC`, `P:0x0643..0x068E`, `P:0x076E`, `P:0x08F1` (Overlay #3).
   - **Zero-Count `DO`/`DOR` Loop Bug (`emu_dor_reg`, `emu_do_reg`):** On Motorola DSP56300 silicon, `DO`/`DOR` with a loop count of `0` skips the loop body (`PC = LA + 1`). In `dsp_cpu.c`, `LC = 0` enters the loop body and decrements `LC` at `LA` to `0xFFFFFF` (`16,777,215` iterations), hanging Overlay #3 at `P:0x04AA` (`dor n6, p:$04b5` when `n6 = 0` for 1-bin critical bands in `Y:0x0F1A`).
   - **Unimplemented DSP56300 Opcodes Used by `dolby_ep.bin`:**
     - `extractu #CO, S2, D` (`0x0C1880`, used in Overlays `#1`, `#2`, `#3`)
     - `insert #CO, S1, D` (`0x0C1900`, used in Overlays `#2`, `#4`)
     - `bscc xxxx` (`0x0D1000`, PC-relative conditional branch to subroutine, used in Overlay `#3` at `P:0x0305`)
     - `move Y:(Rn + xxxx), D` / `move S, Y:(Rn + xxxx)` (`0x0B7080`, used in Overlay `#2` at `P:0x05CA`)
     - `dor [X or Y]:ea, label` (`0x064010`, used in Overlay `#3`)
     - `asl S1, S2, D` (`0x0C1E40`) and `asr S1, S2, D` (`0x0C1E60`, register-shift accumulator, used in Overlays `#2`, `#3`, `#4`)
     - `lsr #ii, D` (`0x0C1EC0`, immediate logical shift right, used in Overlay `#3` at `P:0x08E4`)
     - `clb S, D` (`0x0C1E00`, count leading bits, used in Overlays `#2`, `#3`)

6. **Bug #6 — `assert(dsp_offset + count < 0x1800)` Crashes on Bit-14 DMA Zero-Generator Hole (`0x4000`) (`hw/xbox/mcpx/apu/dsp/dsp_dma.c:83, 192-207`):**
   - `dolby_ep.bin` uses DSP DMA address `0x4000` (and `0x401F` in `Cmd 0x0C`) to read hardware-generated zeros into Scratch RAM (`Cmd 0x12`, `0x15`, `0x16`, `0x19` with `B = 0`) and Output FIFO 0 (`Cmd 0x0C`). `dsp_dma.c:205` rejects `dsp_offset >= 0x3800` with `assert(!"Dsp dma offset out of range")`.

---

## 3. Complete Binary Topology & Verified Disassembly of `tools/dolby_ep.bin`

### 3.1 Exact File Layout (`7,264` Words / `29,056` Bytes)

| Segment / Region | File Word Offset | File Byte Offset | Word Count | Runtime DSP Address | Header / Version ID | Architectural Role |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Segment 1** | `0 .. 199` (`0x0000..0x00C7`) | `0x0000 .. 0x031F` | `200` (`0x00C8`) | `P:0x0000 .. P:0x00C7` | Reset `0x050C08` (`bra p:$0008`) | Resident Executive Supervisor & 6-Block AC-3 Scheduler |
| *(Descriptor Hole)* | *(Unstored in file)* | *(Unstored)* | `96` (`0x0060`) | `P:0x00C8 .. P:0x0127` | Populated by XDK at `P:0x0100..0x013F` | Scratch RAM `(base_word, word_count)` Descriptor & Heap Table |
| *(Heap Hole)* | *(Unstored in file)* | *(Unstored)* | `88` (`0x0058`) | `P:0x0128 .. P:0x017F` | Zeroed by `Cmd 0x00` at boot | Dynamic Scratch Heap Slots (`Cmd 0x11..0x19`) & Stack |
| **Segment 2** | `200 .. 583` (`0x00C8..0x0247`) | `0x0320 .. 0x091F` | `384` (`0x0180`) | `P:0x0180 .. P:0x02FF` | Entry `P:0x0180` | Resident DMA Engine, Descriptor Builder (`P:0x01A2`) & Heap Kernel |
| **Overlay #1** | `584 .. 1337` (`0x0248..0x0539`) | `0x0920 .. 0x14E7` | `754` (`0x02F2`) | `P:0x0300 .. P:0x05F1` | `0x010172` (chk `0xF7F511`) | Dolby Surround (Lt/Rt) 5-to-2 Matrix Downmixer, Phase-Shift Biquad & RMS/Peak Meter |
| **Overlay #2** | `1338 .. 3483` (`0x053A..0x0D9B`) | `0x14E8 .. 0x366F` | `2,146` (`0x0862`) | `P:0x0300 .. P:0x0B61` | `0x020172` (chk `0x686B56`) | AC-3 512-Point Windowed MDCT Filterbank (6 Channels) & Exponent Extractor |
| **Overlay #3** | `3484 .. 5101` (`0x0D9C..0x13ED`) | `0x3670 .. 0x4FB7` | `1,618` (`0x0652`) | `P:0x0300 .. P:0x0951` | `0x030172` (chk `0x7AD5E5`) | AC-3 Exponent Strategy, Psychoacoustic Masking & Bit Allocation (`BAP`) |
| **Overlay #4** | `5102 .. 6848` (`0x13EE..0x1AC0`) | `0x4FB8 .. 0x6B03` | `1,747` (`0x06D3`) | `P:0x0300 .. P:0x09D2` | `0x040172` (chk `0x0FC6C2`) | AC-3 Mantissa Quantizer, IEC 61937 / BSI Bitstream Packer & CRC-16 (`CRC1`/`CRC2`) |
| *(Align Word)* | `6849` (`0x1AC1`) | `0x6B04 .. 0x6B07` | `1` (`0x0001`) | *(Unused)* | `0x000000` | 8-Byte Alignment Padding Word |
| **Overlay #5** | `6850 .. 7037` (`0x1AC2..0x1B7D`) | `0x6B08 .. 0x6DF7` | `188` (`0x00BC`) | `P:0x0300 .. P:0x03BB` | `0x050172` (chk `0x7D5E26`) | Mono (`Lo = Ro`) 5-to-2 Downmixer & Peak/RMS Meter (`Config[3] == 1`) |
| **Overlay #6** | `7038 .. 7238` (`0x1B7E..0x1C46`) | `0x6DF8 .. 0x711B` | `201` (`0x00C9`) | `P:0x0300 .. P:0x03C8` | `0x060172` (chk `0x87C4B0`) | Stereo (`Lo / Ro`) 5-to-2 Downmixer & Peak/RMS Meter (`Config[3] == 2`) |
| *(Align Word)* | `7239` (`0x1C47`) | `0x711C .. 0x711F` | `1` (`0x0001`) | *(Unused)* | `0x000000` | 8-Byte Alignment Padding Word |
| **Config Table** | `7240 .. 7263` (`0x1C48..0x1C5F`) | `0x7120 .. 0x717F` | `24` (`0x0018`) | `X:0x0BC0 .. X:0x0BD7` | `[0]=0x000018` | Default 24-Word Dolby Executive & AC-3 Encoder Configuration Structure |

---

### 3.2 Verified Disassembly of Segment 1: Resident Executive Supervisor (`P:0x0000..0x00C7`)

Disassembled directly via `tools/trace_ep_rom.c` using `interp/debug.c`:

```text
p:0000  050c08         (02 cyc)  bra p:$0008                   ; Reset vector -> boot init at P:0x0008
p:0001  000000         (02 cyc)  nop                           ; [State] Current AC-3 block index (0..5)
p:0002  000001         (02 cyc)  pflushun                      ; [State] Overlay #1 cold-start init flag (1 -> 0)
p:0003  000001         (02 cyc)  pflushun                      ; [State] Overlays #2/#3/#4 cold-start init flag (1 -> 0)
p:0004  000000         (02 cyc)  nop                           ; [State] Last block execution cycle delta
p:0005  000000         (02 cyc)  nop                           ; [State] Subframe start timer snapshot (X:0xFFFFB3)
p:0006  cccccc         (02 cyc)  dc $cccccc                    ; [State] Status / watchdog word (0xCCCCCC=OK, 0xBBBBBB=Timeout)
p:0007  000000         (02 cyc)  nop                           ; [State] Cumulative frame heartbeat counter

; --- Cold Boot Initialization ---
p:0008  240000         (02 cyc)  move #$00,x0
p:0009  077084 000004  (02 cyc)  movem x0,p:$0004              ; Clear cycle delta P:0x0004 = 0
p:000b  077084 000005  (02 cyc)  movem x0,p:$0005              ; Clear timer snapshot P:0x0005 = 0
p:000d  07f432 ffffff  (02 cyc)  movep #$ffffff,x:$ffffb2      ; Init Timer / Periph 0xFFFFB2 = 0xFFFFFF
p:000f  07f430 000001  (02 cyc)  movep #$000001,x:$ffffb0      ; Init Timer / Periph 0xFFFFB0 = 0x000001
p:0011  07f431 000001  (02 cyc)  movep #$000001,x:$ffffb1      ; Init Timer / Periph 0xFFFFB1 = 0x000001
p:0013  0500bb         (02 cyc)  movec #$00,sp                 ; Reset hardware stack pointer SP = 0
p:0014  05f42a 000be0  (02 cyc)  movec #$000be0,ep             ; Set Extension Pointer EP = 0x0BE0
p:0016  0520b8         (02 cyc)  movec #$20,mr                 ; Set Mode Register MR = 0x20 (Scaling/Interrupt mask)
p:0017  0afa74         (02 cyc)  bset #20,omr                  ; Set OMR bit 20
p:0018  200013         (02 cyc)  clr a                         ; A = 0
p:0019  077088 000001  (02 cyc)  movem a0,p:$0001              ; P:0x0001 (AC-3 block counter) = 0
p:001b  000008         (02 cyc)  inc a                         ; A = 1
p:001c  077088 000002  (02 cyc)  movem a0,p:$0002              ; P:0x0002 (Overlay #1 init flag) = 1
p:001e  077088 000003  (02 cyc)  movem a0,p:$0003              ; P:0x0003 (Overlays #2..#4 init flag) = 1
p:0020  56f400 000000  (02 cyc)  move #$000000,a               ; Cmd 0x00: Initialize Scratch Heap & DMA Peripherals
p:0022  0bf080 000180  (02 cyc)  jsr p:$0180                   ; Call Service Kernel P:0x0180 (Cmd 0x00)
p:0024  200003         (02 cyc)  tst a
p:0025  0d1042 0000a2  (02 cyc)  bne p:$0000c7                 ; If error, branch to retry loop
p:0027  08f485 000002  (02 cyc)  movep #$000002,x:$ffffc5      ; Clear INTERRUPT_START_FRAME (0x02) in HBAR

; --- Main Frame Synchronization Loop ---
p:0029  07f08e 000007  (02 cyc)  movem p:$0007,a               ; Increment frame heartbeat counter P:0x0007
p:002b  014180         (02 cyc)  add #$01,a
p:002c  07708e 000007  (02 cyc)  movem a,p:$0007
p:002e  08f484 000001  (02 cyc)  movep #$000001,x:$ffffc4      ; Assert HPCR = 1 (Ready for Frame Start)
p:0030  200013         (02 cyc)  clr a
p:0031  014180         (02 cyc)  add #$01,a                    ; Spin-wait counter while polling HBAR bit 1
p:0032  0a8581 000031  (02 cyc)  jclr #1,x:$ffffc5,p:$0031     ; Poll HBAR bit 1 (INTERRUPT_START_FRAME = 0x02)
p:0034  44f000 ffffb3  (02 cyc)  move x:$ffffb3,x0             ; Snapshot hardware timer X:0xFFFFB3
p:0036  077084 000005  (02 cyc)  movem x0,p:$0005              ; Save start timestamp to P:0x0005
p:0038  08f485 000002  (02 cyc)  movep #$000002,x:$ffffc5      ; Acknowledge/clear bit 1 in HBAR

; --- Step 1: On Block 0 Only (P:0x0001 == 0), DMA 24-Word Config Struct to X:0x0BC0 ---
p:003a  07f08e 000001  (02 cyc)  movem p:$0001,a               ; A = current block index (0..5)
p:003c  60f403 000bc0  (02 cyc)  tst a #$000bc0,r0             ; R0 = X:0x0BC0
p:003e  052409         (02 cyc)  bne p:$000047                 ; Skip Cmd 0x0A if Block != 0
p:003f  56f400 00000a  (02 cyc)  move #$00000a,a               ; Cmd 0x0A: DMA Config Table -> X:0x0BC0
p:0041  382000         (02 cyc)  move #$20,n0                  ; Max count = 0x20 (clamped to P:0x0115 = 0x18)
p:0042  0bf080 000180  (02 cyc)  jsr p:$0180
p:0044  200003         (02 cyc)  tst a
p:0045  0d1042 000082  (02 cyc)  bne p:$0000c7

; --- Step 2: 5-to-2 Analog/PCM Downmixer Selection (Overlay #1 vs Overlay #5 vs Overlay #6) ---
p:0047  56f000 000bc1  (02 cyc)  move x:$0bc1,a                ; Config[1]: Dolby Surround (Pro Logic Lt/Rt) Enable
p:0049  60f403 000300  (02 cyc)  tst a #$000300,r0             ; R0 = P:0x0300 overlay load target
p:004b  05a412         (02 cyc)  beq p:$00005d                 ; If Config[1] == 0, check Config[3] (Overlays #5/#6)
p:004c  56f400 000001  (02 cyc)  move #$000001,a               ; Cmd 0x01: Load Overlay #1 (754 words) -> P:0x0300
p:004e  0bf080 000180  (02 cyc)  jsr p:$0180
p:0050  200003         (02 cyc)  tst a
p:0051  0d1042 000076  (02 cyc)  bne p:$0000c7
p:0053  07f08f 000002  (02 cyc)  movem p:$0002,b               ; B = Overlay #1 init flag (1 on first block, 0 thereafter)
p:0055  60f400 000bc0  (02 cyc)  move #$000bc0,r0              ; R0 = Config struct pointer X:0x0BC0
p:0057  0bf080 000304  (02 cyc)  jsr p:$0304                   ; Execute Overlay #1 entry point
p:0059  240000         (02 cyc)  move #$00,x0
p:005a  077084 000002  (02 cyc)  movem x0,p:$0002              ; Clear P:0x0002 = 0
p:005c  050c1c         (02 cyc)  bra p:$0078                   ; Proceed to Step 3 (AC-3 Encoder check)

; --- Alternative Step 2: Simple Mono (Overlay #5) or Stereo Lo/Ro (Overlay #6) Downmixer ---
p:005d  57f000 000bc3  (02 cyc)  move x:$0bc3,b                ; Config[3]: 0=Off, 1=Mono (Ovl #5), 2=Stereo (Ovl #6)
p:005f  60f40b 000300  (02 cyc)  tst b #$000300,r0
p:0061  05a417         (02 cyc)  beq p:$000078
p:0062  44f413 000005  (02 cyc)  clr a #$000005,x0
p:0064  0140cd 000001  (02 cyc)  cmp #$000001,b
p:0066  202a41         (02 cyc)  tfr x0,a ifeq                 ; If Config[3] == 1 -> A = 5 (Overlay #5)
p:0067  44f400 000006  (02 cyc)  move #$000006,x0
p:0069  0140cd 000002  (02 cyc)  cmp #$000002,b
p:006b  202a41         (02 cyc)  tfr x0,a ifeq                 ; If Config[3] == 2 -> A = 6 (Overlay #6)
p:006c  200003         (02 cyc)  tst a
p:006d  05a40b         (02 cyc)  beq p:$000078
p:006e  0bf080 000180  (02 cyc)  jsr p:$0180                   ; Cmd 0x05 or 0x06: Load Overlay #5/#6 -> P:0x0300
p:0070  200003         (02 cyc)  tst a
p:0071  0d1042 000056  (02 cyc)  bne p:$0000c7
p:0073  60f400 000bc0  (02 cyc)  move #$000bc0,r0
p:0075  0bf080 000304  (02 cyc)  jsr p:$0304                   ; Execute Overlay #5 or #6
p:0077  050c01         (02 cyc)  bra p:$0078

; --- Step 3: AC-3 5.1 Digital Encoder Pipeline (Overlays #2 -> #3 -> #4) ---
p:0078  56f000 000bc2  (02 cyc)  move x:$0bc2,a                ; Config[2]: AC-3 Encoder Enable
p:007a  60f403 000300  (02 cyc)  tst a #$000300,r0
p:007c  05a44f         (02 cyc)  beq p:$0000ab                 ; If Config[2] == 0, skip Overlays #2, #3, #4
p:007d  56f400 000002  (02 cyc)  move #$000002,a               ; Cmd 0x02: Load Overlay #2 (2146 words) -> P:0x0300
p:007f  0bf080 000180  (02 cyc)  jsr p:$0180
p:0081  200003         (02 cyc)  tst a
p:0082  0d1042 000045  (02 cyc)  bne p:$0000c7
p:0084  60f400 000bc0  (02 cyc)  move #$000bc0,r0              ; R0 = X:0x0BC0
p:0086  07f08f 000003  (02 cyc)  movem p:$0003,b               ; B = Cold-start init flag (P:0x0003)
p:0088  07f084 000001  (02 cyc)  movem p:$0001,x0              ; X0 = Block index (0..5)
p:008a  0bf080 000304  (02 cyc)  jsr p:$0304                   ; Execute Overlay #2 (MDCT Filterbank & Exponents)
p:008c  60f400 000300  (02 cyc)  move #$000300,r0
p:008e  56f400 000003  (02 cyc)  move #$000003,a               ; Cmd 0x03: Load Overlay #3 (1618 words) -> P:0x0300
p:0090  0bf080 000180  (02 cyc)  jsr p:$0180
p:0092  200003         (02 cyc)  tst a
p:0093  0d1042 000034  (02 cyc)  bne p:$0000c7
p:0095  07f08f 000003  (02 cyc)  movem p:$0003,b
p:0097  0bf080 000304  (02 cyc)  jsr p:$0304                   ; Execute Overlay #3 (Psychoacoustic Bit Allocation)
p:0099  60f400 000300  (02 cyc)  move #$000300,r0
p:009b  56f400 000004  (02 cyc)  move #$000004,a               ; Cmd 0x04: Load Overlay #4 (1747 words) -> P:0x0300
p:009d  0bf080 000180  (02 cyc)  jsr p:$0180
p:009f  200003         (02 cyc)  tst a
p:00a0  0d1042 000027  (02 cyc)  bne p:$0000c7
p:00a2  07f08f 000003  (02 cyc)  movem p:$0003,b
p:00a4  07f084 000001  (02 cyc)  movem p:$0001,x0              ; X0 = Block index (0..5)
p:00a6  0bf080 000304  (02 cyc)  jsr p:$0304                   ; Execute Overlay #4 (Quantizer, Bitstream Packer & CRC)
p:00a8  240000         (02 cyc)  move #$00,x0
p:00a9  077084 000003  (02 cyc)  movem x0,p:$0003              ; Clear P:0x0003 = 0

; --- Step 4: Modulo-6 Block Counter Advance, Ping-Pong Toggle & Watchdog Check ---
p:00ab  07f08e 000001  (02 cyc)  movem p:$0001,a               ; A = P:0x0001 (0..5)
p:00ad  014180         (02 cyc)  add #$01,a                    ; A = A + 1
p:00ae  014685         (02 cyc)  cmp #$06,a                    ; Compare with 6
p:00af  202113         (02 cyc)  clr a ifge                    ; If A >= 6, wrap A = 0
p:00b0  07708c 000001  (02 cyc)  movem a1,p:$0001              ; Store updated block index (0..5)
p:00b2  56f400 00000b  (02 cyc)  move #$00000b,a               ; Cmd 0x0B: Toggle Ping-Pong Bank bit 8 in P:0x0116
p:00b4  0bf080 000180  (02 cyc)  jsr p:$0180
p:00b6  56f000 ffffb3  (02 cyc)  move x:$ffffb3,a              ; Read end timestamp X:0xFFFFB3
p:00b8  07f084 000005  (02 cyc)  movem p:$0005,x0              ; Read start timestamp P:0x0005
p:00ba  200044         (02 cyc)  sub x0,a
p:00bb  07708c 000004  (02 cyc)  movem a1,p:$0004              ; Store elapsed cycles in P:0x0004
p:00bd  44f400 0acda0  (02 cyc)  move #$0acda0,x0              ; Watchdog threshold = 0x0ACDA0 (708,000 cycles)
p:00bf  0c1ff8         (02 cyc)  cmpu x0,a
p:00c0  0596c9         (02 cyc)  blt p:$000029                 ; If elapsed < 0x0ACDA0, loop to P:0x0029 for next block
p:00c1  44f400 bbbbbb  (02 cyc)  move #$bbbbbb,x0              ; Deadline exceeded -> P:0x0006 = 0xBBBBBB
p:00c3  077084 000006  (02 cyc)  movem x0,p:$0006
p:00c5  050c00         (02 cyc)  bra p:$00c5                   ; Halt spinloop on watchdog fault
p:00c6  050ec3         (02 cyc)  bra p:$0029
p:00c7  050ec2         (02 cyc)  bra p:$0029
```

---

### 3.3 Verified Disassembly of Segment 2: Resident DMA & Heap Service Kernel (`P:0x0180..0x02FF`)

`P:0x0180` is the single unified syscall gateway used by both the Resident Executive (`P:0x0000..0x00C7`) and all six Overlays (`#1..#6`). It indexes the **Scratch RAM Descriptor Table at `P:0x0100 + 2 * CmdID`** (`R2 = 0x0100 + 2 * A1`), builds a 7-word MCPX DMA descriptor at **`P:0x01A2..0x01A8`**, and triggers hardware DMA via `0xFFFFD4` (`DMA_NEXT_BLOCK = 0x29A2`) and `0xFFFFD6` (`DMA_CONTROL = 0x01`):

```text
p:0180  62f400 000100  (02 cyc)  move #$000100,r2              ; Base of Descriptor Table P:0x0100
p:0182  219a00         (02 cyc)  move a1,n2                    ; N2 = Command ID (0x00 .. 0x19)
p:0183  3d0100         (02 cyc)  move #$01,n5                  ; N5 = 1 (wait for DMA EOL by default)
p:0184  014085         (02 cyc)  cmp #$00,a
p:0185  05a50f         (02 cyc)  beq p:$000214                 ; Cmd 0x00 -> Init Heap & DMA Hardware
p:0186  204a00         (02 cyc)  move (r2)+n2,r2
p:0187  204a00         (02 cyc)  move (r2)+n2,r2               ; R2 = 0x0100 + 2 * CmdID
p:0188  07da91         (02 cyc)  movem p:(r2)+,r1              ; R1 = P:(0x0100 + 2*CmdID) = Scratch word offset / FIFO id
p:0189  015185         (02 cyc)  cmp #$11,a
p:018a  0514ce         (02 cyc)  bge p:$0001f8                 ; Cmd 0x11..0x19 -> Dynamic Scratch Heap Manager
p:018b  014685         (02 cyc)  cmp #$06,a
p:018c  05f454         (02 cyc)  ble p:$0001c0                 ; Cmd 0x01..0x06 -> Load P-RAM Overlay #1..#6
p:018d  014985         (02 cyc)  cmp #$09,a
p:018e  05a48c         (02 cyc)  beq p:$0001da                 ; Cmd 0x09 -> S/PDIF AC-3 Bitstream Output FIFO 0
p:018f  014a85         (02 cyc)  cmp #$0a,a
p:0190  05a44b         (02 cyc)  beq p:$0001bb                 ; Cmd 0x0A -> Read 24-word Config Struct to X:(R0)
p:0191  014b85         (02 cyc)  cmp #$0b,a
p:0192  05a451         (02 cyc)  beq p:$0001c3                 ; Cmd 0x0B -> Toggle Ping-Pong Bank bit 8 in P:0x0116
p:0193  014c85         (02 cyc)  cmp #$0c,a
p:0194  05a495         (02 cyc)  beq p:$0001e9                 ; Cmd 0x0C -> Zero-Pad S/PDIF Output FIFO 0 (X:0x401F)
p:0195  014d85         (02 cyc)  cmp #$0d,a
p:0196  05a498         (02 cyc)  beq p:$0001ee                 ; Cmd 0x0D -> Output FIFO Write
p:0197  014885         (02 cyc)  cmp #$08,a
p:0198  05a45b         (02 cyc)  beq p:$0001d3                 ; Cmd 0x08 -> Interleaved 2-Ch Analog Downmix Output FIFO 1
p:0199  014e85         (02 cyc)  cmp #$0e,a
p:019a  05a487         (02 cyc)  beq p:$0001e1                 ; Cmd 0x0E -> Copy 12 Meter Words X:(R0)+ -> P:0x01AF..0x01BA
p:019b  014f85         (02 cyc)  cmp #$0f,a
p:019c  05a482         (02 cyc)  beq p:$0001de                 ; Cmd 0x0F -> Copy 4 Meter Words X:(R0)+ -> P:0x01AB..0x01AE
p:019d  050c4c         (02 cyc)  bra p:$01c9                   ; Cmd 0x07 -> Read 256-Sample PCM Channel N1 from Ping-Pong Ring

; --- Internal State & 7-Word DMA Descriptor Buffer (P:0x019E..0x01BA) ---
; P:0x019E = Current Scratch Heap Free Base Word
; P:0x019F = Current Scratch Heap Remaining Words
; P:0x01A2..0x01A8 = Active 7-Word DMA Descriptor Node (P-RAM address 0x01A2 -> DMA addr 0x29A2)
; P:0x01AB..0x01AE = 4-Word Lt/Rt Peak & RMS Meter Mailbox (written by Cmd 0x0F)
; P:0x01AF..0x01BA = 12-Word 6-Channel Peak & RMS Meter Mailbox (written by Cmd 0x0E)
```

#### Summary of All `P:0x0180` Service Commands (`Cmd 0x00` – `Cmd 0x19`)

| Command ID | Table Slot (`P:0x0100 + 2*ID`) | Sub-Op (`B1`) | Handler PC | Operation Performed |
| :--- | :--- | :--- | :--- | :--- |
| **`Cmd 0x00`** | Reads `P:0x0122..0x0123` | `0` | `P:0x0214` | Copies initial Scratch heap `(base, size)` from `P:0x0122..0x0123` to `P:0x019E..0x019F`, zeroes `P:0x0122..0x013F` (`30` words), and calls `P:0x02EF` to reset `HBAR`/`HPCR`/DMA registers. |
| **`Cmd 0x01..0x06`** | `P:0x0102..0x010D` | `0..2` | `P:0x01C0` -> `P:0x0260` | DMAs **Overlay `#1..#6`** (`count = P:(0x0101+2*ID)`) from Scratch word `R1` (`byte R1<<2`) into **`P:0x0300`** (`dsp_offset = R0 + 0x2800 = 0x2B00`, `ctrl = 0x5BE0`). |
| **`Cmd 0x07`** | `P:0x010E..0x010F` | `0, 1` | `P:0x01C9` -> `P:0x0224` | DMAs `N0 = 256` (`0x100`) 24-bit PCM samples for **Channel `N1` (`0..5`)** from Scratch word `R1 + P:0x0116 + (N1 << 9)` into `X:(R0)` (`X:0x0000..0x0400` or `X:0x0080`, `ctrl = 0x5BE0`). |
| **`Cmd 0x08`** | `P:0x0110..0x0111` | `0, 1` | `P:0x01D3` -> `P:0x0238` -> `P:0x02B1` | When `N1 == 0`: DMAs `256` interleaved stereo 16-bit downmixed samples from `X:(R0)` (`X:0x0400`/`0x0500` or `X:0x0500`/`0x0600`) to **Output FIFO `R1` (`FIFO1`)** (`ctrl = 0x400623`, `cnt = 0x1001`). When `N1 != 0`: no-op (`bne p:$020f`). |
| **`Cmd 0x09`** | `P:0x0112..0x0113` | `2` | `P:0x01DA` -> `P:0x0238` -> `P:0x029A` | DMAs `N0` (`212..220`) 16-bit AC-3 bitstream words from `X:(R0)` (`X:0x001F`) to **Output FIFO `R1` (`FIFO0`)** (`ctrl = 0x4602`). |
| **`Cmd 0x0A`** | `P:0x0114..0x0115` | `0` | `P:0x01BB` -> `P:0x0224` | DMAs `24` (`0x18`) 24-bit Configuration words from Scratch word `R1` (`7240` / byte `0x7120`) to **`X:0x0BC0..0x0BD7`** (`ctrl = 0x5BE0`). |
| **`Cmd 0x0B`** | `P:0x0116..0x0117` | `1` | `P:0x01C3` | Flips bit 8 (`0x000100`) of `P:0x0116` (`bchg #8, x0`) to alternate the 6-channel PCM input ring between Ping (`+0`) and Pong (`+256` words/channel). |
| **`Cmd 0x0C`** | `P:0x0118..0x0119` | `0, 1` | `P:0x01E9` -> `P:0x0238` -> `P:0x029A` | DMAs `N0` (`0x5DA = 1498` in Block 0; `0x122 = 290` in Block 5) 16-bit zero words from the **Bit-14 DMA Zero-Generator Hole (`X:0x401F`)** to **Output FIFO `R1` (`FIFO0`)** (`ctrl = 0x4602`, `N5 = 0` async). |
| **`Cmd 0x0D`** | `P:0x011A..0x011B` | `0` | `P:0x01EE` -> `P:0x0238` -> `P:0x029A` | Auxiliary Output FIFO write (`ctrl = 0x4602`). |
| **`Cmd 0x0E`** | *(Direct `movem`)* | `1` | `P:0x01E1` | Copies `N0 = 12` (`0x0C`) words of 6-channel Peak/RMS meter levels from `X:0x0914..0x091F` to **`P:0x01AF..0x01BA`**. |
| **`Cmd 0x0F`** | *(Direct `movem`)* | `1` | `P:0x01DE` | Copies `N0 = 4` words of Lt/Rt (or Lo/Ro) Peak/RMS meter levels from `X:0x062F..0x0632` (or `X:0x0701..0x0704`) to **`P:0x01AB..0x01AE`**. |
| **`Cmd 0x11..0x19`** | `P:0x0122..0x0133` | `B=0`: Alloc<br>`B=1`: Read X<br>`B=2`: Write X<br>`B=3`: Read Y<br>`B=4`: Write Y<br>`B=5`: Read P<br>`B=6`: Write P | `P:0x01F8` -> `P:0x01F2..0x01F7` | **Dynamic Scratch RAM Heap Manager:**<br>- `B=0`: Allocates `N0` words from `P:0x019E..0x019F` and stores `(base, N0)` in `P:(0x0100+2*ID)` (`Returns A=0` or `A=4` on OOM).<br>- `B=1..6`: Dispatches via jump table `P:0x01F2..0x01F7` (`0x0224, 0x022E, 0x024A, 0x0255, 0x0260, 0x026B`) to DMA `N0` words between Scratch word `R1 + N1` and DSP X/Y/P memory at `R0`. |

---

### 3.4 Architecture of Relocatable Overlays `#1..#6` (`P:0x0300`) and Default Config (`X:0x0BC0`)

Every overlay in `tools/dolby_ep.bin` has a standardized 5-word header at `P:0x0300..0x0304`:
- `P:0x0300`: `0xD01B40` (Signature word)
- `P:0x0301`: Overlay length in words (`0x02F2`, `0x0862`, `0x0652`, `0x06D3`, `0x00BC`, `0x00C9`)
- `P:0x0302`: Overlay ID & Version (`0x010172` .. `0x060172`, i.e., Overlay `1..6`, Version `01.72`)
- `P:0x0303`: 24-bit Checksum word
- `P:0x0304`: Executable entry point (`jsr p:$0304`)

#### Default 24-Word Configuration Structure (`X:0x0BC0..0x0BD7`, File Words `7240..7263`)

| X-RAM Addr | File Word | Value in `dolby_ep.bin` | Meaning & Consumers |
| :--- | :--- | :--- | :--- |
| `X:0x0BC0` | `7240` | `0x000018` (`24`) | Structure size in words (`24`) |
| `X:0x0BC1` | `7241` | `0x000001` | **Dolby Surround (Lt/Rt) Downmixer Enable** (`P:0x0047`: `1` = run Overlay #1) |
| `X:0x0BC2` | `7242` | `0x000001` | **AC-3 5.1 Encoder Enable** (`P:0x0078`: `1` = run Overlays #2, #3, #4) |
| `X:0x0BC3` | `7243` | `0x000000` | **Fallback PCM Downmixer Mode** (`P:0x005D`: `1` = Overlay #5 Mono, `2` = Overlay #6 Stereo Lo/Ro) |
| `X:0x0BC4` | `7244` | `0x000000` | Input channel routing configuration |
| `X:0x0BC5` | `7245` | `0x000007` | AC-3 audio coding mode (`acmod = 7` -> `3/2` 5-channel `FL, C, FR, SL, SR`) |
| `X:0x0BC6` | `7246` | `0x000001` | LFE channel enable (`lfeon = 1`) |
| `X:0x0BC7` | `7247` | `0x00001F` (`31`) | Dialogue normalization (`dialnorm = 31` -> `-31 dBFS`, also indexes gain table `P:0x0305` in Overlay #1) |
| `X:0x0BC8` | `7248` | `0x000004` | Bandwidth code (`chbwcod`) |
| `X:0x0BC9..0x0BD7` | `7249..7263` | `0, 0, 0, 1, 0xFFFF, 1, 0xFFFF, 1, 1, 2, 1, 0, 0, 1, 0` | BSI metadata flags (`compre`, `dynrnge`, `cmixlev`, `surmixlev`, `dsurmod`, `copyrightb`, `origbs`, etc.) |

---

## 4. Deterministic Subframe Execution Trace & Complete 7-Word DMA Descriptor Catalog

### 4.1 Summary of the 5 Simulation Passes in `tools/trace_ep_rom.c`

| Simulation Pass | Configuration | Observed Execution Behavior |
| :--- | :--- | :--- |
| **Pass 1** | Exact unmodified `dsp.c` (`is_gp = false` on `HBAR` `0xFFFFC5`, unpopulated `P:0x0100`) | Boot runs `P:0x0000` -> `P:0x0008..0x0022` -> `jsr p:$0180` (`Cmd 0x00` -> `P:0x0214..0x0223` -> `P:0x02EF..0x02FD`), returns to `P:0x0024..0x0032`, and **spins forever at `P:0x0031..0x0032`** because `HBAR` bit 1 is never returned when `is_gp == false`. |
| **Pass 2** | `HBAR` (`0xFFFFC5`) returning `interrupts \| INTERRUPT_DMA_EOL`, unpopulated `P:0x0100` | In `SF 00..06`, `dor #$001e, p:$0220` at `P:0x021E` trips `dsp_cpu.c`'s 3-iteration 1-word `DOR` spin-wait detector (`is_idle = true`) until all 30 iterations finish in `SF 07`, after which `P:0x0032` wakes each frame, runs `Cmd 0x0A` (`cnt = 0` because `P:0x0100` is zero), sees `X:0x0BC1..0x0BC3 == 0`, and loops back to `P:0x0031..0x0032`. |
| **Pass 3** | Full 6-Block AC-3 Pipeline (`Config[1]=1, Config[2]=1`, populated `P:0x0100..0x0123`, Blocks `0..5`) | Executes **all 6 AC-3 blocks (`Blocks 0..5`) to 100% completion**: loads and executes **Overlay #1**, **Overlay #2**, **Overlay #3**, and **Overlay #4** in every block, executes **`222` 7-word DMA descriptor transfers**, outputs `6 * 256` downmixed stereo samples to `FIFO1` (`Cmd 0x08`) and a complete `3,072`-word (`6,144`-byte) IEC 61937 / AC-3 frame (`1,498` lead-in zeros + `1,284` AC-3 words + `290` trail-out zeros) to `FIFO0` (`Cmd 0x09` / `Cmd 0x0C`), and wraps `P:0x0001` from `5` back to `0`. |
| **Pass 4A** | **Overlay #5** Mono Downmixer (`Config[1]=0, Config[2]=0, Config[3]=1`) | Loads Overlay #5 (`188` words) into `P:0x0300`, DMAs 5 channels (`Cmd 0x07`), downmixes 5.0 to Mono (`Lo = Ro`) with Peak/RMS metering in **`12,586` cycles**, outputs 256 stereo frames to `FIFO1` (`Cmd 0x08`), writes 4 meter words to `P:0x01AB..0x01AE` (`Cmd 0x0F`), and toggles `Cmd 0x0B`. |
| **Pass 4B** | **Overlay #6** Stereo Lo/Ro Downmixer (`Config[1]=0, Config[2]=0, Config[3]=2`) | Loads Overlay #6 (`201` words) into `P:0x0300`, DMAs 5 channels (`Cmd 0x07`), downmixes 5.0 to Stereo (`Lo / Ro`) with Peak/RMS metering in **`15,162` cycles**, outputs 256 stereo frames to `FIFO1` (`Cmd 0x08`), writes 4 meter words to `P:0x01AB..0x01AE` (`Cmd 0x0F`), and toggles `Cmd 0x0B`. |

---

### 4.2 Complete Peripheral Read/Write Audit (`0xFFFF80..0xFFFFFF`)

Every peripheral access executed by `tools/dolby_ep.bin` across boot and subframe execution:

| PC | Instruction | Peripheral Address | Register Name | R/W | Value | Exact Purpose |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `P:0x000D` | `movep #$ffffff,x:$ffffb2` | `X:0xFFFFB2` | Timer / ESSI | Write | `0xFFFFFF` | Boot initialization of peripheral port `0xFFFFB2` |
| `P:0x000F` | `movep #$000001,x:$ffffb0` | `X:0xFFFFB0` | Timer / ESSI | Write | `0x000001` | Boot initialization of peripheral port `0xFFFFB0` |
| `P:0x0011` | `movep #$000001,x:$ffffb1` | `X:0xFFFFB1` | Timer / ESSI | Write | `0x000001` | Boot initialization of peripheral port `0xFFFFB1` |
| `P:0x02EF` | `movep #$000fff,x:$ffffc5` | `X:0xFFFFC5` | `HBAR` | Write | `0x000FFF` | Clear all pending host/DMA interrupt flags (`0x0FFF`) during `Cmd 0x00` |
| `P:0x02F1` | `movep #$000001,x:$ffffc4` | `X:0xFFFFC4` | `HPCR` | Write | `0x000001` | Set `HPCR = 1` during `Cmd 0x00` |
| `P:0x02F3` | `movep #$000000,x:$ffffca` | `X:0xFFFFCA` | Periph `0xFFFFCA` | Write | `0x000000` | Clear `0xFFFFCA` during `Cmd 0x00` |
| `P:0x02F7` | `move x0,x:$ffffd5` | `X:0xFFFFD5` | `DMA_START_BLOCK` | Write | `0x004000` | Initialize `DMA_START_BLOCK` to `0x4000` (`NODE_POINTER_EOL`) |
| `P:0x02F9` | `move x0,x:$ffffd4` | `X:0xFFFFD4` | `DMA_NEXT_BLOCK` | Write | `0x004000` | Initialize `DMA_NEXT_BLOCK` to `0x4000` (`NODE_POINTER_EOL`) |
| `P:0x02FB` | `movep #$000000,x:$ffffd7` | `X:0xFFFFD7` | `DMA_CONFIG` | Write | `0x000000` | Initialize `DMA_CONFIGURATION = 0` |
| `P:0x0027` | `movep #$000002,x:$ffffc5` | `X:0xFFFFC5` | `HBAR` | Write | `0x000002` | Clear `INTERRUPT_START_FRAME` (`bit 1`) after boot |
| `P:0x002E` | `movep #$000001,x:$ffffc4` | `X:0xFFFFC4` | `HPCR` | Write | `0x000001` | Signal EP ready for next frame |
| `P:0x0032` | `jclr #1,x:$ffffc5,p:$0031` | `X:0xFFFFC5` | `HBAR` | Read | `0x000002` | Poll `INTERRUPT_START_FRAME` (`bit 1`) |
| `P:0x0034` | `move x:$ffffb3,x0` | `X:0xFFFFB3` | Timer Counter | Read | `0x......` | Read hardware cycle/timer counter at start of block (`-> P:0x0005`) |
| `P:0x0038` | `movep #$000002,x:$ffffc5` | `X:0xFFFFC5` | `HBAR` | Write | `0x000002` | Acknowledge/clear `INTERRUPT_START_FRAME` (`bit 1`) |
| `P:0x02D0` | `jset #4,x:$ffffd6,p:$02d0` | `X:0xFFFFD6` | `DMA_CONTROL` | Read | `0x00`/`0x10`/`0x20` | Wait for `DMA_CONTROL_RUNNING` (`bit 4 = 0x10`) to clear before building descriptor |
| `P:0x02D2` | `movep #$000080,x:$ffffc5` | `X:0xFFFFC5` | `HBAR` | Write | `0x000080` | Clear `INTERRUPT_DMA_EOL` (`bit 7 = 0x80`) in `HBAR` |
| `P:0x02E4` | `movep a,x:$ffffd4` | `X:0xFFFFD4` | `DMA_NEXT_BLOCK` | Write | `0x0029A2` | Point `DMA_NEXT_BLOCK` to descriptor at `P:0x01A2` (`0x2800 + 0x01A2 = 0x29A2`) |
| `P:0x02D5` | `movep #$000001,x:$ffffd6` | `X:0xFFFFD6` | `DMA_CONTROL` | Write | `0x000001` | Trigger DMA (`DMA_CONTROL_ACTION_START = 1`) |
| `P:0x02D7` | `jclr #4,x:$ffffd6,p:$02d7` | `X:0xFFFFD6` | `DMA_CONTROL` | Read | `0x000010` | Confirm `DMA_CONTROL_RUNNING` (`bit 4 = 0x10`) asserted |
| `P:0x02DA` | `jset #10,x:$ffffc5,p:$02fe` | `X:0xFFFFC5` | `HBAR` | Read | `0x000080` | Check DMA error flag (`bit 10`); if set, halt at `P:0x02FE` |
| `P:0x02DC` | `jclr #7,x:$ffffc5,p:$02da` | `X:0xFFFFC5` | `HBAR` | Read | `0x000080` | Poll `INTERRUPT_DMA_EOL` (`bit 7 = 0x80`) for synchronous DMA completion |
| `P:0x02DE` | `movep #$000080,x:$ffffc5` | `X:0xFFFFC5` | `HBAR` | Write | `0x000080` | Clear `INTERRUPT_DMA_EOL` (`bit 7 = 0x80`) |
| `P:0x00B6` | `move x:$ffffb3,a` | `X:0xFFFFB3` | Timer Counter | Read | `0x......` | Read hardware cycle/timer counter at end of block (`P:0x0004 = end - start`) |

---

### 4.3 Complete Catalog of 7-Word DMA Descriptors Executed Across the 6-Block AC-3 Cycle (`222` Transfers)

Every DMA transfer in `dolby_ep.bin` is constructed in-place at **`P:0x01A2..0x01A8`** (`DMA_NEXT_BLOCK = 0x0029A2`, `Word[0] = 0x0041A2` with `EOL = 1`, `Word[5] = 0x000000`, `Word[6] = 0x000000`). The table below lists every distinct 7-word DMA descriptor executed across an 8-subframe / 6-block AC-3 frame (`Blocks 0..5`) and in Overlays `#5` and `#6`:

| Seq (Blk 0) | Blocks Executed | `P:0x0180` Cmd (`B1`) | Caller PC | `Word[1]` (`control`) | Direction & `buf_id` | Format & Intlv | `Word[2]` (`count`) | `Word[3]` (`dsp_offset`) | Decoded DSP Target | `Word[4]` (`sc_off` bytes) | Purpose |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`[000]`** | Blk `0` only (`1x`) | `Cmd 0x0A` (`B=0`) | `P:0x0042` | `0x005BE0` | Scratch -> DSP (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x000018` (`24`) | `0x000BC0` | `X:0x0BC0..0x0BD7` | `0x007120` (word `7240`) | Load 24-word Configuration Struct |
| **`[001]`** | Blk `0..5` (`6x`) | `Cmd 0x01` (`B=1`) | `P:0x004E` | `0x005BE0` | Scratch -> DSP (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x0002F2` (`754`) | `0x002B00` | `P:0x0300..0x05F1` | `0x000920` (word `584`) | Load **Overlay #1** (Lt/Rt Pro Logic Downmixer) |
| **`[002..006]`** | Blk `0..5` (`5x/blk` = `30x`) | `Cmd 0x07` (`N1=0..4`) | `P:0x03B3..0x03D3` | `0x005BE0` | Scratch -> DSP (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x000100` (`256`) | `0x000000..0x000400` | `X:0x0000..0x04FF` | Bank0: `0x8000..0xA000`<br>Bank1: `0x8400..0xA400` | Read 256 PCM samples x 5 channels (`FL, C, FR, SL, SR`) for Downmixer |
| **`[042]`** | Blk `1..5` (`5x`) | `Cmd 0x11` (`B=1`) | `P:0x05BC` | `0x005BE0` | Scratch -> DSP (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x00004E` (`78`) | `0x00063D` | `X:0x063D..0x068A` | `0x010000` (word `0x4000`) | Restore Overlay #1 biquad filter state (`Blk 1..5`) |
| **`[007]`** | Blk `0..5` (`6x`) | `Cmd 0x11` (`B=2`) | `P:0x05C7` | `0x005BE2` | DSP -> Scratch (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x00004E` (`78`) | `0x00063D` | `X:0x063D..0x068A` | `0x010000` (word `0x4000`) | Save Overlay #1 biquad filter state |
| **`[008]`** | Blk `0..5` (`6x`) | `Cmd 0x08` (`B=0, N1=0`) | `P:0x0429` | `0x400623` | DSP -> **FIFO1** (`buf_id=1`) | 16-bit, **`Intlv=1`** (`2 ch`) | `0x001001` (`256x2`) | `0x000400` | `X:0x0400` (Lt), `X:0x0500` (Rt) | *(FIFO1 hardware ring)* | Write 256 interleaved 16-bit Lt/Rt samples to **Output FIFO 1** |
| **`[009]`** | Blk `0..5` (`6x`) | `Cmd 0x02` (`B=1`) | `P:0x007F` | `0x005BE0` | Scratch -> DSP (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x000862` (`2146`) | `0x002B00` | `P:0x0300..0x0B61` | `0x0014E8` (word `1338`) | Load **Overlay #2** (AC-3 512-Pt MDCT Filterbank) |
| **`[046]`** | Blk `1..5` (`5x`) | `Cmd 0x12` (`B=1`) | `P:0x0AED` | `0x005BE0` | Scratch -> DSP (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x000416` (`1046`) | `0x0004FD` | `X:0x04FD..0x0912` | `0x010138` (word `0x404E`) | Restore 6-channel 256-sample MDCT overlap history (`Blk 1..5`) |
| **`[047]`** | Blk `1..5` (`5x`) | `Cmd 0x19` (`B=1`) | `P:0x0B13` | `0x005BE0` | Scratch -> DSP (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x000080` (`128`) | `0x000B40` | `X:0x0B40..0x0BBF` | `0x012E10` (word `0x4B84`) | Restore 128-word AC-3 encoder state (`Blk 1..5`) |
| **`[010,013..025]`** | Blk `0..5` (`6x/blk` = `36x`) | `Cmd 0x07` (`N1=0..5`) | `P:0x03EB` | `0x005BE0` | Scratch -> DSP (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x000100` (`256`) | `0x000080` | `X:0x0080..0x017F` | Bank0: `0x8000..0xA800`<br>Bank1: `0x8400..0xAC00` | Read 256 PCM samples for channel `N1` (`FL, C, FR, SL, SR, LFE`) into MDCT input window |
| **`[011,014..023]`** | Blk `0..5` (`5x/blk` = `30x`) | `Cmd 0x15` (`B=2`, `ch 0..4`) | `P:0x0B43` | `0x005BE2` | DSP -> Scratch (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x0000B5` (`181`) | `0x000180` | `X:0x0180..0x0234` | `0x11190 + ch*0x2D4` | Write 181 MDCT coefficient words for full-bandwidth channel `0..4` |
| **`[012,015..024]`** | Blk `0..5` (`5x/blk` = `30x`) | `Cmd 0x16` (`B=2`, `ch 0..4`) | `P:0x0B5D` | `0x005BE2` | DSP -> Scratch (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x0000B5` (`181`) | `0x000241` | `X:0x0241..0x02F5` | `0x11FD0 + ch*0x2D4` | Write 181 exponent words for full-bandwidth channel `0..4` |
| **`[026]`** | Blk `0..5` (`6x`) | `Cmd 0x15` (`B=2`, `LFE ch 5`) | `P:0x0B43` | `0x005BE2` | DSP -> Scratch (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x000007` (`7`) | `0x000235` | `X:0x0235..0x023B` | `0x011FB4` (word `0x47ED`) | Write 7 LFE MDCT coefficient words |
| **`[027]`** | Blk `0..5` (`6x`) | `Cmd 0x16` (`B=2`, `LFE ch 5`) | `P:0x0B5D` | `0x005BE2` | DSP -> Scratch (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x000007` (`7`) | `0x0002F6` | `X:0x02F6..0x02FC` | `0x012DF4` (word `0x4B7D`) | Write 7 LFE exponent words |
| **`[028]`** | Blk `0..5` (`6x`) | `Cmd 0x12` (`B=2`) | `P:0x0AFB` | `0x005BE2` | DSP -> Scratch (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x000416` (`1046`) | `0x0004FD` | `X:0x04FD..0x0912` | `0x010138` (word `0x404E`) | Save 6-channel 256-sample MDCT overlap history |
| **`[029]`** | Blk `0..5` (`6x`) | `Cmd 0x03` (`B=1`) | `P:0x0090` | `0x005BE0` | Scratch -> DSP (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x000652` (`1618`) | `0x002B00` | `P:0x0300..0x0951` | `0x003670` (word `3484`) | Load **Overlay #3** (Psychoacoustic Masking & Bit Allocation) |
| **`[030]`** | Blk `0..5` (`6x`) | `Cmd 0x16` (`B=1`) | `P:0x094B` | `0x005BE0` | Scratch -> DSP (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x000390` (`912`) | `0x0000FA` | `X:0x00FA..0x0489` | `0x011FD0` (word `0x47F4`) | Read all 6 channels' exponents (`5*181 + 7 = 912` words) into Overlay #3 |
| **`[031]`** | Blk `0..5` (`6x`) | `Cmd 0x04` (`B=0`) | `P:0x009D` | `0x005BE0` | Scratch -> DSP (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x0006D3` (`1747`) | `0x002B00` | `P:0x0300..0x09D2` | `0x004FB8` (word `5102`) | Load **Overlay #4** (Mantissa Quantizer, Bitstream Packer & CRC) |
| **`[032]`** | Blk `0..5` (`6x`) | `Cmd 0x15` (`B=1`) | `P:0x097F` | `0x005BE0` | Scratch -> DSP (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x000390` (`912`) | `0x000100` | `X:0x0100..0x048F` | `0x011190` (word `0x4464`) | Read all 6 channels' MDCT coefficients (`912` words) into Overlay #4 |
| **`[033]`** | Blk `0` only (`1x`) | `Cmd 0x0C` (`B=0`) | `P:0x0991` | `0x004602` | DSP -> **FIFO0** (`buf_id=0`) | 16-bit, `Intlv=0` | `0x0005DA` (`1498`) | `0x00401F` | **Bit-14 Zero Hole** (`0x401F`) | *(FIFO0 hardware ring)* | Write `1,498` 16-bit zero words to **Output FIFO 0** (IEC 61937 gap) |
| **`[034]`** | Blk `0..5` (`6x`) | `Cmd 0x09` (`B=2`) | `P:0x09AA` | `0x004602` | DSP -> **FIFO0** (`buf_id=0`) | 16-bit, `Intlv=0` | `0xDC` (Blk 0)<br>`0xD6` (Blk 1,5)<br>`0xD4` (Blk 2..4) | `0x00001F` | `X:0x001F..0x00FA` | *(FIFO0 hardware ring)* | Write packed 16-bit IEC 61937 / AC-3 bitstream block (`1,284` words total across Blk `0..5`) to **Output FIFO 0** |
| **`[220]`** | Blk `5` only (`1x`) | `Cmd 0x0C` (`B=1`) | `P:0x09B8` | `0x004602` | DSP -> **FIFO0** (`buf_id=0`) | 16-bit, `Intlv=0` | `0x000122` (`290`) | `0x00401F` | **Bit-14 Zero Hole** (`0x401F`) | *(FIFO0 hardware ring)* | Write `290` 16-bit trailing zero words to **Output FIFO 0** (`1498 + 1284 + 290 = 3072` words = `6144` bytes) |
| **`[035]`** | Blk `0..5` (`6x`) | `Cmd 0x19` (`B=2`) | `P:0x09C3` | `0x005BE2` | DSP -> Scratch (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x000080` (`128`) | `0x000B40` | `X:0x0B40..0x0BBF` | `0x012E10` (word `0x4B84`) | Save 128-word AC-3 Bitstream Packer state |
| **`Pass 4A [001]`** | When `Config[3]=1` | `Cmd 0x05` (`B=1`) | `P:0x006E` | `0x005BE0` | Scratch -> DSP (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x0000BC` (`188`) | `0x002B00` | `P:0x0300..0x03BB` | `0x006B08` (word `6850`) | Load **Overlay #5** (Mono `Lo=Ro` 5-to-2 Downmixer) |
| **`Pass 4B [001]`** | When `Config[3]=2` | `Cmd 0x06` (`B=2`) | `P:0x006E` | `0x005BE0` | Scratch -> DSP (`0xF` Linear) | 24-bit LSB, `Intlv=0` | `0x0000C9` (`201`) | `0x002B00` | `P:0x0300..0x03C8` | `0x006DF8` (word `7038`) | Load **Overlay #6** (Stereo `Lo/Ro` 5-to-2 Downmixer) |

---

### 4.4 Exact Subroutine Cycle Profile Across a Full 6-Block AC-3 Frame (`4,793,740` Total Cycles)

Measured deterministically by `tools/trace_ep_rom.c` across a full 6-block (`1,536`-sample) AC-3 cycle (`Pass 3`) and in `Pass 4A`/`4B`:

#### A. Resident Executive & DMA/Heap Kernel (`P:0x0000..0x02FF` — Housekeeping & DMA)

| Subroutine PC | Segment | Calls (6 Blks) | Self Insts | Self Cycles | Inclusive Cycles (6 Blks) | Avg Cycles / Block | Classification & Exact Role |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`P:0x0000`** | Segment 1 | `1` (loop) | `24,125` | `96,768` | `96,768` (self) | `~420` active + wait | **Executive Supervisor Loop:** Frame sync (`P:0x0032`), overlay dispatch (`P:0x004E..0x00A6`), mod-6 block counter (`P:0x0001`), watchdog (`P:0x00B6..0x00C0`) |
| **`P:0x0180`** | Segment 2 | `252` | `6,944` | `18,630` | `57,516` | `9,586` (`228` cyc/DMA) | **DMA & Heap Service Gateway:** Dispatches `Cmd 0x00..0x19` |
| **`P:0x0224`** | Segment 2 | `94` | `658` | `2,444` | `16,350` | `2,725` | Scratch -> DSP X-RAM DMA launcher (`P:0x0276` + `P:0x02E1`) |
| **`P:0x022E`** | Segment 2 | `90` | `630` | `2,340` | `15,660` | `2,610` | DSP X-RAM -> Scratch DMA launcher (`P:0x0288` + `P:0x02E1`) |
| **`P:0x0238`** | Segment 2 | `14` | `148` | `464` | `2,660` | `443` | Output FIFO (`FIFO0`/`FIFO1`) DMA launcher (`P:0x029A`/`0x02B1` + `P:0x02E1`) |
| **`P:0x0260`** | Segment 2 | `24` | `168` | `624` | `4,176` | `696` | Scratch -> DSP P-RAM Overlay DMA launcher (`R0 + 0x2800`) |
| **`P:0x0276`** | Segment 2 | `118` | `1,652` | `6,608` | `9,198` | `1,533` | Builds 7-word Scratch -> DSP read descriptor (`ctrl=0x5BE0`) at `P:0x01A2` |
| **`P:0x0288`** | Segment 2 | `90` | `1,260` | `5,040` | `7,020` | `1,170` | Builds 7-word DSP -> Scratch write descriptor (`ctrl=0x5BE2`) at `P:0x01A2` |
| **`P:0x029A`** | Segment 2 | `8` | `152` | `496` | `672` | `112` | Builds 16-bit non-interleaved FIFO descriptor (`ctrl=0x0602 \| (r1<<5)`) at `P:0x01A2` |
| **`P:0x02B1`** | Segment 2 | `6` | `162` | `468` | `600` | `100` | Builds 16-bit interleaved stereo FIFO descriptor (`ctrl=0x400603 \| (r1<<5)`) at `P:0x01A2` |
| **`P:0x02D0`** | Segment 2 | `222` | `887` | `4,878` | `4,878` | `813` | Polls `DMA_CONTROL` (`0xFFFFD6` bit 4) idle & clears `HBAR` bit 7 (`0x80`) |
| **`P:0x02D5`** | Segment 2 | `222` | `666` | `3,552` | `3,552` | `592` | Writes `0x01` to `DMA_CONTROL` (`0xFFFFD6`) & waits for bit 4 (`RUNNING`) |
| **`P:0x02DA`** | Segment 2 | `220` | `880` | `4,840` | `4,840` | `807` | Polls `HBAR` (`0xFFFFC5`) bit 10 (error) & bit 7 (`DMA_EOL`), clears bit 7 |
| **`P:0x02E1`** | Segment 2 | `222` | `2,218` | `7,092` | `15,484` | `2,581` | Writes `0x29A2` to `DMA_NEXT_BLOCK` (`0xFFFFD4`), calls `P:0x02D5` & `P:0x02DA` |
| **`P:0x02EF`** | Segment 2 | `1` | `8` | `40` | `40` | *(Boot only)* | Cold-boot peripheral & DMA register reset (`0xFFFFC4..0xFFFFD7`) |

#### B. Relocatable Overlays `#1..#6` (`P:0x0300..` — Heavy Signal Processing & AC-3 Math)

| Overlay & Subroutine PC | Calls (6 Blks) | Inclusive Cycles (6 Blks) | Avg Cycles / Block | % of Total DSP Compute | Exact Mathematical / Signal-Processing Function |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Overlay #1 (`P:0x0304`)** | `6` | **`529,350`** | **`88,225`** | **11.27%** | **Dolby Surround (Lt/Rt) 5-to-2 Matrix Downmixer & Biquad Filter** |
| └─ `P:0x03B3..0x03D3` (`Cmd 0x07` x5) | `30` | `8,100` | `1,350` | 0.17% | DMAs 5 discrete PCM channels (`FL, C, FR, SL, SR`) into `X:0x0000..0x04FF` |
| └─ `P:0x043A` (Input gain scale) | `6` | `30,804` | `5,134` | 0.66% | Scales 5 channels (`1,280` samples) by attenuation factor from `P:0x0305` table |
| └─ `P:0x0443` (Matrix + Biquads) | `6` | `455,440` | `75,907` | 9.70% | 5-to-2 Pro Logic matrixing (`P:0x0479..0x050A`) + 3x cascaded all-pass/bandpass biquad filters (`P:0x05CA`: `401,748` cyc) + `Cmd 0x11` state DMA |
| └─ `P:0x03EA..0x0420` (Peak/RMS) | `6` | `~32,600` | `~5,433` | 0.69% | Computes Lt/Rt Peak and 23-iteration Newton-Raphson `sqrt(RMS)` (`X:0x062F..0x0632`) |
| └─ `P:0x0429..0x0438` (`Cmd 0x08/0x0F`)| `18` | `2,796` | `466` | 0.06% | Outputs 256 Lt/Rt stereo samples to `FIFO1` (`Cmd 0x08`) & 4 meter words (`Cmd 0x0F`) |
| **Overlay #2 (`P:0x0304`)** | `6` | **`1,402,202`** | **`233,700`** | **29.85%** | **AC-3 512-Point Windowed MDCT Filterbank & Exponent Extractor (6 Channels)** |
| └─ `P:0x0AD9`/`0x0AE4`/`0x0B0A`/`0x0B18`| `12` | `2,978` | `496` | 0.06% | Allocates (`Blk 0`) or restores (`Blk 1..5`) `Cmd 0x12`, `0x15`, `0x16`, `0x19` buffers |
| └─ `P:0x03D9` (`Cmd 0x07` per ch) | `36` | `11,520` | `1,920` | 0.25% | DMAs 256 PCM samples for each of the 6 channels (`FL, C, FR, SL, SR, LFE`) |
| └─ `P:0x0339` -> `P:0x062B` (MDCT) | `36` | `1,402,814` | `233,802` | 29.87% | **Core 6-Channel Filterbank:** High-pass/low-pass Biquads (`P:0x07C6`: `79,272` cyc, `P:0x080B`: `124,668` cyc, `P:0x0898`: `346,680` cyc), Block-Switching Transient Detector (`P:0x0850`: `71,712` cyc), Window + Overlap (`P:0x03EF`: `248,992` cyc), Pre-IFFT Twiddle (`P:0x03D7`), **Radix-2 Complex IFFT (`P:0x03F1`: `248,398` cyc)**, Post-Twiddle (`P:0x0596`: `200,870` cyc), Exponent `clb` Extraction (`P:0x0A0C`: `249,844` cyc) |
| └─ `P:0x0B2D` (`Cmd 0x15` & `0x16`) | `36` | `19,872` | `3,312` | 0.42% | Writes 6 channels' MDCT coefficients (`Cmd 0x15`) and exponents (`Cmd 0x16`) to Scratch |
| └─ `P:0x0AF2` (`Cmd 0x12` & `0x0E`) | `6` | `2,892` | `482` | 0.06% | Saves 1046-word MDCT overlap history (`Cmd 0x12`) & 12 Peak/RMS meter words (`Cmd 0x0E`) |
| **Overlay #3 (`P:0x0304`)** | `6` | **`2,208,652`** | **`368,109`** | **47.02%** | **AC-3 Exponent Strategy, Psychoacoustic Masking & Bit Allocation (`BAP`)** |
| └─ `P:0x0942` (`Cmd 0x16` read) | `6` | `1,584` | `264` | 0.03% | Reads 912 words of 6-channel exponents from Scratch into `X:0x00FA..0x0489` |
| └─ `P:0x053F` (Psychoacoustic Core) | `6` | `2,206,348` | `367,725` | 46.97% | **Psychoacoustic Bit Allocation:** Exponent strategy & grouping (`P:0x08F3`: `32,520` cyc; `P:0x0582`: `300,414` cyc), Banding & Excitation/Masking Curve (`P:0x0718` -> `P:0x073B` -> `P:0x07D4` -> **`P:0x0486`: `1,810,296` cyc**), SNR Offset Binary Search (`csnroffst`/`fsnroffst`) |
| **Overlay #4 (`P:0x0304`)** | `6` | **`519,432`** | **`86,572`** | **11.06%** | **AC-3 Mantissa Quantizer, IEC 61937 / BSI Bitstream Packer & CRC-16 (`CRC1`/`CRC2`)** |
| └─ `P:0x09C8` & `P:0x0976` (`Cmd 0x19/15`)| `7` | `1,698` | `283` | 0.04% | Allocates `Cmd 0x19` (`Blk 0`) & reads 912 MDCT coefficients (`Cmd 0x15`) into `X:0x0100` |
| └─ `P:0x04D0` (Quantize + Pack + CRC)| `6` | `510,036` | `85,006` | 10.86% | BAP-guided Mantissa Quantization (`P:0x062F`: `250,916` cyc; `P:0x0870` -> `P:0x048B`: `177,484` cyc), Bitstream Packing (`P:0x06A6`: `39,790` cyc via `P:0x0361`/`0x036F`/`0x0381`/`0x0393`), **CRC-16 Generator (`P:0x08E9` -> `P:0x03B3`/`0x03CA`: `38,992` cyc)** |
| └─ `P:0x0984` (`Cmd 0x0C/0x09/0x19`) | `6` | `5,494` | `916` | 0.12% | Writes IEC 61937 zero-pad (`Cmd 0x0C`), AC-3 bitstream (`Cmd 0x09`) to `FIFO0`, saves `Cmd 0x19` |
| **Overlay #5 (`P:0x0304`, Pass 4A)** | `1`/blk | *(Alt mode)* | **`12,586`** | — | **Mono (`Lo = Ro`) 5-to-2 Downmixer & Peak/RMS Meter** (`Config[3] == 1`) |
| **Overlay #6 (`P:0x0304`, Pass 4B)** | `1`/blk | *(Alt mode)* | **`15,162`** | — | **Stereo (`Lo / Ro`) 5-to-2 Downmixer & Peak/RMS Meter** (`Config[3] == 2`) |

> **Key Takeaway from the Cycle Profile:**  
> Overlays `#1`, `#2`, `#3`, and `#4` consume **`4,659,636` out of `4,756,404` active cycles (`97.97%`)** per 6-block AC-3 frame (`~776,606` cycles per 256-sample block, or `~97,075` cycles per 32-sample subframe—nearly **8x higher** than Xemu Symphony's bounded `EP_SUBFRAME_CYCLES = 12,800` budget!). Meanwhile, the Resident Executive (`P:0x0000..0x00C7`) and Service Kernel (`P:0x0180..0x02FF`) consume only **`~1,600` cycles per block** when not executing AC-3 math overlays.

---

## 5. Exact Clean-Room Specification for `symphony_ep.bin` (`symphony_ep_rom.h`)

### 5.1 Architectural Requirements for Xemu Symphony

To replace `tools/dolby_ep.bin` with a clean-room, open-source micro-kernel (`symphony_ep.bin` / `symphony_ep_rom.h`) that guarantees 100% discrete 5.1 surround sound in Xemu Symphony while eliminating all host CPU overhead from AC-3 MDCT/psychoacoustic/Huffman math:

1. **Binary Size & Segment Layout Invariant (`>= 583` Words / `>= 2,332` Bytes):**
   - `dsp_bootstrap_ep_firmware()` (`hw/xbox/mcpx/apu/dsp/dsp.c:351`) requires `total_words >= (0xC8 + 0x17F)` (**`583` words / `2,332` bytes**; `584` words / `2,336` bytes for full `0xC8 + 0x180` alignment).
   - `mcpx_apu_monitor_init()` (`hw/xbox/mcpx/apu/monitor.c:88`) requires **`P:0x0000 != 0x000000`** (e.g., `0x050C08` `bra p:$0008`) at boot so `fw_present = true` and **`d->is_5_1_active = true`**, enabling the 6-channel discrete `surround_buf` pipeline in `gp_ep.c:544-579`.

2. **Subroutines & State Locations That MUST Be Preserved / Reimplemented Clean-Room:**

| Address Range | Component | Clean-Room Requirement & Rationale |
| :--- | :--- | :--- |
| **`P:0x0000`** | Reset Vector | Must contain a non-zero branch instruction (`0x050C08` `bra p:$0008`) to satisfy `monitor.c:88` (`reset_vec != 0`) and jump past the status mailbox words `P:0x0001..0x0007`. |
| **`P:0x0001..0x0007`** | Executive Status Mailbox | Maintain the 7-word status block readable by the guest via MMIO (`ep_read` at `0x50000+`):<br>- `P:0x0001`: Modulo-6 block counter (`0..5`, incremented each frame)<br>- `P:0x0002`: `0x000000` (Overlay #1 init complete)<br>- `P:0x0003`: `0x000000` (Overlays #2..#4 init complete)<br>- `P:0x0004`: Last block cycle delta (`< 0x0ACDA0`, e.g., `0x000100`)<br>- `P:0x0005`: Start timestamp (`X:0xFFFFB3`)<br>- `P:0x0006`: `0xCCCCCC` (Healthy watchdog signature)<br>- `P:0x0007`: Frame heartbeat counter (incremented every frame) |
| **`P:0x0008..0x0038`** | Boot & Frame Sync | Initialize `SP = 0`, `MR = 0x20`, peripheral registers (`HBAR = 0x0FFF`, `HPCR = 0x0001`, `DMA_START_BLOCK = 0x4000`, `DMA_NEXT_BLOCK = 0x4000`, `DMA_CONFIG = 0`), increment `P:0x0007`, modulo-6 advance `P:0x0001`, toggle `P:0x0116` bit 8, and **yield via `WAIT` (`0x000086`)** so `dsp56k_execute_instruction()` immediately sets `core.is_idle = true` and returns control to QEMU in `< 50` cycles per subframe (instead of burning `12,800` cycles spinning on `jclr #1, x:$ffffc5, p:$0031`). |
| **`P:0x0100..0x013F`** | Descriptor & Heap Table | Reserved zero-initialized P-RAM table populated by guest XDK (`Cmd 0x01..0x19` `(base, size)` pairs). |
| **`P:0x0180..0x02FF`** | Service Kernel & Meter Mailbox (`P:0x01AB..0x01BA`) | Clean-room implementation of `P:0x0180` supporting:<br>- `Cmd 0x00` (`P:0x0214` / `P:0x02EF`): Copy `P:0x0122..0x0123` to `P:0x019E..0x019F` and reset `HBAR`/`HPCR`/DMA registers.<br>- `Cmd 0x0A`: Optional 24-word Config DMA (`P:0x0114..0x0115` -> `X:0x0BC0..0x0BD7`).<br>- `Cmd 0x0B`: Toggle Ping-Pong bank bit 8 (`0x000100`) in `P:0x0116`.<br>- `P:0x01AB..0x01BA`: 16-word Peak/RMS meter mailbox (`P:0x01AB..0x01AE` and `P:0x01AF..0x01BA`). |

3. **AC-3 & Downmix Subroutines That Can Be Short-Circuited (`rts` `0x00000C` or Omitted):**

| Overlay / Subroutine | Cycle Cost in `dolby_ep.bin` | Why It Can Be Short-Circuited in Xemu Symphony |
| :--- | :--- | :--- |
| **Overlay #1 (`P:0x0304`, `Cmd 0x01`)**<br>- `P:0x043A` (Input gain)<br>- `P:0x0443` / `P:0x05CA` (Pro Logic Lt/Rt matrix & biquad filters) | `88,225` cycles / block (`529,350` cycles / 6 blocks) | Xemu Symphony taps discrete 6-channel 32-bit float LPCM directly from GP Mixbins (`X:0x1500..0x15BF`) + GP 2D Stereo (`X:0x1400`/`0x1420`) in `gp_ep.c:544-579` and never reads the EP's 2-channel Pro Logic Lt/Rt downmix (`FIFO1`). Can be short-circuited with `rts` (`0x00000C`). |
| **Overlay #2 (`P:0x0304`, `Cmd 0x02`)**<br>- `P:0x0339` / `P:0x062B` (512-pt MDCT, Biquads, IFFT `P:0x03F1`, Exponents `P:0x0A0C`) | `233,700` cycles / block (`1,402,202` cycles / 6 blocks) | Pure ATSC A/52 time-to-frequency MDCT transform and exponent extraction for S/PDIF AC-3 compression. Xemu Symphony's Direct Lossless Audio Pipeline bypasses AC-3 compression entirely. Can be short-circuited with `rts` (`0x00000C`). |
| **Overlay #3 (`P:0x0304`, `Cmd 0x03`)**<br>- `P:0x053F` / `P:0x0582` / `P:0x0718` / `P:0x0486` (Psychoacoustic masking & BAP) | `368,109` cycles / block (`2,208,652` cycles / 6 blocks) | Pure ATSC A/52 psychoacoustic masking model and SNR offset bit allocation search. Zero host or guest side effects outside AC-3 mantissa quantization. Can be short-circuited with `rts` (`0x00000C`). |
| **Overlay #4 (`P:0x0304`, `Cmd 0x04`)**<br>- `P:0x04D0` / `P:0x062F` / `P:0x06A6` / `P:0x0870` / `P:0x08E9` (Quantizer, Bitstream Packer & CRC-16) | `86,572` cycles / block (`519,432` cycles / 6 blocks) | Pure ATSC A/52 mantissa quantization, IEC 61937 bitstream packing, and CRC-16 calculation for virtual S/PDIF (`FIFO0`). Can be short-circuited with `rts` (`0x00000C`). |
| **Overlays #5 & #6 (`P:0x0304`, `Cmd 0x05` & `0x06`)** | `12,586` / `15,162` cycles / block | Fallback 5-to-2 Mono/Stereo PCM downmixers for `FIFO1`; unused by Xemu Symphony's direct 5.1 mixer tap. Can be short-circuited with `rts` (`0x00000C`). |

