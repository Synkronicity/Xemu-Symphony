/*
 * Xemu Symphony - Deterministic EP ROM Disassembler & Subframe Execution Tracer
 *
 * Clean-room diagnostic utility for Phase 117A forensic analysis of
 * tools/dolby_ep.bin and MCPX APU Encode Processor (DSP56362) behavior.
 */

#define TRACE_EP_ROM_MAIN 1

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "dsp_cpu.h"
#include "dsp_cpu_regs.h"
#include "dsp_dma.h"
#include "dsp_dma_regs.h"
#include "debug.h"
#include "trace.h"

#define EP_SUBFRAME_CYCLES    12800
#define SCRATCH_RAM_WORDS     0x100000 /* 4 MB Scratch RAM mirror */
#define INTERRUPT_ABORT_FRAME (1 << 0)
#define INTERRUPT_START_FRAME (1 << 1)
#define INTERRUPT_DMA_EOL     (1 << 7)

typedef struct {
    uint32_t next_block;
    uint32_t control;
    uint32_t count;
    uint32_t dsp_offset;
    uint32_t scratch_offset;
    uint32_t scratch_base;
    uint32_t scratch_size;
} DMADescriptorLog;

typedef struct {
    uint32_t subframe;
    uint32_t block_idx;
    uint32_t cmd_id;
    uint32_t sub_op;
    uint32_t trigger_pc;
    uint32_t trigger_reg;
    uint32_t trigger_val;
    uint32_t raw_node_ptr;
    uint32_t node_addr;
    int node_space;
    DMADescriptorLog desc;
} DMATransferEvent;

#define MAX_DMA_EVENTS    1024
#define MAX_PERIPH_EVENTS 8192
#define MAX_CALL_EVENTS   8192

typedef struct {
    uint32_t subframe;
    uint32_t pc;
    uint32_t addr;
    uint32_t val;
    bool is_write;
} PeriphEvent;

typedef struct {
    uint32_t subframe;
    uint32_t from_pc;
    uint32_t to_pc;
    uint32_t sp_depth;
    uint64_t cycle;
    const char *kind;
} CallEvent;

typedef struct {
    uint32_t entry_pc;
    uint64_t self_cycles;
    uint64_t inclusive_cycles;
    uint32_t self_insts;
    uint32_t call_count;
    uint32_t subframes_seen_mask;
} SubProfile;

typedef struct {
    dsp_core_t core;
    DSPDMAState dma;
    bool is_gp;
    uint32_t hcr;
    uint32_t hsr;
    uint32_t hpcr;
    uint32_t hbar;
    uint32_t horx;
    uint32_t hotx;
    uint32_t interrupts;
    int save_cycles;
    uint32_t current_subframe;
    uint32_t last_p0180_cmd;
    uint32_t last_p0180_subop;
    bool emulate_hbar_ep_irq;
    bool full_pipeline_mode;

    uint32_t *scratch_ram;
    uint32_t epif_base[4];
    uint32_t epif_cur[4];
    uint32_t epif_end[4];
    uint32_t epof_base[4];
    uint32_t epof_cur[4];
    uint32_t epof_end[4];

    DMATransferEvent dma_events[MAX_DMA_EVENTS];
    uint32_t num_dma_events;

    PeriphEvent periph_events[MAX_PERIPH_EVENTS];
    uint32_t num_periph_events;

    CallEvent call_events[MAX_CALL_EVENTS];
    uint32_t num_call_events;

    SubProfile prof[DSP_PRAM_SIZE];
    uint32_t call_stack_sub[64];
    uint64_t call_stack_enter_cyc[64];
    int call_stack_depth;
    bool halt_requested;
} EPTraceState;

static EPTraceState g_state;

void dsp_set_halt_requested(void *opaque, bool val);
void dsp_set_halt_requested(void *opaque, bool val)
{
    if (opaque) {
        EPTraceState *st = (EPTraceState *)opaque;
        st->halt_requested = val;
    }
}

static const char *space_name(int space)
{
    switch (space) {
    case DSP_SPACE_X: return "X";
    case DSP_SPACE_Y: return "Y";
    case DSP_SPACE_P: return "P";
    default: return "?";
    }
}

static const char *buf_id_name(uint32_t buf_id)
{
    switch (buf_id) {
    case 0x0: return "FIFO0";
    case 0x1: return "FIFO1";
    case 0x2: return "FIFO2";
    case 0x3: return "FIFO3";
    case 0xE: return "SCRATCH_CIRC(0xE)";
    case 0xF: return "SCRATCH_LIN(0xF)";
    default: return "UNKNOWN";
    }
}

static const char *format_name(uint32_t fmt)
{
    switch (fmt) {
    case 0: return "8-bit";
    case 1: return "16-bit";
    case 2: return "24-bit MSB";
    case 3: return "32-bit";
    case 6: return "24-bit LSB";
    default: return "invalid";
    }
}

static void decode_dma_addr(uint32_t raw_addr, int *out_space, uint32_t *out_addr)
{
    if (raw_addr < 0x1800) {
        *out_space = DSP_SPACE_X;
        *out_addr = raw_addr;
    } else if (raw_addr >= 0x1800 && raw_addr < 0x2000) {
        *out_space = DSP_SPACE_Y;
        *out_addr = raw_addr - 0x1800;
    } else if (raw_addr >= 0x2800 && raw_addr < 0x3800) {
        *out_space = DSP_SPACE_P;
        *out_addr = raw_addr - 0x2800;
    } else if (raw_addr >= 0x4000) {
        *out_space = DSP_SPACE_X;
        *out_addr = raw_addr; /* Bit-14 DMA Hole (0x4000) */
    } else {
        *out_space = DSP_SPACE_X;
        *out_addr = raw_addr & 0x0FFF;
    }
}

