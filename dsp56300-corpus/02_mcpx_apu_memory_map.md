**Phase 1 Codebase Reconnaissance Report: Xemu Audio Modernization**

Architecture Note on File Locations

In the Xbox architecture, the Audio Processing Unit (APU) containing the
Voice Processor (VP), Global Processor (GP), and Encode Processor (EP)
resides within the **MCPX Southbridge** (PCI Device 00:05.0), not the
NV2A Northbridge/GPU (00:00.0). As a result, the relevant audio
subsystems are located under hw/xbox/mcpx/apu/ rather
than hw/xbox/nv2a/.

------------------------------------------------------------------------

**Target 1: The AC-3 Encoder Registers & MMIO Mapping**

Current MMIO Layout

The MCPX APU BAR 0 (d-\>mmio, size 0x80000 / 512 KB) is initialized
in hw/xbox/mcpx/apu/apu.c:

- **0x00000 - 0x1FFFF**: Base APU registers and FIFO configuration
  (handled by mcpx_apu_mmio_ops in apu.c).

- **0x20000 - 0x2FFFF**: Voice Processor (VP) MMIO
  (vp_ops in hw/xbox/mcpx/apu/vp/vp.c).

- **0x30000 - 0x3FFFF**: Global Processor (GP) DSP MMIO
  (gp_ops in hw/xbox/mcpx/apu/dsp/gp_ep.c).

- **0x50000 - 0x5FFFF**: **Encode Processor (EP)
  MMIO** (ep_ops in hw/xbox/mcpx/apu/dsp/gp_ep.c).

- *Note*: 0x40000 - 0x4FFFF is unmapped (falls through to default where
  writes are ignored).

The AC-3 / EP MMIO Registers

The Xbox hardware AC-3 encoder is the **Encode Processor (EP)**. Its
MMIO registers and memories are mapped as follows:

- **EP Reset / Execution Register**:

  - File: hw/xbox/mcpx/apu/dsp/gp_ep.c

  - Constant: NV_PAPU_EPRST (0x0000FFFC, relative
    to d-\>ep.mmio at 0x5FFFC) defined in apu_regs.h.

  - Behavior: When the guest activates the AC-3 encoder, it
    writes NV_PAPU_GPRST_GPRST \|
    NV_PAPU_GPRST_GPDSPRST to NV_PAPU_EPRST.

- **EP Internal Memory Ranges**:

  - NV_PAPU_EPXMEM (0x50000 - 0x52FFF, size 0xC00 \* 4): EP X Data
    Memory (gp_ep.c:L368-L373 / L407-L412).

  - NV_PAPU_EPYMEM (0x56000 - 0x563FF, size 0x100 \* 4): EP Y Data
    Memory (gp_ep.c:L374-L379 / L413-L418).

  - NV_PAPU_EPPMEM (0x5A000 - 0x5DFFF, size 0x1000 \* 4): EP Program RAM
    where guest microcode is loaded (gp_ep.c:L380-L385 / L419-L424).

- **EP DMA & FIFO Registers in Base
  MMIO** (hw/xbox/mcpx/apu/apu_regs.h):

  - NV_PAPU_EPSADDR (0x2048) & NV_PAPU_EPSMAXSGE (0x20DC): EP Scratch
    Address & Max SGE.

  - NV_PAPU_EPFADDR (0x204C) & NV_PAPU_EPFMAXSGE (0x20E0): EP FIFO SGE
    Address & Max SGE.

  - NV_PAPU_EPOFBASE0..3 (0x4024, 0x4034, 0x4044, 0x4054): EP Output
    FIFOs (AC-3 bitstream out to S/PDIF).

  - NV_PAPU_EPIFBASE0..1 (0x4064, 0x4074): EP Input FIFOs.

- **Watchdog Hook Target**:

  - Any unhandled register in ep_write (gp_ep.c:L430-L432) falls through
    to default: d-\>ep.regs\[addr\] = val;.

  - The natural trigger for the AC-3 watchdog is
    hooking ep_write in gp_ep.c:L425-L433 on NV_PAPU_EPRST / NV_PAPU_EPPMEM,
    and base MMIO in apu.c:L93-L97 when the guest
    configures NV_PAPU_EPFADDR (0x204C) or NV_PAPU_EPOFBASE0 (0x4024).

------------------------------------------------------------------------

**Target 2: The DSP State Tracking**

