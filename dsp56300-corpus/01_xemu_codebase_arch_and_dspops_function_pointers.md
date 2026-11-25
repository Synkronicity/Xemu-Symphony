# Architectural Reconnaissance: Phase 66 Harmonization

**Target**: Harmonization of Native 5.1 Discrete Audio Pipeline (`feature/ep-native-5.1`) with Upstream Pluggable DSP Engine Rewrite (PR #2801 / Commit `429c997`)  
**Scope**: `hw/xbox/mcpx/apu/` (`apu.c`, `apu_int.h`, `monitor.c`, `dsp/dsp.c`, `dsp/dsp.h`, `dsp/dsp_c.c`, `dsp/dsp_jit.c`, `dsp/gp_ep.c`, `dsp/interp/dsp_cpu.c`)  
**Mode**: Read-Only Architectural Audit & Integration Blueprint  

---

## Executive Overview

Upstream PR #2801 (`429c997`) introduces a major architectural rewrite of Xemu's digital signal processing subsystem. The previous monolithic, C-interpreter-only DSP architecture has been refactored into a pluggable, multi-backend abstraction driven by a function-pointer dispatch table (`struct DSPOps`). This new abstraction supports two execution backends:
1. **The Legacy C Interpreter Backend (`c_dsp_ops` / `dsp_c.c`)**: Wrapping the modified Hatari/ARAnyM `dsp_core_t`.
2. **The New Rust JIT Backend (`jit_dsp_ops` / `dsp_jit.c`)**: Wrapping external `libdsp56300` (`dsp56300.h`) for high-performance just-in-time compilation.

While PR #2801 delivers engine hot-swapping and JIT acceleration, **upstream completely lacks discrete multi-channel surround sound extraction, EP microcode execution, and host audio output routing**. Upstream's audio pipeline remains hardcoded to 2-channel stereo. Furthermore, upstream EP cycle execution is an empty loop whose transform data is completely discarded.

This document details the architectural collisions, technical gaps, and unified migration paths required to merge our verified Phase 1–65 LLE Motorola DSP56362 audio pipeline into upstream's new pluggable DSP engine.

---

## 1. Memory Access Abstraction Collision

### 1.1 The Collision: Direct Pointer Dereference vs. Pluggable Backends

In our `feature/ep-native-5.1` branch, memory transfers and buffer inspections inside `gp_ep.c` frequently accessed internal state structures directly via raw pointers:
```c
/* Direct access in feature/ep-native-5.1 */
dsp_core_t *core = (dsp_core_t *)d->ep.dsp->backend;
core->xram[address] = value;
uint32_t sample = core->xram[0x1400 + (ch * 0x200) + i];
```

In upstream PR #2801, **direct pointer access to core memory arrays is completely broken when running under the JIT backend**:
* Upstream's `DSPState` encapsulates backends behind `void *backend`.
* In `dsp_c.c`, `backend` is a `dsp_core_t *` allocated on the heap.
* In `dsp_jit.c`, `backend` is a `JitBackend *` wrapping a separate Rust-managed `Dsp56300Jit` instance.
* While `DSPState.core` contains a `DspCoreState` mirror struct, **this mirror is NOT updated during real-time execution**. It is only populated when `dsp_sync_to_vm()` is explicitly invoked prior to VM snapshot serialization or engine swapping.
* Any direct read from `d->ep.dsp->core.xram` during active audio frames will read **stale uninitialized memory** under the JIT engine.

### 1.2 Upstream Abstraction Layer: `DSPOps` Accessors

Upstream introduced uniform, backend-independent memory accessors declared in `dsp.h` and dispatched through `DSPOps`:
```c
uint32_t dsp_read_memory(DSPState *dsp, char space, uint32_t address);
void dsp_write_memory(DSPState *dsp, char space, uint32_t address, uint32_t value);
```

Behind the scenes:
* For `dsp_c`: Dispatches to `dsp_c_read_memory()` $\rightarrow$ `dsp56k_read_memory(c_core(dsp), space, addr)`.
* For `dsp_jit`: Dispatches to `dsp56300_read_memory(jit_be(dsp)->jit, space, addr)`.

### 1.3 Complete Audit of Pointer Dereferences in `gp_ep.c`

To harmonize with upstream, every direct memory dereference in `gp_ep.c` must be translated to `DSPOps` accessors:

| Location / Operation in `gp_ep.c` | `feature/ep-native-5.1` (Direct) | Upstream PR #2801 Compliant Translation |
| :--- | :--- | :--- |
| **MIXBUF Ingestion** (VP $\rightarrow$ GP) | `dsp_write_memory(d->gp.dsp, 'X', base + sample, float_to_24b(...))` | **Identical**: `dsp_write_memory()` already matches `DSPOps`. |
| **Multichannel Stride Input** (GP $\rightarrow$ EP) | `d->ep.dsp->ep_dma.ext_mem[pair_base + sample] = val;` | Route via generalized aperture: `dsp_write_memory(d->ep.dsp, 'X', 0x4000 + (pair * 0x200) + sample, val);` |
| **Discrete 5.1 Mixbin Scrape** (Monitor) | `uint32_t s = dsp->core.xram[0x1400 + (ch * 0x200) + i];` | `uint32_t s = dsp_read_memory(d->gp.dsp, 'X', 0x1400 + (ch * 0x200) + i);` |
| **EP Mailbox Synchronizer** | `d->ep.dsp->interrupts |= INTERRUPT_START_FRAME;` | Wrap in `dsp_start_frame(d->ep.dsp);` which invokes `ops->start_frame(dsp);`. |
| **EP Reset Vector Presence Check** | `d->ep.dsp->core.pram[0] != 0` | `dsp_read_memory(d->ep.dsp, 'P', 0x0000) != 0` |
| **EP Halt State Polling** | `d->ep.dsp->core.halt_requested` | `dsp_get_halt_requested(d->ep.dsp)` |
| **Cycle Metering** | `g_dbg.ep.cycles = d->ep.dsp->core.cycle_count;` | `g_dbg.ep.cycles = dsp_get_cycle_count(d->ep.dsp);` |

### 1.4 Multichannel 512-Word ($0x0200$) Strides via `DSPOps`

In Phase 62.1, we discovered that the original 32-word ($0x20$) channel stride was a legacy artifact from stereo downmixing. The genuine Motorola DSP56362 AC-3 transform kernel operates across 512-word ($0x0200$) double-buffered memory blocks per channel pair ($0x0000$ = Left/Right, $0x0200$ = Center/LFE, $0x0400$ = Surround Left/Surround Right).

Under `DSPOps`, reading these multichannel strides requires no custom pointers. The 6 discrete channels are scraped systematically using loop-invariant stride offsets:
```c
for (int ch = 0; ch < 6; ch++) {
    uint32_t channel_base = 0x1400 + (ch * 0x0200);
    for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
        uint32_t raw_word = dsp_read_memory(d->gp.dsp, 'X', channel_base + i);
        int16_t sample_16 = (int16_t)(raw_word >> 8);
        d->monitor.surround_buf[off + i][ch] = sample_16;
    }
}
```

---

## 2. Host Audio Output Gap

### 2.1 Upstream Audio Output Analysis

An inspection of upstream commit `429c997` reveals a critical functional deficit: **upstream possesses zero multi-channel audio capability**.

1. **Hardcoded Stereo Stream in `monitor.c`**:
   Upstream initializes SDL3 exclusively with 2 channels:
   ```c
   /* hw/xbox/mcpx/apu/monitor.c:24-28 (Commit 429c997) */
   SDL_AudioSpec spec = {
       .freq = 48000,
       .format = SDL_AUDIO_S16LE,
       .channels = 2,
   };
   ```
2. **Stereo-Locked Frame Buffer in `apu_int.h`**:
   The frame buffer is sized strictly for two channels:
   ```c
   /* hw/xbox/mcpx/apu/apu_int.h:120 (Commit 429c997) */
   int16_t frame_buf[256][2]; // 1 EP frame (0x400 bytes)
   ```
3. **The Upstream EP Black Hole**:
   In upstream `gp_ep.c` (lines 481–494), the Encode Processor is stepped through a run slice:
   ```c
   /* Run EP */
   if ((d->ep.regs[NV_PAPU_EPRST] & NV_PAPU_GPRST_GPRST) &&
       (d->ep.regs[NV_PAPU_EPRST] & NV_PAPU_GPRST_GPDSPRST)) {
       if (d->ep_frame_div % 8 == 0) {
           dsp_start_frame(d->ep.dsp);
           dsp_set_halt_requested(d->ep.dsp, false);
           dsp_set_cycle_count(d->ep.dsp, 0);
           do {
               dsp_run(d->ep.dsp, 1000);
           } while (!dsp_get_halt_requested(d->ep.dsp) && d->ep.realtime);
           g_dbg.ep.cycles = dsp_get_cycle_count(d->ep.dsp);
       }
   }
   ```
   **What upstream does with the resulting EP transform data upon cycle completion**: **Nothing.**  
   The cycles are recorded in telemetry, but no audio samples are extracted from EP memory, no DMA descriptors are parsed, and no buffers are sent to `monitor.c`. Upstream's EP execution is completely orphaned from the host sound card.

### 2.2 Harmonizing the 6-Channel SDL3 Stream Pipeline

To integrate our discrete 5.1 audio output into upstream without regressing stereo compatibility, the following architecture must be deployed:

```
Upstream Audio Stack:
   [GP DSP] ──(Stereo 0x1400)──► frame_buf[256][2] ──► SDL3 AudioStream (2-ch) ──► Host

Phase 66 Harmonized Stack:
   [GP DSP] ──(Discrete 5.1)───► surround_buf[256][6] ──┐
                                                         ├──► Dynamic SDL3 Stream ──► 5.1 Surround
   [EP DSP] ──(AC-3 Transform)─► ext_mem aperture   ───┘     (Falls back to 2-ch if stereo EEPROM)
```

1. **State Tracking in `apu_int.h`**:
   Restore `bool is_5_1_active;` in `MCPXAPUState`.
   Add `int16_t surround_buf[256][6];` to the `monitor` union/struct.
2. **Dynamic SDL3 Stream Negotiation in `monitor.c`**:
   During `mcpx_apu_monitor_init()`, or dynamically upon guest EEPROM mode detection:
   ```c
   int channels = d->is_5_1_active ? 6 : 2;
   SDL_AudioSpec spec = {
       .freq = 48000,
       .format = SDL_AUDIO_S16LE,
       .channels = channels,
   };
   ```
3. **Buffer Geometry & Throttling Adjustments**:
   * Stereo frame size: $256 \times 2 \times 2 = 1,024 \text{ bytes}$.
   * Surround frame size: $256 \times 6 \times 2 = 3,072 \text{ bytes}$.
   * In `apu.c`, the `throttle()` function uses watermarks (`queued_bytes_low` and `queued_bytes_high`) calculated from `frame_bytes`. The calculation must dynamically track `sizeof(d->monitor.surround_buf)` when 5.1 is active; otherwise, the host audio stream will drain 3x faster than the throttle anticipates, causing rhythmic audio underruns.
4. **Channel Mapping Conformity**:
   Ensure extracted samples map 1:1 to DirectSound3D / USB / HDMI 5.1 speaker positions:
   * Channel 0: Front Left (`FL`)
   * Channel 1: Front Right (`FR`)
   * Channel 2: Front Center (`FC`)
   * Channel 3: Low-Frequency Effects (`LFE` / Subwoofer)
   * Channel 4: Surround Left (`SL` / Rear Left)
   * Channel 5: Surround Right (`SR` / Rear Right)

---

## 3. Firmware Staging & DMA Mechanisms

### 3.1 Upstream DMA & Register Enhancements

Upstream commit `429c997` contains two notable additions:
1. **`dma_read_count` Completion Latch in `dsp_dma.c`**:
   ```c
   /* hw/xbox/mcpx/apu/dsp/dsp_dma.c:323-331 */
   case DMA_CONTROL:
       if (s->control & DMA_CONTROL_RUNNING) {
           s->dma_read_count++;
           if (s->dma_read_count > 2) {
               s->control &= ~DMA_CONTROL_RUNNING;
               s->control |= DMA_CONTROL_STOPPED;
               s->dma_read_count = 0;
           }
       }
       return s->control;
   ```
   This accurately models the asynchronous behavior of the hardware DMA controller: software polls `DMA_CONTROL` repeatedly, and after 3 reads the controller transitions to `DMA_CONTROL_STOPPED`.
2. **Global Sample Counter `NV_PAPU_XGSCNT` in `apu.c`**:
   ```c
   /* hw/xbox/mcpx/apu/apu.c:52-54 */
   case NV_PAPU_XGSCNT:
       r = (uint64_t)d->ep_frame_div * NUM_SAMPLES_PER_FRAME;
       break;
   ```
   Returns the running sample counter based on the subframe divider. Guest audio drivers poll this register to calculate playback position.

### 3.2 The Upstream Bootstrap Clashing Bug

In upstream `dsp_c.c` (lines 89–103) and `dsp_jit.c` (lines 86–100), the `bootstrap` method is implemented as follows:
```c
static void dsp_c_bootstrap(DSPState *dsp)
{
    dsp_core_t *core = c_core(dsp);
    // scratch memory is dma'd in to pram by the bootrom
    dsp->dma.scratch_rw(dsp->dma.rw_opaque, (uint8_t *)core->pram, 0, 0x800 * 4, false);
    ...
}
```
**The Critical Flaw**: Upstream does **not** check whether `dsp->is_gp` is true!  
When the guest kernel initializes the APU, if `dsp_bootstrap()` is called on `d->ep.dsp`, upstream blindly overwrites EP P-RAM ($0x0000$–$0x07FF$) with GP scratch memory! This destroys any Dolby microcode loaded into the Encode Processor.

### 3.3 Generalizing the BYOF (Bring Your Own Firmware) Loader behind `DSPOps`

In Phase 63–65, we implemented `dsp_c_bootstrap_ep_firmware()` directly in `dsp_c.c`. To harmonize with upstream, this loader must be decoupled and promoted to the `DSPOps` interface:

```c
typedef struct DSPOps {
    ...
    void (*bootstrap)(DSPState *dsp);
    void (*bootstrap_ep)(DSPState *dsp, const char *fw_path);
    ...
} DSPOps;
```

#### Proposed Backend-Agnostic Bootstrap Logic:
```c
void dsp_bootstrap(DSPState *dsp)
{
    if (dsp->is_gp) {
        /* Standard hardware behavior: DMA 0x800 words from scratch into PRAM */
        dsp->ops->bootstrap(dsp);
    } else {
        /* EP Bootstrap: Check if guest kernel already populated P-RAM */
        uint32_t reset_vec = dsp_read_memory(dsp, 'P', 0x0000);
        if (reset_vec == 0 || reset_vec == 0xCACACA) {
            /* P-RAM is empty; invoke the BYOF fallback search hierarchy */
            dsp_bootstrap_ep_firmware(dsp);
        }
    }
}
```

#### Universal Word-Level Loader via `dsp_write_memory()`:
Instead of writing directly to `core->pram` pointers, the BYOF loader will parse the 32-bit LE microcode dump and load words using `dsp_write_memory(dsp, 'P', addr, word)`.  
**Advantage**: This immediately enables microcode loading into **both** the C interpreter and the Rust JIT backend simultaneously, requiring zero JIT-specific loader modifications.

---

## 4. Stepping & Telemetry Integration

### 4.1 Cycle Metering & Per-Slice Telemetry

* **Our Phase 64 Implementation**:
  Computed slice deltas:
  $$\Delta \text{Cycles} = \text{CycleCount}_{\text{end}} - \text{CycleCount}_{\text{start}}$$
* **Upstream PR #2801**:
  Standardized on `dsp_get_cycle_count(DSPState *dsp)` and `dsp_set_cycle_count(DSPState *dsp, uint32_t count)`.
  In upstream `gp_ep.c`:
  ```c
  dsp_set_cycle_count(d->ep.dsp, 0);
  dsp_run(d->ep.dsp, 1000);
  g_dbg.ep.cycles = dsp_get_cycle_count(d->ep.dsp);
  ```
* **Harmonization Decision**:
  Adopt upstream's `dsp_set_cycle_count(..., 0)` prior to execution and `dsp_get_cycle_count(...)` post-execution. This allows `DSPOps` to map cleanly to both `core->cycle_count` in the interpreter and `dsp56300_get_cycle_count()` in the JIT.

### 4.2 Unifying Stepping and Halt-State Polling Across Engines

A key architectural challenge of PR #2801 is synchronizing the run-loop behavior between interpreter and JIT:

| Engine | Stepping Function | Halt / Idle Mechanism | End-of-Frame Synchronization |
| :--- | :--- | :--- | :--- |
| **C Interpreter** (`dsp_c.c`) | `dsp56k_execute_instruction(core)` | `core->is_idle` (set via peripheral `$FFFFC4`) | Breaks out of `dsp_c_run()` loop when `is_idle == true`. |
| **Rust JIT** (`dsp_jit.c`) | `dsp56300_run(jit, cycles)` | `dsp56300_get_halt(jit)` | Returns execution to host when basic block hits halt/wait. |

#### Unified Subframe Execution Model:
To ensure predictable pacing across both engines:
1. **Slice Budget**: Define an allocation of cycles per $128\,\mu\text{s}$ subframe ($~12,800$ cycles assuming a $100\,\text{MHz}$ DSP clock).
2. **Interrupt Kickoff**: Call `dsp_start_frame(d->ep.dsp)`. In both backends, this asserts `INTERRUPT_START_FRAME` on `$FFFFC5` and clears `halt_requested = false`.
3. **Execution Loop**:
   ```c
   dsp_start_frame(d->ep.dsp);
   dsp_set_halt_requested(d->ep.dsp, false);
   dsp_set_cycle_count(d->ep.dsp, 0);

   int cycle_budget = EP_SLICE_CYCLES;
   while (cycle_budget > 0 && !dsp_get_halt_requested(d->ep.dsp)) {
       int step_chunk = MIN(cycle_budget, 1000);
       dsp_run(d->ep.dsp, step_chunk);
       cycle_budget -= step_chunk;
   }
   g_dbg.ep.cycles = dsp_get_cycle_count(d->ep.dsp);
   ```

### 4.3 Program Counter Export via `DSPOps`

In Phase 62.1, we exported `dsp_get_pc()` to inspect the executing microcode address for real-time disassembler tracing and crash diagnostics.  
Upstream PR #2801 omitted `get_pc` from `DSPOps`.  
**Harmonization Requirement**: Add `uint32_t (*get_pc)(DSPState *dsp);` to `DSPOps`:
* In `dsp_c.c`: Returns `c_core(dsp)->pc`.
* In `dsp_jit.c`: Returns `dsp56300_get_pc(jit_be(dsp)->jit)`.

---

## 5. Phase 66 Integration Roadmap & Action Items

```
[Phase 66 Action Plan]
  │
  ├──► Task 1: Memory Subsystem Refactor
  │     ├── Expand DSP_PRAM_SIZE to 32768 in dsp_cpu_regs.h & dsp.h DspCoreState.
  │     └── Protect EP P-RAM during dsp_c_bootstrap & dsp_jit_bootstrap.
  │
  ├──► Task 2: DSPOps Interface Expansion
  │     ├── Add dsp_get_pc() to DSPOps (dsp.h, dsp_c.c, dsp_jit.c).
  │     └── Wire ESSI0 ($FFFFB3) & Mailbox ($FFFFC5) handshakes into dsp.c read/write_peripheral.
  │
  ├──► Task 3: gp_ep.c Harmonization
  │     ├── Replace all direct pointer dereferences with dsp_read_memory/dsp_write_memory.
  │     ├── Re-introduce float_to_24b() conversion.
  │     ├── Apply 512-word multichannel strides.
  │     └── Enforce early return / stereo suppression when is_5_1_active && ep_enabled.
  │
  ├──► Task 4: Monitor & Audio Engine Convergence
  │     ├── Expand monitor.c to negotiate 6-channel SDL3 streams.
  │     ├── Update throttle() in apu.c to scale watermarks dynamically.
  │     └── Generalize BYOF firmware loader to populate both C and JIT backends.
  │
  └──► Task 5: Compilation & Test Verification
        ├── Clean compilation via ./build.sh (MSYS2 MinGW-w64).
        └── Validation using tools/ep_harness.
```

### Risk Assessment & Mitigation
* **Risk**: Rust JIT backend rejects non-standard instruction extensions (`EXTRACTU`, `DO FOREVER`) implemented in the C interpreter.
  * **Mitigation**: Maintain strict core routing where `d->ep.dsp` defaults to the C interpreter backend (`dsp_set_engine(d->ep.dsp, false)`), while `d->gp.dsp` takes full advantage of JIT acceleration.
* **Risk**: Dynamic audio stream switching causes popping on dashboard boot.
  * **Mitigation**: Audio stream channel count is locked during cold boot based on guest EEPROM surround settings, preventing mid-frame device resets.

---

*Report compiled and certified for Phase 66 execution.*