static uint32_t c_dma_mem_read(void *opaque, int space, uint32_t addr)
{
    return dsp56k_read_memory((dsp_core_t *)opaque, space, addr);
}

static void c_dma_mem_write(void *opaque, int space, uint32_t addr,
                            uint32_t value)
{
    dsp_core_t *core = (dsp_core_t *)opaque;
    dsp56k_write_memory(core, space, addr, value);
    if (space == DSP_SPACE_P && addr < DSP_PRAM_SIZE) {
        core->predecode_table[addr].flags = 0;
    }
}

static void record_dma_chain(EPTraceState *st, uint32_t trigger_reg,
                             uint32_t trigger_val, uint32_t head_ptr)
{
    uint32_t cur_ptr = head_ptr;
    int safety = 0;

    while (!(cur_ptr & NODE_POINTER_EOL) && safety < 32) {
        safety++;
        uint32_t raw_addr = cur_ptr & NODE_POINTER_VAL;
        int block_space = DSP_SPACE_X;
        uint32_t block_addr = 0;
        decode_dma_addr(raw_addr, &block_space, &block_addr);

        if (st->num_dma_events < MAX_DMA_EVENTS) {
            DMATransferEvent *ev = &st->dma_events[st->num_dma_events++];
            ev->subframe = st->current_subframe;
            ev->block_idx = st->core.pram[0x0001] & 0x00FFFFFF;
            ev->cmd_id = st->last_p0180_cmd;
            ev->sub_op = st->last_p0180_subop;
            ev->trigger_pc = st->core.pc;
            ev->trigger_reg = trigger_reg;
            ev->trigger_val = trigger_val;
            ev->raw_node_ptr = cur_ptr;
            ev->node_addr = block_addr;
            ev->node_space = block_space;
            ev->desc.next_block = dsp56k_read_memory(&st->core, block_space, block_addr + 0);
            ev->desc.control = dsp56k_read_memory(&st->core, block_space, block_addr + 1);
            ev->desc.count = dsp56k_read_memory(&st->core, block_space, block_addr + 2);
            ev->desc.dsp_offset = dsp56k_read_memory(&st->core, block_space, block_addr + 3);
            ev->desc.scratch_offset = dsp56k_read_memory(&st->core, block_space, block_addr + 4);
            ev->desc.scratch_base = dsp56k_read_memory(&st->core, block_space, block_addr + 5);
            ev->desc.scratch_size = dsp56k_read_memory(&st->core, block_space, block_addr + 6);
            cur_ptr = ev->desc.next_block;
        } else {
            break;
        }
    }
}

static void trace_scratch_rw(void *opaque, uint8_t *ptr, uint32_t addr,
                             size_t len, bool dir)
{
    EPTraceState *st = (EPTraceState *)opaque;
    uint32_t word_idx = (addr >> 2) & (SCRATCH_RAM_WORDS - 1);
    size_t words = len / 4;
    if (dir) {
        for (size_t i = 0; i < words; i++) {
            uint32_t val = ldl_le_p(ptr + i * 4) & 0x00FFFFFF;
            st->scratch_ram[(word_idx + i) & (SCRATCH_RAM_WORDS - 1)] = val;
        }
    } else {
        for (size_t i = 0; i < words; i++) {
            uint32_t val = st->scratch_ram[(word_idx + i) & (SCRATCH_RAM_WORDS - 1)];
            stl_le_p(ptr + i * 4, val);
        }
    }
}

static void trace_fifo_rw(void *opaque, uint8_t *ptr, unsigned int index,
                          size_t len, bool dir)
{
    EPTraceState *st = (EPTraceState *)opaque;
    assert(index < 4);
    if (dir) {
        st->epof_cur[index] += (uint32_t)len;
        trace_scratch_rw(opaque, ptr, st->epof_cur[index], len, true);
    } else {
        trace_scratch_rw(opaque, ptr, st->epif_cur[index], len, false);
        st->epif_cur[index] += (uint32_t)len;
    }
}

static uint32_t trace_read_peripheral(dsp_core_t *core, uint32_t address)
{
    EPTraceState *st = (EPTraceState *)core->opaque;
    uint32_t v = 0xababa;

    switch (address) {
    case 0xFFFFB3:
        v = 0;
        break;
    case DSP_REG_PERIPH_HCR: /* 0xFFFFC2 */
        v = st->hcr;
        break;
    case DSP_REG_PERIPH_HSR: /* 0xFFFFC3 */
        v = st->hsr;
        break;
    case DSP_REG_PERIPH_HPCR: /* 0xFFFFC4 */
        v = st->hpcr;
        break;
    case DSP_REG_PERIPH_HBAR: /* 0xFFFFC5 */
        v = st->hbar;
        if (st->emulate_hbar_ep_irq) {
            v |= st->interrupts;
            if (st->dma.eol) {
                v |= INTERRUPT_DMA_EOL;
            }
        }
        break;
    case DSP_REG_PERIPH_HORX: /* 0xFFFFC6 */
        v = st->horx;
        st->hsr &= ~DSP_HSR_HRDF;
        break;
    case DSP_REG_PERIPH_HOTX: /* 0xFFFFC7 */
        v = st->hotx;
        break;
    case DSP_REG_PERIPH_HDDR: /* 0xFFFFC8 */
    case DSP_REG_PERIPH_HDR:  /* 0xFFFFC9 */
        v = 0;
        break;
    case 0xFFFFD4:
        v = dsp_dma_read(&st->dma, DMA_NEXT_BLOCK);
        break;
    case 0xFFFFD5:
        v = dsp_dma_read(&st->dma, DMA_START_BLOCK);
        break;
    case 0xFFFFD6:
        v = dsp_dma_read(&st->dma, DMA_CONTROL);
        break;
    case 0xFFFFD7:
        v = dsp_dma_read(&st->dma, DMA_CONFIGURATION);
        break;
    case 0xFFFFE3:
    case 0xFFFFE7:
    case 0xFFFFEB:
        v = (uint32_t)(core->cycle_count & 0x00FFFFFF);
        break;
    case 0xFFFFE0 ... 0xFFFFE2:
    case 0xFFFFE4 ... 0xFFFFE6:
    case 0xFFFFE8 ... 0xFFFFEA:
    case 0xFFFFEC ... 0xFFFFEF:
        v = 0;
        break;
    }

    if (st->num_periph_events < MAX_PERIPH_EVENTS) {
        PeriphEvent *pe = &st->periph_events[st->num_periph_events++];
        pe->subframe = st->current_subframe;
        pe->pc = core->pc;
        pe->addr = address;
        pe->val = v;
        pe->is_write = false;
    }

    return v;
}

