Phase 52 Reconnaissance Report: Opcode Dispatch & Loop Handling

The inspection of hw/xbox/mcpx/apu/dsp/interp/ was conducted to analyze
opcode dispatch and loop machinery for DSP56300 DO FOREVER,
expr (0x000100).

------------------------------------------------------------------------

Target 1: Opcode Dispatch Table & Lookup Switch

From hw/xbox/mcpx/apu/dsp/interp/dsp_cpu.c:

c

/\* runtime shit \*/

// start_time = SDL_GetTicks();

dsp-\>num_inst = 0;

dsp-\>exception_debugging = true;

dsp-\>disasm_prev_inst_pc = 0xFFFFFFFF;

}

static const OpcodeEntry \*lookup_opcode_slow(uint32_t op) {

for (int i = 0; i \< ARRAY_SIZE(nonparallel_opcodes); i++) {

if ((op & nonparallel_matches\[i\]\[0\]) ==
nonparallel_matches\[i\]\[1\]) {

if (nonparallel_opcodes\[i\].match_func

&& !nonparallel_opcodes\[i\].match_func(op)) continue;

return &nonparallel_opcodes\[i\];

}

}

fprintf(stderr, \"op = %08x\\n\", op);

assert(!\"Invalid op code in dsp_cpu\");

return NULL;

}

static const OpcodeEntry \*lookup_opcode(uint32_t op) {

static struct opcache_entry {

uint32_t op;

const OpcodeEntry \*entry;

} opcache\[256\];

uint8_t tag =

((op \>\> 24) & 0xff) \^

((op \>\> 16) & 0xff) \^

((op \>\> 8) & 0xff) \^

((op \>\> 0) & 0xff);

if (opcache\[tag\].op != op \|\| opcache\[tag\].entry == NULL) {

opcache\[tag\].op = op;

opcache\[tag\].entry = lookup_opcode_slow(op);

}

return opcache\[tag\].entry;

}

------------------------------------------------------------------------

Target 2: Existing DO / Loop Instruction Handlers

1\. In hw/xbox/mcpx/apu/dsp/interp/dsp_cpu.c:

text

224: { \"0000011001MMMRRR0S000000\", \"do \[X or Y\]:ea, expr\",
dis_do_ea, emu_do_ea, match_MMMRRR },

225: { \"0000011000aaaaaa0S000000\", \"do \[X or Y\]:aa, expr\",
dis_do_aa, emu_do_aa },

226: { \"00000110iiiiiiii1000hhhh\", \"do #xxx, expr\", dis_do_imm,
emu_do_imm },

227: { \"0000011011DDDDDD00000000\", \"do S, expr\", dis_do_reg,
emu_do_reg },

228: { \"000000000000001000000011\", \"do_f\", NULL, NULL },

323: { \"0000011001MMMRRR0S100000\", \"rep \[X or Y\]:ea\", dis_rep_ea,
emu_rep_ea, match_MMMRRR },

324: { \"0000011000aaaaaa0S100000\", \"rep \[X or Y\]:aa\", dis_rep_aa,
emu_rep_aa },

325: { \"00000110iiiiiiii1010hhhh\", \"rep #xxx\", dis_rep_imm,
emu_rep_imm },

326: { \"0000011011dddddd00100000\", \"rep S\", dis_rep_reg, emu_rep_reg
},

397: dsp-\>loop_rep = 0;

686: if (dsp-\>loop_rep) {

695: dsp-\>loop_rep = 0;

759: if (dsp-\>loop_rep) {

2\. In hw/xbox/mcpx/apu/dsp/interp/dsp_emu.c.inc:

text

6714:static void emu_do_aa(dsp_core_t\* dsp)

6734:static void emu_do_imm(dsp_core_t\* dsp)

6750:static void emu_do_ea(dsp_core_t\* dsp)

6771:static void emu_do_reg(dsp_core_t\* dsp)

7916:static void emu_rep_aa(dsp_core_t\* dsp)

7922: dsp-\>loop_rep = 1; /\* We are now running rep \*/

7929:static void emu_rep_imm(dsp_core_t\* dsp)

7935: dsp-\>loop_rep = 1; /\* We are now running rep \*/

7943:static void emu_rep_ea(dsp_core_t\* dsp)

7952: dsp-\>loop_rep = 1; /\* We are now running rep \*/

7960:static void emu_rep_reg(dsp_core_t\* dsp)

7968: dsp-\>loop_rep = 1; /\* We are now running rep \*/

3\. Standard DO Push Pattern (dsp_emu.c.inc:6734-6748):

c

static void emu_do_imm(dsp_core_t\* dsp)

{

dsp_stack_push(dsp, dsp-\>registers\[DSP_REG_LA\],
dsp-\>registers\[DSP_REG_LC\], 0);

dsp-\>registers\[DSP_REG_LA\] = read_memory_p(dsp, dsp-\>pc+1) &
BITMASK(16);

dsp-\>cur_inst_len++;

dsp_stack_push(dsp, dsp-\>pc+dsp-\>cur_inst_len,
dsp-\>registers\[DSP_REG_SR\], 0);

dsp-\>registers\[DSP_REG_SR\] \|= (1\<\<DSP_SR_LF);

dsp-\>registers\[DSP_REG_LC\] = ((dsp-\>cur_inst\>\>8) & BITMASK(8))

\+ ((dsp-\>cur_inst & BITMASK(4))\<\<8);

dsp-\>instr_cycle += 4;

}

4\. Existing Loop End Evaluation (dsp_cpu.c:712-732):

c

/\* When running a DO loop, we test the end of loop with the \*/

/\* updated PC, pointing to last instruction of the loop \*/

if (dsp-\>registers\[DSP_REG_SR\] & (1\<\<DSP_SR_LF)) {

/\* Did we execute the last instruction in loop ? \*/

if (dsp-\>pc == dsp-\>registers\[DSP_REG_LA\] + 1) {

\--dsp-\>registers\[DSP_REG_LC\];

dsp-\>registers\[DSP_REG_LC\] &= BITMASK(16);

if (dsp-\>registers\[DSP_REG_LC\] == 0) {

/\* end of loop \*/

uint32_t saved_pc, saved_sr;

dsp_stack_pop(dsp, &saved_pc, &saved_sr);

dsp-\>registers\[DSP_REG_SR\] &= 0x7f;

dsp-\>registers\[DSP_REG_SR\] \|= saved_sr & (1\<\<DSP_SR_LF);

dsp_stack_pop(dsp, &dsp-\>registers\[DSP_REG_LA\],
&dsp-\>registers\[DSP_REG_LC\]);

} else {

/\* Loop one more time \*/

dsp-\>pc = dsp-\>registers\[DSP_REG_SSH\];

}

}

}

------------------------------------------------------------------------

Target 3: Register Definitions for LA, LC, and Status Register (SR)
Flags

From hw/xbox/mcpx/apu/dsp/interp/dsp_cpu_regs.h:

text

48:#define DSP_SR_LF 0x0f

49:#define DSP_SR_FV 0x10

95:#define DSP_REG_SR 0x39

100:#define DSP_REG_LA 0x3e

101:#define DSP_REG_LC 0x3f

104:#define DSP_REG_LCSAVE 0x30

------------------------------------------------------------------------

Implementation Analysis for DO FOREVER, expr

1.  **Opcode Mask**:

    - 0x000100 in 24-bit binary is 0000 0000 0000 0001 0000 0000.

    - In nonparallel_opcodes\[\], replace the unhandled stub entry at
      line 228 (000000000000001000000011 / do_f) with:

c

{ \"000000000000000100000000\", \"do forever, expr\", NULL,
emu_do_forever },

2.  **Execution Handler (emu_do_forever)**:

    - Pushes (LA, LC) to stack.

    - Reads target loop address from read_memory_p(dsp, dsp-\>pc +
      1) into registers\[DSP_REG_LA\].

    - Increments dsp-\>cur_inst_len++ to consume Word 2.

    - Pushes (pc + cur_inst_len, SR) to stack.

    - Asserts both DSP_SR_LF (Bit 15) and DSP_SR_FV (Bit 16)
      in registers\[DSP_REG_SR\].

    - LC is preserved and not modified.

    - Adds 4 cycles (dsp-\>instr_cycle += 4).

3.  **Loop Boundary Handler**:

    - In dsp_cpu.c at line 715, check if (dsp-\>registers\[DSP_REG_SR\]
      & (1 \<\< DSP_SR_FV)).

    - If set, skip LC decrement and branch directly to dsp-\>pc =
      dsp-\>registers\[DSP_REG_SSH\];.

    - When a loop terminates or unwinds via ENDDO,
      restore/clear DSP_SR_FV along with DSP_SR_LF.