Xemu tracks the \"Experimental DSP\" (Real-time DSP) toggle at the
following locations:

1.  **Configuration Schema**:

    - File: config_spec.yml

    - Key: audio.use_dsp: bool (and audio.use_dsp_jit: bool).

2.  **Global Settings Structure**:

    - Declared in: ui/xemu-settings.h (extern struct config g_config;).

    - Defined in: ui/xemu-settings.cc (struct config g_config;).

    - Field access: g_config.audio.use_dsp.

3.  **UI Setting Binding**:

    - File: ui/xui/main-menu.cc

    - Toggle(\"Real-time DSP processing\", &g_config.audio.use_dsp,
      \"Enable improved audio accuracy (experimental)\");

4.  **Runtime APU State Synchronization**:

    - File: hw/xbox/mcpx/apu/dsp/gp_ep.c,
      in mcpx_apu_update_dsp_preference(MCPXAPUState \*d):

c

if (last_known_dsp_pref != (int)g_config.audio.use_dsp) {

if (g_config.audio.use_dsp) {

d-\>monitor.point = MCPX_APU_DEBUG_MON_GP_OR_EP;

d-\>gp.realtime = true;

d-\>ep.realtime = true;

} else {

d-\>monitor.point = MCPX_APU_DEBUG_MON_VP;

d-\>gp.realtime = false;

d-\>ep.realtime = false;

}

last_known_dsp_pref = g_config.audio.use_dsp;

}

- APU State Flags in hw/xbox/mcpx/apu/apu_int.h:

  - d-\>gp.realtime

  - d-\>ep.realtime

  - d-\>monitor.point (MCPX_APU_DEBUG_MON_GP_OR_EP vs MCPX_APU_DEBUG_MON_VP).

- Called per frame inside se_frame(): hw/xbox/mcpx/apu/apu.c:L221.

------------------------------------------------------------------------

**Target 3: The Audio PRD Parsers & Downmix Loops**

Mode A: Historic Preservation (LLE Pipeline)

- **Host Downmix Extraction Loop**:

  - File: hw/xbox/mcpx/apu/dsp/gp_ep.c

  - Loop:

c

if ((d-\>monitor.point == MCPX_APU_DEBUG_MON_GP) \|\|

(d-\>monitor.point == MCPX_APU_DEBUG_MON_GP_OR_EP && !ep_enabled)) {

int off = (d-\>ep_frame_div % 8) \* NUM_SAMPLES_PER_FRAME;

for (int i = 0; i \< NUM_SAMPLES_PER_FRAME; i++) {

uint32_t l = dsp_read_memory(d-\>gp.dsp, \'X\', 0x1400 + i);

d-\>monitor.frame_buf\[off + i\]\[0\] = l \>\> 8;

uint32_t r =

dsp_read_memory(d-\>gp.dsp, \'X\', 0x1400 + 1 \* 0x20 + i);

d-\>monitor.frame_buf\[off + i\]\[1\] = r \>\> 8;

}

}

*(Note: Output buffer is populated and pushed to SDL
in hw/xbox/mcpx/apu/monitor.c:L70-L84 via SDL_PutAudioStreamData).*

- **DSP DMA Multi-Channel Interleave & PRD Scattering**:

  - **Interleaved multi-channel packing loop**:

    - File: hw/xbox/mcpx/apu/dsp/dsp_dma.c

    - Loops through channel_count (up to 16 channels) and block_count to
      pack interleaved PCM samples into scratch_buf before
      calling s-\>fifo_rw.

  - **PRD Descriptor Parsing & Guest UMA Access**:

    - File: hw/xbox/mcpx/apu/dsp/gp_ep.c in scatter_gather_rw():

      - **Lines 62-67**: Parses Physical Region Descriptors (PRDs / SGE
        entries) from physical memory:

c

uint32_t prd_address = ldl_le_phys(&address_space_memory,

sge_base + page_entry \* 8 + 0);

hwaddr paddr = prd_address + offset_in_page;

- **Lines 75-80**: Performs direct memcpy to/from guest UMA memory
  (d-\>ram_ptr\[paddr\]).

<!-- -->

- Circular ring buffer
  wrapper: hw/xbox/mcpx/apu/dsp/gp_ep.c:L110-L141 in circular_scatter_gather_rw().

Mode B: Modern Performance (HLE Pipeline)

- **Voice PRD / SGE / SSL Parsing**:

  - File: hw/xbox/mcpx/apu/vp/vp.c in voice_get_samples():

    - Streaming Voices (SSL segment table): vp.c:L957-L960 reads
      physical segment descriptors from d-\>regs\[NV_PAPU_VPSSLADDR\] +
      page \* 8.

    - Buffer Voices (SGE PRD
      table): vp.c:L1029-L1034 and L1054-L1057 resolve physical memory
      via get_data_ptr(d-\>regs\[NV_PAPU_VPSGEADDR\], 0xFFFFFFFF,
      linear_addr).

- **Voice 3D Spatialization & Coordinates Parsing**:

  - File: hw/xbox/mcpx/apu/vp/vp.c in voice_process():

    - **3D Voice Gating**: vp.c:L1380-L1385 (v \< MCPX_HW_MAX_3D_VOICES)
      routes 3D voice mixbins through d-\>vp.hrtf_submix\[0..3\].

    - **HRTF Filter Processing**: vp.c:L1458-L1465:

c

if (v \< MCPX_HW_MAX_3D_VOICES && g_config.audio.hrtf) {

uint16_t hrtf_handle = voice_get_mask(d, v,
NV_PAVS_VOICE_CFG_HRTF_TARGET,

NV_PAVS_VOICE_CFG_HRTF_TARGET_HANDLE);

if (hrtf_handle != HRTF_NULL_HANDLE) {

hrtf_filter_process(&d-\>vp.filters\[v\].hrtf, samples, samples);

}

}

- **HRTF Front-End Methods (DirectSound3D Transfer)**:

  - Head-Related Impulse Response (HRIR) / ITD parameter
    loading: vp.c:L414-L438 (NV1BA0_PIO_SET_HRIR, NV1BA0_PIO_SET_HRIR_X).

  - HRTF Target Handle
    Binding: vp.c:L352-L367 (NV1BA0_PIO_SET_VOICE_TAR_HRTF).

<!-- -->

- **HLE Stereo Downmix Accumulation**:

  - File: hw/xbox/mcpx/apu/vp/vp.c in voice_process() accumulates
    samples into sample_buf\[i\]\[0..1\].

  - File: hw/xbox/mcpx/apu/vp/vp.c in mcpx_apu_vp_frame() converts d-\>vp.sample_buf to
    16-bit integer and writes it into d-\>monitor.frame_buf for SDL
    playback.

------------------------------------------------------------------------

Phase 52 Reconnaissance Report: C-Interpreter Interface Audit

------------------------------------------------------------------------

Target 1: Inspect interp/ Directory

Directory: hw/xbox/mcpx/apu/dsp/interp/

text

-rw-r\--r\-- 1 runner runner 10169 debug.c

-rw-r\--r\-- 1 runner runner 50993 dsp_cpu.c

-rw-r\--r\-- 1 runner runner 4434 dsp_cpu.h

-rw-r\--r\-- 1 runner runner 3338 dsp_cpu_regs.h

-rw-r\--r\-- 1 runner runner 63062 dsp_dis.c.inc

-rw-r\--r\-- 1 runner runner 238793 dsp_emu.c.inc

-rw-r\--r\-- 1 runner runner 150 meson.build

**Key Structural Observations:**

- dsp_cpu.c includes dsp_emu.c.inc and dsp_dis.c.inc directly
  via #include.

- dsp_cpu.h defines the primary C-interpreter execution core
  struct dsp_core_t (struct dsp_core_s).

- dsp_cpu_regs.h defines memory sizes, space identifiers, register
  indices, and status register bitmasks
  (DSP_SPACE_X, DSP_SPACE_Y, DSP_SPACE_P, DSP_XRAM_SIZE, DSP_YRAM_SIZE, DSP_PRAM_SIZE,
  etc.).

------------------------------------------------------------------------

Target 2: Audit Core Headers

1. hw/xbox/mcpx/apu/dsp/dsp.h

c

#ifndef DSP_H

#define DSP_H

#include \<stdint.h\>

#include \<stdbool.h\>

#include \"interp/dsp_cpu_regs.h\"

#include \"dsp_dma.h\"

typedef struct DSPState DSPState;

typedef struct DSPOps {

void (\*finalize)(DSPState \*dsp);

void (\*reset)(DSPState \*dsp);

void (\*step)(DSPState \*dsp);

void (\*run)(DSPState \*dsp, int cycles);

void (\*bootstrap)(DSPState \*dsp);

void (\*start_frame)(DSPState \*dsp);

uint32_t (\*read_memory)(DSPState \*dsp, char space, uint32_t addr);

void (\*write_memory)(DSPState \*dsp, char space, uint32_t addr,

uint32_t value);

bool (\*get_halt_requested)(DSPState \*dsp);

void (\*set_halt_requested)(DSPState \*dsp, bool idle);

uint32_t (\*get_cycle_count)(DSPState \*dsp);

void (\*set_cycle_count)(DSPState \*dsp, uint32_t count);

void (\*invalidate_opcache)(DSPState \*dsp);

void (\*sync_to_vm)(DSPState \*dsp);

void (\*sync_from_vm)(DSPState \*dsp);

} DSPOps;

typedef struct DspCoreState {

uint32_t pc;

uint32_t registers\[DSP_REG_MAX\];

uint32_t stack\[2\]\[16\];

uint32_t xram\[DSP_XRAM_SIZE\];

uint32_t yram\[DSP_YRAM_SIZE\];

uint32_t pram\[DSP_PRAM_SIZE\];

uint32_t mixbuffer\[DSP_MIXBUFFER_SIZE\];

uint32_t periph\[DSP_PERIPH_SIZE\];

uint32_t cycle_count;

uint16_t instr_cycle;

bool halt_requested;

bool is_gp;

uint32_t loop_rep;

uint32_t pc_on_rep;

uint32_t cur_inst;

uint32_t cur_inst_len;

uint16_t interrupt_state;

uint16_t interrupt_instr_fetch;

uint16_t interrupt_save_pc;

uint16_t interrupt_counter;

uint16_t interrupt_ipl_to_raise;

uint16_t interrupt_pipeline_count;

int16_t interrupt_ipl\[12\]; /\* only \[0..3\] used; size 12 for
snapshot compat \*/

uint16_t interrupt_is_pending\[12\]; /\* only \[0..3\] used; size 12 for
snapshot compat \*/

} DspCoreState;

struct DSPState {

const DSPOps \*ops;

DspCoreState core;

DSPDMAState dma;

int save_cycles;

uint32_t interrupts;

bool is_gp;

void \*backend;

};

DSPState \*dsp_init(void \*rw_opaque, dsp_scratch_rw_func scratch_rw,

dsp_fifo_rw_func fifo_rw, bool is_gp);

void dsp_destroy(DSPState \*dsp);

void dsp_reset(DSPState \*dsp);

void dsp_step(DSPState \*dsp);

void dsp_run(DSPState \*dsp, int cycles);

void dsp_bootstrap(DSPState \*dsp);

void dsp_start_frame(DSPState \*dsp);

uint32_t dsp_read_memory(DSPState \*dsp, char space, uint32_t addr);

void dsp_write_memory(DSPState \*dsp, char space, uint32_t address,

uint32_t value);

/\* Accessor functions for backend-independent state access \*/

bool dsp_get_halt_requested(DSPState \*dsp);

void dsp_set_halt_requested(DSPState \*dsp, bool idle);

uint32_t dsp_get_cycle_count(DSPState \*dsp);

void dsp_set_cycle_count(DSPState \*dsp, uint32_t count);

void dsp_invalidate_opcache(DSPState \*dsp);

/\* Backend synchronization - sync backend state to/from DspCoreState
\*/

void dsp_sync_to_vm(DSPState \*dsp);

void dsp_sync_from_vm(DSPState \*dsp);

/\* Engine switching \*/

void dsp_set_engine(DSPState \*dsp, bool use_jit);

#endif /\* DSP_H \*/

2. hw/xbox/mcpx/apu/dsp/dsp_internal.h

c

#ifndef DSP_INTERNAL_H

#define DSP_INTERNAL_H

#include \"dsp.h\"

uint32_t read_peripheral(DSPState \*dsp, uint32_t address);

void write_peripheral(DSPState \*dsp, uint32_t address, uint32_t value);

void dsp_start_frame_impl(DSPState \*dsp);

extern const DSPOps c_dsp_ops;

void dsp_c_init(DSPState \*dsp);

extern const DSPOps jit_dsp_ops;

void dsp_jit_init(DSPState \*dsp);

#endif /\* DSP_INTERNAL_H \*/

3\. Interpreter Underlying Declarations (interp/dsp_cpu.h)

c

void dsp56k_reset_cpu(dsp_core_t\* dsp);

void dsp56k_execute_instruction(dsp_core_t\* dsp);

uint32_t dsp56k_read_memory(dsp_core_t\* dsp, int space, uint32_t
address);

void dsp56k_write_memory(dsp_core_t\* dsp, int space, uint32_t address,
uint32_t value);

void dsp56k_add_interrupt(dsp_core_t\* dsp, uint16_t inter);

------------------------------------------------------------------------

Target 3: Audit C-Backend Initialization & Step Prototypes

Lines
in hw/xbox/mcpx/apu/dsp/dsp_c.c matching (init\|reset\|run\|step\|read\|write\|exec):

text

36:static uint32_t c_dma_mem_read(void \*opaque, int space, uint32_t
addr)

38: return dsp56k_read_memory((dsp_core_t \*)opaque, space, addr);

41:static void c_dma_mem_write(void \*opaque, int space, uint32_t addr,

44: dsp56k_write_memory((dsp_core_t \*)opaque, space, addr, value);

47:static uint32_t c_read_peripheral(dsp_core_t \*core, uint32_t
address)

49: return read_peripheral((DSPState \*)core-\>opaque, address);

52:static void c_write_peripheral(dsp_core_t \*core, uint32_t address,

55: write_peripheral((DSPState \*)core-\>opaque, address, value);

58:static void dsp_c_reset(DSPState \*dsp)

60: dsp56k_reset_cpu(c_core(dsp));

64:static void dsp_c_step(DSPState \*dsp)

66: dsp56k_execute_instruction(c_core(dsp));

69:static void dsp_c_run(DSPState \*dsp, int cycles)

79: dsp56k_execute_instruction(core);

105:static uint32_t dsp_c_read_memory(DSPState \*dsp, char space,
uint32_t address)

120: assert(!\"Invalid dsp space when reading from memory\");

124: return dsp56k_read_memory(c_core(dsp), space_id, address);

127:static void dsp_c_write_memory(DSPState \*dsp, char space, uint32_t
address,

143: assert(!\"Invalid dsp space when writing to memory\");

147: dsp56k_write_memory(c_core(dsp), space_id, address, value);

251:void dsp_c_init(DSPState \*dsp)

255: core-\>read_peripheral = c_read_peripheral;

256: core-\>write_peripheral = c_write_peripheral;

262: dsp-\>dma.mem_read = c_dma_mem_read;

263: dsp-\>dma.mem_write = c_dma_mem_write;

265: /\* Ensure the interpreter\'s opcode decoder tables are
initialized.

269: dsp56k_reset_cpu(core);

289: .read_memory = dsp_c_read_memory,

290: .reset = dsp_c_reset,

291: .run = dsp_c_run,

295: .step = dsp_c_step,

298: .write_memory = dsp_c_write_memory,

------------------------------------------------------------------------

Architectural Takeaways for tools/bench_ep.c

1.  **Direct Interpreter Instantiation:**

    - Rather than dragging in QEMU\'s hypervisor headers (qemu/osdep.h,
      glib dependencies, VMState), a standalone harness can
      instantiate dsp_core_t directly:

c

#include \"hw/xbox/mcpx/apu/dsp/interp/dsp_cpu.h\"

#include \"hw/xbox/mcpx/apu/dsp/interp/dsp_cpu_regs.h\"

- Initialize the core via:

c

dsp_core_t core = { 0 };

core.is_gp = false; /\* EP mode \*/

dsp56k_reset_cpu(&core);

2.  **Memory Access & Execution APIs:**

    - Program Memory injection: dsp56k_write_memory(&core, DSP_SPACE_P,
      addr, opcode)

    - Execution stepping: dsp56k_execute_instruction(&core)

    - State
      Inspection: core.pc, core.registers\[DSP_REG\_\*\], core.is_idle, core.cycle_count

3.  **Peripheral Callbacks:**

    - core.read_peripheral and core.write_peripheral are standard
      function pointers:

c

uint32_t (\*read_peripheral)(dsp_core_t\* core, uint32_t address);

void (\*write_peripheral)(dsp_core_t\* core, uint32_t address, uint32_t
value);

- This allows mock HI08 peripheral hooking directly
  within bench_ep.c without needing hw/xbox/mcpx/apu/apu_int.h.