static void trace_write_peripheral(dsp_core_t *core, uint32_t address,
                                   uint32_t value)
{
    EPTraceState *st = (EPTraceState *)core->opaque;

    if (st->num_periph_events < MAX_PERIPH_EVENTS) {
        PeriphEvent *pe = &st->periph_events[st->num_periph_events++];
        pe->subframe = st->current_subframe;
        pe->pc = core->pc;
        pe->addr = address;
        pe->val = value;
        pe->is_write = true;
    }

    uint32_t saved_dsp_off = 0xFFFFFFFF;
    int saved_space = DSP_SPACE_P;
    uint32_t saved_addr = 0;

    if (address == 0xFFFFD6) {
        if ((value & DMA_CONTROL_ACTION) == DMA_CONTROL_ACTION_START &&
            !(st->dma.control & DMA_CONTROL_FROZEN)) {
            record_dma_chain(st, address, value, st->dma.next_block);
            if (!(st->dma.next_block & NODE_POINTER_EOL)) {
                decode_dma_addr(st->dma.next_block & NODE_POINTER_VAL,
                                &saved_space, &saved_addr);
                uint32_t doff = dsp56k_read_memory(&st->core, saved_space, saved_addr + 3);
                if (doff >= 0x4000) {
                    /* Bit-14 DMA zero-generator hole (0x4000): redirect to zeroed X:0x0FC0 */
                    saved_dsp_off = doff;
                    memset(&st->core.xram[0x0FC0], 0, 64 * sizeof(uint32_t));
                    dsp56k_write_memory(&st->core, saved_space, saved_addr + 3, 0x0FC0);
                }
            }
        }
    }

    switch (address) {
    case DSP_REG_PERIPH_HCR: /* 0xFFFFC2 */
        st->hcr = value & 0x00FFFFFF;
        break;
    case DSP_REG_PERIPH_HPCR: /* 0xFFFFC4 */
        st->hpcr = value;
        break;
    case DSP_REG_PERIPH_HBAR: /* 0xFFFFC5 */
        st->hbar = value;
        if (st->emulate_hbar_ep_irq) {
            st->interrupts &= ~value;
            if (value & INTERRUPT_DMA_EOL) {
                st->dma.eol = false;
            }
        }
        break;
    case DSP_REG_PERIPH_HOTX: /* 0xFFFFC7 */
        st->hotx = value & 0x00FFFFFF;
        st->hsr &= ~DSP_HSR_HTDE;
        st->hsr |= DSP_HSR_HTDE;
        break;
    case DSP_REG_PERIPH_HDDR:
    case DSP_REG_PERIPH_HDR:
        break;
    case 0xFFFFD4:
        dsp_dma_write(&st->dma, DMA_NEXT_BLOCK, value);
        break;
    case 0xFFFFD5:
        dsp_dma_write(&st->dma, DMA_START_BLOCK, value);
        break;
    case 0xFFFFD6:
        dsp_dma_write(&st->dma, DMA_CONTROL, value);
        break;
    case 0xFFFFD7:
        dsp_dma_write(&st->dma, DMA_CONFIGURATION, value);
        break;
    case 0xFFFFE0 ... 0xFFFFEF:
        break;
    }

    if (saved_dsp_off != 0xFFFFFFFF) {
        dsp56k_write_memory(&st->core, saved_space, saved_addr + 3, saved_dsp_off);
    }
}

static void dump_disassembly_range(dsp_core_t *core, FILE *out,
                                   const char *title,
                                   uint32_t start_pc, uint32_t end_pc)
{
    fprintf(out, "\n========================================================================\n");
    fprintf(out, "  %s (P:0x%04X .. P:0x%04X)\n", title, start_pc, end_pc);
    fprintf(out, "========================================================================\n");

    uint32_t save_pc = core->pc;
    core->pc = start_pc;

    while (core->pc <= end_pc) {
        uint32_t cur_pc = core->pc;
        uint32_t w0 = core->pram[cur_pc] & 0x00FFFFFF;

        if (w0 == 0x000000) {
            uint32_t z_start = cur_pc;
            while (core->pc <= end_pc && (core->pram[core->pc] & 0x00FFFFFF) == 0x000000) {
                core->pc++;
            }
            uint32_t z_end = core->pc - 1;
            if (z_end > z_start + 1) {
                fprintf(out, "p:%04x..p:%04x  000000         (%u words of 0x000000 / nop)\n",
                        z_start, z_end, z_end - z_start + 1);
                continue;
            }
            core->pc = z_start;
        }

        core->disasm_prev_inst_pc = 0xFFFFFFFF;
        core->instr_cycle = 2;
        uint16_t len = ep_trace_disasm_instruction(core, DSP_DISASM_MODE);
        if (len == 0) {
            len = 1;
        }
        const char *text = ep_trace_disasm_get_text(core);
        fprintf(out, "%s\n", text);
        core->pc = cur_pc + len;
    }

    core->pc = save_pc;
}

typedef struct {
    uint32_t ovl_num;
    uint32_t file_word_offset;
    uint32_t word_count;
    uint32_t version_id;
    uint32_t checksum;
    const char *desc;
} OverlayInfo;

static const OverlayInfo k_overlays[6] = {
    { 1,  584,  754, 0x010172, 0xF7F511, "OVERLAY #1: Dolby Surround (Lt/Rt) 5-to-2 Matrix Downmixer & Biquad Filter" },
    { 2, 1338, 2146, 0x020172, 0x686B56, "OVERLAY #2: AC-3 512-Point MDCT Filterbank & Exponent Extractor" },
    { 3, 3484, 1618, 0x030172, 0x7AD5E5, "OVERLAY #3: AC-3 Psychoacoustic Masking & Bit Allocation (BAP)" },
    { 4, 5102, 1747, 0x040172, 0x0FC6C2, "OVERLAY #4: AC-3 Mantissa Quantizer, IEC 61937 / BSI Bitstream Packer & CRC-16" },
    { 5, 6850,  188, 0x050172, 0x7D5E26, "OVERLAY #5: Mono (Lo=Ro) 5-to-2 Downmixer & Peak/RMS Meter" },
    { 6, 7038,  201, 0x060172, 0x87C4B0, "OVERLAY #6: Stereo (Lo/Ro) 5-to-2 Downmixer & Peak/RMS Meter" },
};

static void setup_guest_descriptor_table(EPTraceState *st, const uint32_t *raw_words,
                                         size_t total_words, uint32_t cfg1_prologic,
                                         uint32_t cfg2_ac3, uint32_t cfg3_downmix)
{
    /* Copy entire dolby_ep.bin into Scratch RAM at word offset 0..7263 */
    for (size_t i = 0; i < total_words; i++) {
        st->scratch_ram[i] = raw_words[i] & 0x00FFFFFF;
    }
    /* Override Config[1..3] at Scratch RAM word offset 7240 (0x1C48) */
    st->scratch_ram[7240 + 1] = cfg1_prologic;
    st->scratch_ram[7240 + 2] = cfg2_ac3;
    st->scratch_ram[7240 + 3] = cfg3_downmix;

    /* Populate non-zero test PCM samples in 6-channel ping-pong ring at word 0x2000..0x2BFF */
    for (uint32_t ch = 0; ch < 6; ch++) {
        for (uint32_t s = 0; s < 512; s++) {
            int32_t sample = (int32_t)(((s * 197 + ch * 1021) & 0x1FFF) - 0x1000) << 6;
            st->scratch_ram[0x2000 + (ch << 9) + s] = (uint32_t)sample & 0x00FFFFFF;
        }
    }

    /* Populate P:0x0102..0x0123 Scratch RAM Descriptor Table */
    for (int ov = 0; ov < 6; ov++) {
        uint32_t cmd = (uint32_t)(ov + 1);
        st->core.pram[0x0100 + 2 * cmd + 0] = k_overlays[ov].file_word_offset;
        st->core.pram[0x0100 + 2 * cmd + 1] = k_overlays[ov].word_count;
    }
    /* Cmd 0x07: 6-channel PCM input ring */
    st->core.pram[0x010E] = 0x002000;
    st->core.pram[0x010F] = 0x000C00;
    /* Cmd 0x08: Analog/PCM 2-channel Output FIFO 1 */
    st->core.pram[0x0110] = 0x000001;
    st->core.pram[0x0111] = 0x000200;
    /* Cmd 0x09: S/PDIF AC-3 Output FIFO 0 */
    st->core.pram[0x0112] = 0x000000;
    st->core.pram[0x0113] = 0x000600;
    /* Cmd 0x0A: 24-word Configuration Struct at file word 7240 (0x1C48) */
    st->core.pram[0x0114] = 7240;
    st->core.pram[0x0115] = 24;
    /* Cmd 0x0B: Ping-pong bank offset (0x000000 or 0x000100) */
    st->core.pram[0x0116] = 0x000000;
    st->core.pram[0x0117] = 0x000100;
    /* Cmd 0x0C: S/PDIF zero-pad Output FIFO 0 */
    st->core.pram[0x0118] = 0x000000;
    st->core.pram[0x0119] = 0x000600;
    /* Cmd 0x0D: Output FIFO 0 */
    st->core.pram[0x011A] = 0x000000;
    st->core.pram[0x011B] = 0x000600;
    /* Cmd 0x11: Initial Scratch RAM Heap Pool (copied by Cmd 0x00 to P:0x019E..0x019F) */
    st->core.pram[0x0122] = 0x004000;
    st->core.pram[0x0123] = 0x008000;

    for (unsigned int i = 0; i < 4; i++) {
        st->epof_cur[i] = 0x20000 + i * 0x4000;
        st->epif_cur[i] = 0x30000 + i * 0x4000;
    }
}

static void run_simulation_pass(FILE *out, const uint32_t *raw_words,
                                size_t total_words, bool emulate_hbar_ep_irq,
                                bool full_pipeline_mode,
                                uint32_t cfg1, uint32_t cfg2, uint32_t cfg3,
                                uint32_t num_subframes, int cycles_per_sf,
                                const char *pass_label)
{
    fprintf(out, "\n########################################################################\n");
    fprintf(out, "  SIMULATION PASS: %s\n", pass_label);
    fprintf(out, "########################################################################\n");

    memset(&g_state.core, 0, sizeof(g_state.core));
    memset(&g_state.dma, 0, sizeof(g_state.dma));
    memset(g_state.scratch_ram, 0, SCRATCH_RAM_WORDS * sizeof(uint32_t));
    g_state.num_dma_events = 0;
    g_state.num_periph_events = 0;
    g_state.num_call_events = 0;
    memset(g_state.prof, 0, sizeof(g_state.prof));

    g_state.is_gp = false;
    g_state.core.is_gp = false;
    g_state.core.opaque = &g_state;
    g_state.core.read_peripheral = trace_read_peripheral;
    g_state.core.write_peripheral = trace_write_peripheral;

    g_state.dma.rw_opaque = &g_state;
    g_state.dma.scratch_rw = trace_scratch_rw;
    g_state.dma.fifo_rw = trace_fifo_rw;
    g_state.dma.mem_opaque = &g_state.core;
    g_state.dma.mem_read = c_dma_mem_read;
    g_state.dma.mem_write = c_dma_mem_write;

    g_state.hcr = 0;
    g_state.hsr = DSP_HSR_HTDE;
    g_state.hpcr = 0;
    g_state.hbar = 0x80;
    g_state.horx = 0;
    g_state.hotx = 0;
    g_state.interrupts = 0;
    g_state.save_cycles = 0;
    g_state.last_p0180_cmd = 0xFF;
    g_state.last_p0180_subop = 0xFF;
    g_state.emulate_hbar_ep_irq = emulate_hbar_ep_irq;
    g_state.full_pipeline_mode = full_pipeline_mode;

    dsp56k_reset_cpu(&g_state.core);
    memset(g_state.core.pram, 0, sizeof(g_state.core.pram));
    memset(g_state.core.xram, 0, sizeof(g_state.core.xram));
    memset(g_state.core.yram, 0, sizeof(g_state.core.yram));
    memset(g_state.core.predecode_table, 0, sizeof(g_state.core.predecode_table));

    /* Exact dsp_bootstrap_ep_firmware() unpacking logic from hw/xbox/mcpx/apu/dsp/dsp.c */
    for (size_t i = 0; i < 0xC8 && i < total_words; i++) {
        g_state.core.pram[0x0000 + i] = raw_words[i] & 0x00FFFFFF;
    }
    for (size_t i = 0; i < 0x17F && (0xC8 + i) < total_words; i++) {
        g_state.core.pram[0x0180 + i] = raw_words[0xC8 + i] & 0x00FFFFFF;
    }
    size_t tail_offset = 0xC8 + 0x17F;
    size_t tail_words = total_words - tail_offset;
    for (size_t i = 0; i < tail_words && (0x0300 + i) < DSP_PRAM_SIZE; i++) {
        g_state.core.pram[0x0300 + i] = raw_words[tail_offset + i] & 0x00FFFFFF;
    }

    if (full_pipeline_mode) {
        setup_guest_descriptor_table(&g_state, raw_words, total_words, cfg1, cfg2, cfg3);
    }

    g_state.call_stack_depth = 1;
    g_state.call_stack_sub[0] = 0x0000;
    g_state.call_stack_enter_cyc[0] = 0;
    g_state.prof[0x0000].entry_pc = 0x0000;
    g_state.prof[0x0000].call_count = 1;

    for (uint32_t sf = 0; sf < num_subframes; sf++) {
        g_state.current_subframe = sf;

        /* Exact dsp_start_frame_impl() from hw/xbox/mcpx/apu/dsp/dsp.c */
        g_state.interrupts |= INTERRUPT_START_FRAME;
        g_state.hsr |= DSP_HSR_HRDF;
        if (g_state.hcr & DSP_HCR_HRIE) {
            g_state.interrupts |= (1 << 2);
        }
        if (g_state.hcr & DSP_HCR_HCIE) {
            g_state.interrupts |= (1 << 3);
        }

        /* Exact gp_ep.c subframe wakeup + dsp_run(d->ep.dsp, EP_SUBFRAME_CYCLES) */
        g_state.core.is_idle = false;
        g_state.save_cycles += cycles_per_sf;

        uint32_t sf_insts = 0;
        uint32_t sf_start_pc = g_state.core.pc;
        uint64_t sf_start_cyc = g_state.core.cycle_count;

        fprintf(out, "\n--- [SUBFRAME %02u (ep_frame_div mod 8 = %u)] Start PC: 0x%04X | Block(P:0001): %u | save_cycles: %d ---\n",
                sf, sf % 8, sf_start_pc, g_state.core.pram[0x0001], g_state.save_cycles);

        uint32_t loop_detect_pc = 0xFFFFFFFF;
        uint32_t loop_detect_cnt = 0;

        while (g_state.save_cycles > 0) {
            uint32_t pc_before = g_state.core.pc;
            uint32_t sp_before = g_state.core.registers[DSP_REG_SP] & 0xF;
            uint32_t w0 = g_state.core.pram[pc_before] & 0x00FFFFFF;

            if (full_pipeline_mode) {
                ep_trace_patch_predecode(&g_state.core, pc_before);
            }

            g_state.core.disasm_prev_inst_pc = 0xFFFFFFFF;
            g_state.core.instr_cycle = 2;
            uint16_t dlen = ep_trace_disasm_instruction(&g_state.core, DSP_DISASM_MODE);
            const char *dtext = ep_trace_disasm_get_text(&g_state.core);
            char mnem_buf[256];
            snprintf(mnem_buf, sizeof(mnem_buf), "%s", g_state.core.disasm_str_instr);
            (void)dlen;
            (void)dtext;
            g_state.core.pc = pc_before;

            if (!full_pipeline_mode && sf == 0 && sf_insts < 60) {
                fprintf(out, "  [BOOT INST %02u] p:%04x  %06x  %s\n",
                        sf_insts, pc_before, w0, mnem_buf);
            }

            dsp56k_execute_instruction(&g_state.core);

            /* In full_pipeline_mode, prevent 1-instruction DO/DOR loop spin-wait false-positive */
            if (full_pipeline_mode && g_state.core.is_idle &&
                (g_state.core.registers[DSP_REG_SR] & (1 << DSP_SR_LF)) &&
                g_state.core.pc != 0x0031 && g_state.core.pc != 0x0032 &&
                w0 != 0x000086 && w0 != 0x000087) {
                g_state.core.is_idle = false;
            }

            uint32_t cyc = g_state.core.instr_cycle;
            g_state.save_cycles -= (int)cyc;
            g_state.core.cycle_count += cyc;
            sf_insts++;

            uint32_t pc_after = g_state.core.pc;
            uint32_t sp_after = g_state.core.registers[DSP_REG_SP] & 0xF;

            if (pc_after == pc_before || pc_after == loop_detect_pc) {
                loop_detect_cnt++;
                if (loop_detect_cnt == 5 && (!full_pipeline_mode || pc_before == 0x0032 || pc_before == 0x00C5)) {
                    fprintf(out, "  [SF %02u] Spinloop detected near PC 0x%04X -> 0x%04X (%s) [cyc=%" PRIu64 "]\n",
                            sf, pc_before, pc_after, mnem_buf,
                            (uint64_t)(g_state.core.cycle_count - sf_start_cyc));
                    if (full_pipeline_mode && (pc_before == 0x0032 || pc_before == 0x0031)) {
                        g_state.core.is_idle = true;
                        g_state.save_cycles = 0;
                    }
                }
            } else {
                loop_detect_pc = pc_before;
                loop_detect_cnt = 0;
            }

            uint32_t cur_sub = (g_state.call_stack_depth > 0)
                                   ? g_state.call_stack_sub[g_state.call_stack_depth - 1]
                                   : 0x0000;
            g_state.prof[cur_sub].entry_pc = cur_sub;
            g_state.prof[cur_sub].self_cycles += cyc;
            g_state.prof[cur_sub].self_insts += 1;
            if (sf < 32) {
                g_state.prof[cur_sub].subframes_seen_mask |= (1u << sf);
            }

            if (strncmp(mnem_buf, "jsr ", 4) == 0 ||
                strncmp(mnem_buf, "bsr ", 4) == 0 ||
                strncmp(mnem_buf, "js", 2) == 0 ||
                strncmp(mnem_buf, "bs", 2) == 0) {
                if (sp_after > sp_before && pc_after != pc_before + 1 && pc_after != pc_before + 2) {
                    if (pc_after == 0x0180) {
                        g_state.last_p0180_cmd = g_state.core.registers[DSP_REG_A1] & 0x00FFFFFF;
                        g_state.last_p0180_subop = g_state.core.registers[DSP_REG_B1] & 0x00FFFFFF;
                    }
                    if (g_state.num_call_events < MAX_CALL_EVENTS) {
                        CallEvent *ce = &g_state.call_events[g_state.num_call_events++];
                        ce->subframe = sf;
                        ce->from_pc = pc_before;
                        ce->to_pc = pc_after;
                        ce->sp_depth = (uint32_t)g_state.call_stack_depth;
                        ce->cycle = g_state.core.cycle_count;
                        ce->kind = "CALL";
                    }
                    if (!full_pipeline_mode || g_state.call_stack_depth <= 3 || pc_after == 0x0180 || pc_after == 0x0304) {
                        fprintf(out, "  [SF %02u] %*sCALL 0x%04X -> 0x%04X (%s) [A1=0x%02X B1=0x%02X R0=0x%04X N0=0x%04X N1=0x%04X cyc=%" PRIu64 "]\n",
                                sf, g_state.call_stack_depth * 2, "",
                                pc_before, pc_after, mnem_buf,
                                g_state.core.registers[DSP_REG_A1],
                                g_state.core.registers[DSP_REG_B1],
                                g_state.core.registers[DSP_REG_R0],
                                g_state.core.registers[DSP_REG_N0],
                                g_state.core.registers[DSP_REG_N1],
                                (uint64_t)(g_state.core.cycle_count - sf_start_cyc));
                    }
                    if (g_state.call_stack_depth < 63) {
                        g_state.call_stack_sub[g_state.call_stack_depth] = pc_after;
                        g_state.call_stack_enter_cyc[g_state.call_stack_depth] = g_state.core.cycle_count;
                        g_state.call_stack_depth++;
                    }
                    g_state.prof[pc_after].entry_pc = pc_after;
                    g_state.prof[pc_after].call_count++;
                }
            } else if (strncmp(mnem_buf, "rts", 3) == 0 || strncmp(mnem_buf, "rti", 3) == 0) {
                if (g_state.call_stack_depth > 1) {
                    g_state.call_stack_depth--;
                    uint32_t exited_sub = g_state.call_stack_sub[g_state.call_stack_depth];
                    uint64_t enter_cyc = g_state.call_stack_enter_cyc[g_state.call_stack_depth];
                    uint64_t inc_cyc = g_state.core.cycle_count - enter_cyc;
                    g_state.prof[exited_sub].inclusive_cycles += inc_cyc;
                    if (!full_pipeline_mode || g_state.call_stack_depth <= 3 || exited_sub == 0x0180 || exited_sub == 0x0304) {
                        fprintf(out, "  [SF %02u] %*sRET  0x%04X (from sub 0x%04X) -> 0x%04X [sub_cyc=%" PRIu64 "]\n",
                                sf, g_state.call_stack_depth * 2, "",
                                pc_before, exited_sub, pc_after, inc_cyc);
                    }
                }
            }

            if (g_state.core.is_idle) {
                break;
            }
        }

        uint64_t sf_cycles = g_state.core.cycle_count - sf_start_cyc;
        fprintf(out, "--- [SUBFRAME %02u END] End PC: 0x%04X | Block(P:0001): %u | Insts: %u | Cycles: %" PRIu64 " | Idle: %s ---\n",
                sf, g_state.core.pc, g_state.core.pram[0x0001], sf_insts, sf_cycles,
                g_state.core.is_idle ? "YES" : "NO (BUDGET_EXHAUSTED)");
    }

    fprintf(out, "\n========================================================================\n");
    fprintf(out, "  PERIPHERAL READ / WRITE LOG (%u events)\n", g_state.num_periph_events);
    fprintf(out, "========================================================================\n");
    uint32_t max_p_print = (g_state.num_periph_events > 120) ? 120 : g_state.num_periph_events;
    for (uint32_t i = 0; i < max_p_print; i++) {
        const PeriphEvent *pe = &g_state.periph_events[i];
        fprintf(out, "[%03u] SF %02u | PC 0x%04X | %s 0x%06X %s 0x%06X\n",
                i, pe->subframe, pe->pc,
                pe->is_write ? "WRITE" : "READ ",
                pe->addr,
                pe->is_write ? "<-" : "->",
                pe->val);
    }

    fprintf(out, "\n========================================================================\n");
    fprintf(out, "  DMA 7-WORD DESCRIPTOR LOG (%u descriptor blocks executed)\n", g_state.num_dma_events);
    fprintf(out, "========================================================================\n");
    for (uint32_t i = 0; i < g_state.num_dma_events; i++) {
        const DMATransferEvent *ev = &g_state.dma_events[i];
        uint32_t ctrl = ev->desc.control;
        bool dsp_interleave = (ctrl >> 0) & 1;
        bool dir = (ctrl & NODE_CONTROL_DIRECTION) != 0;
        bool writeback = (ctrl >> 4) & 1;
        uint32_t buf_id = (ctrl >> 5) & 0xF;
        uint32_t fmt = (ctrl >> 10) & 0x7;
        bool eol = (ev->desc.next_block & NODE_POINTER_EOL) != 0;
        int dsp_space = DSP_SPACE_X;
        uint32_t dsp_addr = 0;
        decode_dma_addr(ev->desc.dsp_offset, &dsp_space, &dsp_addr);

        fprintf(out,
                "[%03u] SF %02u (Blk %u) | Cmd=0x%02X(B=0x%X) | TriggerPC=0x%04X | Node %s:0x%04X (ptr=0x%06X)\n"
                "      Raw Words : [0]next=0x%06X [1]ctrl=0x%06X [2]cnt=0x%06X [3]dsp=0x%06X\n"
                "                  [4]sc_off=0x%06X [5]sc_base=0x%06X [6]sc_size=0x%06X\n"
                "      Decoded   : Dir=%s | Buf=%s | DSP=%s:0x%04X (raw 0x%04X) | Count=%u (0x%X)\n"
                "                  Fmt=%s | Intlv=%d | Writeback=%d | Scratch=0x%06X+0x%06X (sz=0x%X) | EOL=%d\n",
                i, ev->subframe, ev->block_idx, ev->cmd_id, ev->sub_op,
                ev->trigger_pc, space_name(ev->node_space), ev->node_addr, ev->raw_node_ptr,
                ev->desc.next_block, ev->desc.control, ev->desc.count, ev->desc.dsp_offset,
                ev->desc.scratch_offset, ev->desc.scratch_base, ev->desc.scratch_size,
                dir ? "DSP->Scratch/FIFO (Write)" : "Scratch/FIFO->DSP (Read)",
                buf_id_name(buf_id),
                space_name(dsp_space), dsp_addr, ev->desc.dsp_offset,
                ev->desc.count, ev->desc.count,
                format_name(fmt), dsp_interleave ? 1 : 0, writeback ? 1 : 0,
                ev->desc.scratch_base, ev->desc.scratch_offset, ev->desc.scratch_size + 1,
                eol ? 1 : 0);
    }

    fprintf(out, "\n========================================================================\n");
    fprintf(out, "  SUBROUTINE CYCLE PROFILE (%s)\n", pass_label);
    fprintf(out, "========================================================================\n");
    fprintf(out, "Subroutine  | Calls | Self Insts | Self Cycles | Inclusive Cycles | Subframes Mask\n");
    fprintf(out, "------------+-------+------------+-------------+------------------+---------------\n");
    for (uint32_t p = 0; p < DSP_PRAM_SIZE; p++) {
        if (g_state.prof[p].call_count > 0 || g_state.prof[p].self_cycles > 0) {
            fprintf(out, "P:0x%04X    | %5u | %10u | %11" PRIu64 " | %16" PRIu64 " | 0x%04X\n",
                    p,
                    g_state.prof[p].call_count,
                    g_state.prof[p].self_insts,
                    g_state.prof[p].self_cycles,
                    g_state.prof[p].inclusive_cycles,
                    g_state.prof[p].subframes_seen_mask);
        }
    }
}

int main(int argc, char **argv)
{
    const char *bin_path = (argc > 1) ? argv[1] : "tools/dolby_ep.bin";
    const char *out_path = (argc > 2) ? argv[2] : "tools/trace_ep_rom_output.txt";

    FILE *f = fopen(bin_path, "rb");
    if (!f) {
        fprintf(stderr, "[-] Cannot open %s\n", bin_path);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0 || (file_size % 4) != 0) {
        fprintf(stderr, "[-] Invalid firmware size: %ld bytes\n", file_size);
        fclose(f);
        return 1;
    }

    size_t total_words = (size_t)(file_size / 4);
    uint32_t *raw_words = (uint32_t *)calloc(total_words, sizeof(uint32_t));
    if (!raw_words) {
        fclose(f);
        return 1;
    }

    if (fread(raw_words, sizeof(uint32_t), total_words, f) != total_words) {
        fprintf(stderr, "[-] Failed to read %zu words\n", total_words);
        free(raw_words);
        fclose(f);
        return 1;
    }
    fclose(f);

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "[-] Cannot open output log %s\n", out_path);
        free(raw_words);
        return 1;
    }

    fprintf(out, "========================================================================\n");
    fprintf(out, "  PHASE 117A: DETERMINISTIC DISASSEMBLY & TRACE OF %s\n", bin_path);
    fprintf(out, "========================================================================\n");
    fprintf(out, "File Size        : %ld bytes (%zu 32-bit LE words)\n", file_size, total_words);
    fprintf(out, "Minimum Gate     : %d words (0xC8 + 0x17F = 583 words / 2332 bytes)\n", 0xC8 + 0x17F);
    fprintf(out, "Segment 1        : File words 0..199     (200 words)  -> P:0x0000..0x00C7\n");
    fprintf(out, "Segment 2        : File words 200..583   (384 words)  -> P:0x0180..0x02FF\n");
    for (int ov = 0; ov < 6; ov++) {
        fprintf(out, "Overlay #%u       : File words %4u..%4u (%4u words) -> Runtime P:0x0300..0x%04X (id=0x%06X)\n",
                k_overlays[ov].ovl_num,
                k_overlays[ov].file_word_offset,
                k_overlays[ov].file_word_offset + k_overlays[ov].word_count - 1,
                k_overlays[ov].word_count,
                0x0300 + k_overlays[ov].word_count - 1,
                k_overlays[ov].version_id);
    }
    fprintf(out, "Default Config   : File words 7240..7263 (24 words)   -> Runtime X:0x0BC0..0x0BD7\n");

    memset(&g_state, 0, sizeof(g_state));
    g_state.scratch_ram = (uint32_t *)calloc(SCRATCH_RAM_WORDS, sizeof(uint32_t));
    if (!g_state.scratch_ram) {
        fclose(out);
        free(raw_words);
        return 1;
    }

    /* Disassemble Segment 1 and Segment 2 */
    dsp56k_reset_cpu(&g_state.core);
    memset(g_state.core.pram, 0, sizeof(g_state.core.pram));
    for (size_t i = 0; i < 0xC8; i++) {
        g_state.core.pram[0x0000 + i] = raw_words[i] & 0x00FFFFFF;
    }
    for (size_t i = 0; i < 0x180; i++) {
        g_state.core.pram[0x0180 + i] = raw_words[0xC8 + i] & 0x00FFFFFF;
    }

    dump_disassembly_range(&g_state.core, out,
                           "SEGMENT 1: RESIDENT EXECUTIVE SUPERVISOR",
                           0x0000, 0x00C7);
    dump_disassembly_range(&g_state.core, out,
                           "SEGMENT 2: RESIDENT DMA & HEAP SERVICE KERNEL",
                           0x0180, 0x02FF);

    /* Disassemble each of the 6 Relocatable Overlays at their runtime base P:0x0300 */
    for (int ov = 0; ov < 6; ov++) {
        memset(&g_state.core.pram[0x0300], 0, (DSP_PRAM_SIZE - 0x0300) * sizeof(uint32_t));
        for (uint32_t w = 0; w < k_overlays[ov].word_count; w++) {
            g_state.core.pram[0x0300 + w] =
                raw_words[k_overlays[ov].file_word_offset + w] & 0x00FFFFFF;
        }
        dump_disassembly_range(&g_state.core, out,
                               k_overlays[ov].desc,
                               0x0300, 0x0300 + k_overlays[ov].word_count - 1);
    }

    /* Pass 1: Exact current dsp.c behavior (is_gp == false on HBAR 0xFFFFC5) */
    run_simulation_pass(out, raw_words, total_words, false, false, 1, 1, 0, 12,
                        EP_SUBFRAME_CYCLES,
                        "PASS 1: Exact dsp.c (is_gp=false on HBAR 0xFFFFC5, unpopulated P:0x0100)");

    /* Pass 2: With HBAR 0xFFFFC5 returning dsp->interrupts | INTERRUPT_DMA_EOL, unpopulated P:0x0100 */
    run_simulation_pass(out, raw_words, total_words, true, false, 1, 1, 0, 12,
                        EP_SUBFRAME_CYCLES,
                        "PASS 2: With EP HBAR 0xFFFFC5 IRQ & DMA EOL active (unpopulated P:0x0100)");

    /* Pass 3: Full 6-Block AC-3 Pipeline (Overlay #1 + Overlays #2, #3, #4 across blocks 0..5) */
    run_simulation_pass(out, raw_words, total_words, true, true, 1, 1, 0, 6,
                        800000,
                        "PASS 3: Full 6-Block AC-3 Pipeline (Overlay #1 + Overlays #2/#3/#4, Blocks 0..5)");

    /* Pass 4: Overlay #5 (Mono Downmixer) + Overlay #6 (Lo/Ro Stereo Downmixer) */
    run_simulation_pass(out, raw_words, total_words, true, true, 0, 0, 1, 1,
                        50000,
                        "PASS 4A: Overlay #5 (Mono 5-to-2 Downmixer, Config[3]=1)");
    run_simulation_pass(out, raw_words, total_words, true, true, 0, 0, 2, 1,
                        50000,
                        "PASS 4B: Overlay #6 (Stereo Lo/Ro 5-to-2 Downmixer, Config[3]=2)");

    fclose(out);
    free(g_state.scratch_ram);
    free(raw_words);
    printf("[+] Forensic disassembly and trace written to %s\n", out_path);
    return 0;
}
