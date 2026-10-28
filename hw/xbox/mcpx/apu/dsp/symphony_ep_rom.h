/*
 * Xemu Symphony - MCPX APU Encode Processor Clean-Room Micro-Kernel
 *
 * Copyright (c) 2026 Will Bonnett
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef HW_XBOX_MCPX_APU_DSP_SYMPHONY_EP_ROM_H
#define HW_XBOX_MCPX_APU_DSP_SYMPHONY_EP_ROM_H

#include <stdint.h>
#include <stddef.h>

#define SYMPHONY_EP_SEG1_WORDS 200  /* 0xC8 words, loaded at P:0x0000..0x00C7 */
#define SYMPHONY_EP_SEG2_WORDS 384  /* 0x180 words, loaded at P:0x0180..0x02FF */
#define SYMPHONY_EP_ROM_WORDS  (SYMPHONY_EP_SEG1_WORDS + SYMPHONY_EP_SEG2_WORDS) /* 584 words */

/*
 * Combined 584-word firmware image mirroring external binary layout:
 * - Words   0..199 (0x000..0x0C7): Resident Executive Supervisor (Segment 1)
 * - Words 200..583 (0x0C8..0x247): Resident DMA & Heap Service Kernel (Segment 2)
 */
// clang-format off
static const uint32_t symphony_ep_rom[SYMPHONY_EP_ROM_WORDS] = {
    /* ==================================================================== */
    /*   SEGMENT 1: RESIDENT EXECUTIVE SUPERVISOR (P:0x0000 .. P:0x00C7)    */
    /*   200 Words (0xC8)                                                   */
    /* ==================================================================== */

    /* Word 0 (P:0x0000): Hardware Reset Vector */
    0x050C08,           /* P:0x0000: bra p:$0008 (Boot initialization) */

    /* Words 1..7 (P:0x0001..0x0007): Executive Status Mailbox */
    0x000000,           /* P:0x0001: Modulo-6 AC-3 block index (0..5) */
    0x000001,           /* P:0x0002: Overlay #1 cold-start init flag (1 -> 0) */
    0x000001,           /* P:0x0003: Overlays #2..#4 cold-start init flag (1 -> 0) */
    0x000000,           /* P:0x0004: Last block execution cycle delta */
    0x000000,           /* P:0x0005: Subframe start timer snapshot (X:0xFFFFB3) */
    0xCCCCCC,           /* P:0x0006: Watchdog signature word (0xCCCCCC = OK) */
    0x000000,           /* P:0x0007: Cumulative frame heartbeat counter */

    /* Words 8..40 (P:0x0008..0x0028): Cold-Boot Initialization */
    0x240000,           /* P:0x0008: move #$00,x0 */
    0x077084, 0x000004, /* P:0x0009: movem x0,p:$0004 (Clear cycle delta) */
    0x077084, 0x000005, /* P:0x000B: movem x0,p:$0005 (Clear start timer) */
    0x07F432, 0xFFFFFF, /* P:0x000D: movep #$ffffff,x:$ffffb2 */
    0x07F430, 0x000001, /* P:0x000F: movep #$000001,x:$ffffb0 */
    0x07F431, 0x000001, /* P:0x0011: movep #$000001,x:$ffffb1 */
    0x0500BB,           /* P:0x0013: movec #$00,sp (Reset stack pointer) */
    0x05F42A, 0x000BE0, /* P:0x0014: movec #$000be0,ep (Set Extension Pointer) */
    0x0520B8,           /* P:0x0016: movec #$20,mr (Set Mode Register) */
    0x0AFA74,           /* P:0x0017: bset #20,omr (Set OMR bit 20) */
    0x200013,           /* P:0x0018: clr a */
    0x077088, 0x000001, /* P:0x0019: movem a0,p:$0001 (Clear block counter) */
    0x000008,           /* P:0x001B: inc a */
    0x077088, 0x000002, /* P:0x001C: movem a0,p:$0002 (P:0x0002 = 1) */
    0x077088, 0x000003, /* P:0x001E: movem a0,p:$0003 (P:0x0003 = 1) */
    0x56F400, 0x000000, /* P:0x0020: move #$000000,a (Cmd 0x00) */
    0x0BF080, 0x000180, /* P:0x0022: jsr p:$0180 (Call Service Kernel Cmd 0x00) */
    0x200003,           /* P:0x0024: tst a */
    0x0D1042, 0x000029, /* P:0x0025: bne p:$0029 (Branch to loop on error) */
    0x08F485, 0x000002, /* P:0x0027: movep #$000002,x:$ffffc5 (Clear START_FRAME) */

    /* Words 41..47 (P:0x0029..0x002F): Subframe Supervisor Loop Entry */
    0x07F08E, 0x000007, /* P:0x0029: movem p:$0007,a (Read heartbeat counter) */
    0x014180,           /* P:0x002B: add #$01,a (Increment heartbeat) */
    0x07708E, 0x000007, /* P:0x002C: movem a,p:$0007 (Store heartbeat counter) */
    0x08F484, 0x000001, /* P:0x002E: movep #$000001,x:$ffffc4 (Assert HPCR = 1) */

    /* Word 48 (P:0x0030): Subframe Yield Opcode */
    0x000086,           /* P:0x0030: wait (Yield control instantly to host event loop) */

    /* Words 49..67 (P:0x0031..0x0043): Subframe Wakeup & Frame Processing */
    0x44F000, 0xFFFFB3, /* P:0x0031: move x:$ffffb3,x0 (Snapshot hardware timer) */
    0x077084, 0x000005, /* P:0x0033: movem x0,p:$0005 (Save start timestamp) */
    0x08F485, 0x000002, /* P:0x0035: movep #$000002,x:$ffffc5 (Clear HBAR bit 1) */
    0x07F08E, 0x000001, /* P:0x0037: movem p:$0001,a (A = current block index 0..5) */
    0x60F403, 0x000BC0, /* P:0x0039: tst a #$000bc0,r0 (Test A, R0 = X:0x0BC0) */
    0x052409,           /* P:0x003B: bne p:$0044 (Skip Cmd 0x0A if Block != 0) */
    0x56F400, 0x00000A, /* P:0x003C: move #$00000a,a (Cmd 0x0A: Read Config Struct) */
    0x382000,           /* P:0x003E: move #$20,n0 (Max count = 0x20) */
    0x0BF080, 0x000180, /* P:0x003F: jsr p:$0180 (Call Service Kernel Cmd 0x0A) */
    0x200003,           /* P:0x0041: tst a */
    0x0D1042, 0x000029, /* P:0x0042: bne p:$0029 */

    /* Words 68..72 (P:0x0044..0x0048): Clear Cold-Start Initialization Flags */
    0x240000,           /* P:0x0044: move #$00,x0 */
    0x077084, 0x000002, /* P:0x0045: movem x0,p:$0002 (Clear Overlay #1 init flag) */
    0x077084, 0x000003, /* P:0x0047: movem x0,p:$0003 (Clear Overlays #2..#4 init flag) */

    /* Words 73..79 (P:0x0049..0x004F): Modulo-6 Block Counter Increment */
    0x07F08E, 0x000001, /* P:0x0049: movem p:$0001,a (A = current block index) */
    0x014180,           /* P:0x004B: add #$01,a (A = A + 1) */
    0x014685,           /* P:0x004C: cmp #$06,a (Compare with 6) */
    0x202113,           /* P:0x004D: clr a ifge (If A >= 6, wrap A = 0) */
    0x07708C, 0x000001, /* P:0x004E: movem a1,p:$0001 (Store updated block index 0..5) */

    /* Words 80..83 (P:0x0050..0x0053): Toggle Ping-Pong Bank Bit 8 */
    0x56F400, 0x00000B, /* P:0x0050: move #$00000b,a (Cmd 0x0B: Toggle bank bit 8) */
    0x0BF080, 0x000180, /* P:0x0052: jsr p:$0180 (Call Service Kernel Cmd 0x0B) */

    /* Words 84..90 (P:0x0054..0x005A): Measure Elapsed Subframe Cycle Delta */
    0x56F000, 0xFFFFB3, /* P:0x0054: move x:$ffffb3,a (Read end timestamp) */
    0x07F084, 0x000005, /* P:0x0056: movem p:$0005,x0 (Read start timestamp) */
    0x200044,           /* P:0x0058: sub x0,a (A = elapsed cycles) */
    0x07708C, 0x000004, /* P:0x0059: movem a1,p:$0004 (Store cycle delta in P:0x0004) */

    /* Word 91 (P:0x005B): Loop Back to Supervisor Entry P:0x0029 */
    0x0C0029,           /* P:0x005B: jmp p:$0029 (Jump to supervisor loop entry) */

    /* Words 92..199 (P:0x005C..0x00C7): Padding NOPs (108 words) */
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000,

    /* ==================================================================== */
    /*   SEGMENT 2: RESIDENT DMA & HEAP SERVICE KERNEL (P:0x0180 .. 0x02FF)  */
    /*   384 Words (0x180)                                                  */
    /* ==================================================================== */

    /* P:0x0180..0x019D: Command Dispatcher Gateway (30 words) */
    0x62F400, 0x000100, /* P:0x0180: move #$000100,r2 (Base of Descriptor Table P:0x0100) */
    0x219A00,           /* P:0x0182: move a1,n2 (N2 = Command ID 0x00..0x19) */
    0x3D0100,           /* P:0x0183: move #$01,n5 (N5 = 1 default sync DMA) */
    0x014085,           /* P:0x0184: cmp #$00,a */
    0x05A50F,           /* P:0x0185: beq p:$000214 (Cmd 0x00 -> Init Heap & Hardware) */
    0x204A00,           /* P:0x0186: move (r2)+n2,r2 */
    0x204A00,           /* P:0x0187: move (r2)+n2,r2 (R2 = 0x0100 + 2 * CmdID) */
    0x07DA91,           /* P:0x0188: movem p:(r2)+,r1 (R1 = Scratch offset / FIFO id) */
    0x015185,           /* P:0x0189: cmp #$11,a */
    0x0514CE,           /* P:0x018A: bge p:$0001f8 (Cmd 0x11..0x19 -> Dynamic Heap Mgr) */
    0x014685,           /* P:0x018B: cmp #$06,a */
    0x05F454,           /* P:0x018C: ble p:$0001c0 (Cmd 0x01..0x06 -> Overlay load) */
    0x014985,           /* P:0x018D: cmp #$09,a */
    0x05A48C,           /* P:0x018E: beq p:$0001da (Cmd 0x09 -> AC-3 Bitstream FIFO 0) */
    0x014A85,           /* P:0x018F: cmp #$0a,a */
    0x05A44B,           /* P:0x0190: beq p:$0001bb (Cmd 0x0A -> Read Config Struct) */
    0x014B85,           /* P:0x0191: cmp #$0b,a */
    0x05A451,           /* P:0x0192: beq p:$0001c3 (Cmd 0x0B -> Toggle Ping-Pong bank) */
    0x014C85,           /* P:0x0193: cmp #$0c,a */
    0x05A495,           /* P:0x0194: beq p:$0001e9 (Cmd 0x0C -> Zero-Pad FIFO 0) */
    0x014D85,           /* P:0x0195: cmp #$0d,a */
    0x05A498,           /* P:0x0196: beq p:$0001ee (Cmd 0x0D -> Output FIFO Write) */
    0x014885,           /* P:0x0197: cmp #$08,a */
    0x05A45B,           /* P:0x0198: beq p:$0001d3 (Cmd 0x08 -> Downmix FIFO 1) */
    0x014E85,           /* P:0x0199: cmp #$0e,a */
    0x05A487,           /* P:0x019A: beq p:$0001e1 (Cmd 0x0E -> 12-word Meter levels) */
    0x014F85,           /* P:0x019B: cmp #$0f,a */
    0x05A482,           /* P:0x019C: beq p:$0001de (Cmd 0x0F -> 4-word Meter levels) */
    0x050C4C,           /* P:0x019D: bra p:$01c9 (Cmd 0x07 -> PCM Channel Read) */

    /* P:0x019E..0x01BA: Internal State Mailbox & 7-word DMA descriptor buffer (29 words) */
    0x000000, 0x000000, 0x000000, /* P:0x019E..0x01A0 (Heap base, size, rsvd) */
    0x000001,                     /* P:0x01A1: pflushun */
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, /* P:0x01A2..0x01A6 */
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, /* P:0x01A7..0x01AB */
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, /* P:0x01AC..0x01B0 */
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, /* P:0x01B1..0x01B5 */
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, /* P:0x01B6..0x01BA */

    /* P:0x01BB..0x01BF: Cmd 0x0A Handler (Read Config Struct, 5 words) */
    0x07DA8E,           /* P:0x01BB: movem p:(r2)+,a */
    0x230400,           /* P:0x01BC: move n0,x0 */
    0x219845,           /* P:0x01BD: cmp x0,a a1,n0 */
    0x0508C6,           /* P:0x01BE: bsr p:$0224 */
    0x050C90,           /* P:0x01BF: bra p:$020f */

    /* P:0x01C0..0x01C2: Cmd 0x01..0x06 (Overlay loads) -> Clean-Room: return success (3 words) */
    0x200013,           /* P:0x01C0: clr a (Success status A = 0) */
    0x00000C,           /* P:0x01C1: rts (Immediate return without Scratch RAM DMA) */
    0x000000,           /* P:0x01C2: nop */

    /* P:0x01C3..0x01C8: Cmd 0x0B Handler (Toggle Ping-Pong bank bit 8 in P:0x0116, 6 words) */
    0x07F084, 0x000116, /* P:0x01C3: movem p:$0116,x0 */
    0x0BC448,           /* P:0x01C5: bchg #8,x0 */
    0x077084, 0x000116, /* P:0x01C6: movem x0,p:$0116 */
    0x050C87,           /* P:0x01C8: bra p:$020f */

    /* P:0x01C9..0x01D2: Cmd 0x07 Handler (Read 256 PCM samples for channel N1, 10 words) */
    0x232F00,           /* P:0x01C9: move n1,b */
    0x0C1D93,           /* P:0x01CA: asl #$09,b,b */
    0x222400,           /* P:0x01CB: move r1,x0 */
    0x200048,           /* P:0x01CC: add x0,b */
    0x07F08E, 0x000116, /* P:0x01CD: movem p:$0116,a */
    0x200010,           /* P:0x01CF: add b,a */
    0x219100,           /* P:0x01D0: move a1,r1 */
    0x050893,           /* P:0x01D1: bsr p:$0224 */
    0x050C5D,           /* P:0x01D2: bra p:$020f */

    /* P:0x01D3..0x01D9: Cmd 0x08 Handler (Output FIFO 1, 7 words) */
    0x232E00,           /* P:0x01D3: move n1,a */
    0x014085,           /* P:0x01D4: cmp #$00,a */
    0x05245A,           /* P:0x01D5: bne p:$00020f */
    0x3A0100,           /* P:0x01D6: move #$01,n2 */
    0x3C0000,           /* P:0x01D7: move #$00,n4 */
    0x0508C0,           /* P:0x01D8: bsr p:$0238 */
    0x050C56,           /* P:0x01D9: bra p:$020f */

    /* P:0x01DA..0x01DD: Cmd 0x09 Handler (Output FIFO 0, 4 words) */
    0x3A0000,           /* P:0x01DA: move #$00,n2 */
    0x3C0000,           /* P:0x01DB: move #$00,n4 */
    0x05089C,           /* P:0x01DC: bsr p:$0238 */
    0x050C52,           /* P:0x01DD: bra p:$020f */

    /* P:0x01DE..0x01E8: Cmd 0x0F & 0x0E Handlers (Meter mailbox copies, 11 words) */
    0x61F400, 0x0001AB, /* P:0x01DE: move #$0001ab,r1 (4 meter words target) */
    0x050C03,           /* P:0x01E0: bra p:$01e3 */
    0x61F400, 0x0001AF, /* P:0x01E1: move #$0001af,r1 (12 meter words target) */
    0x06D810, 0x000004, /* P:0x01E3: dor n0,p:$01e7 */
    0x44D800,           /* P:0x01E5: move x:(r0)+,x0 */
    0x075984,           /* P:0x01E6: movem x0,p:(r1)+ */
    0x000000,           /* P:0x01E7: nop */
    0x050C47,           /* P:0x01E8: bra p:$020f */

    /* P:0x01E9..0x01ED: Cmd 0x0C Handler (Zero-pad Output FIFO 0, 5 words) */
    0x3A0000,           /* P:0x01E9: move #$00,n2 */
    0x3C0100,           /* P:0x01EA: move #$01,n4 */
    0x3D0000,           /* P:0x01EB: move #$00,n5 */
    0x05088C,           /* P:0x01EC: bsr p:$0238 */
    0x050C42,           /* P:0x01ED: bra p:$020f */

    /* P:0x01EE..0x01F1: Cmd 0x0D Handler (Auxiliary Output FIFO write, 4 words) */
    0x3A0000,           /* P:0x01EE: move #$00,n2 */
    0x3C0000,           /* P:0x01EF: move #$00,n4 */
    0x050888,           /* P:0x01F0: bsr p:$0238 */
    0x050C1E,           /* P:0x01F1: bra p:$020f */

    /* P:0x01F2..0x01F7: Heap Manager Jump Table (B=1..6, 6 words) */
    0x000224,           /* P:0x01F2: dc $000224 (Read X) */
    0x00022E,           /* P:0x01F3: dc $00022e (Write X) */
    0x00024A,           /* P:0x01F4: dc $00024a (Read Y) */
    0x000255,           /* P:0x01F5: dc $000255 (Write Y) */
    0x000260,           /* P:0x01F6: dc $000260 (Read P) */
    0x00026B,           /* P:0x01F7: dc $00026b (Write P) */

    /* P:0x01F8..0x0213: Cmd 0x11..0x19 Dynamic Scratch Heap Manager (28 words) */
    0x01408D,           /* P:0x01F8: cmp #$00,b */
    0x05A408,           /* P:0x01F9: beq p:$000201 */
    0x21BC00,           /* P:0x01FA: move b1,n4 */
    0x64F400, 0x0001F1, /* P:0x01FB: move #$0001f1,r4 */
    0x204900,           /* P:0x01FD: move (r1)+n1,r1 */
    0x07EC96,           /* P:0x01FE: movem p:(r4+n4),r6 */
    0x0BE680,           /* P:0x01FF: jsr p:(r6) */
    0x050C0F,           /* P:0x0200: bra p:$020f */
    0x230F00,           /* P:0x0201: move n0,b */
    0x07F08E, 0x00019F, /* P:0x0202: movem p:$019f,a */
    0x07F084, 0x00019E, /* P:0x0204: movem p:$019e,x0 */
    0x200014,           /* P:0x0206: sub b,a */
    0x05940A,           /* P:0x0207: blt p:$000211 */
    0x205248,           /* P:0x0208: add x0,b (r2)-,r2 */
    0x075A84,           /* P:0x0209: movem x0,p:(r2)+ */
    0x075A98,           /* P:0x020A: movem n0,p:(r2)+ */
    0x07708C, 0x00019F, /* P:0x020B: movem a1,p:$019f */
    0x07708D, 0x00019E, /* P:0x020D: movem b1,p:$019e */
    0x200013,           /* P:0x020F: clr a (Success return) */
    0x00000C,           /* P:0x0210: rts */
    0x56F400, 0x000004, /* P:0x0211: move #$000004,a (OOM error return) */
    0x00000C,           /* P:0x0213: rts */

    /* P:0x0214..0x0223: Cmd 0x00 Handler (Init Heap & DMA Hardware, 16 words) */
    0x07F084, 0x000122, /* P:0x0214: movem p:$0122,x0 */
    0x077084, 0x00019E, /* P:0x0216: movem x0,p:$019e */
    0x07F084, 0x000123, /* P:0x0218: movem p:$0123,x0 */
    0x077084, 0x00019F, /* P:0x021A: movem x0,p:$019f */
    0x60F413, 0x000122, /* P:0x021C: clr a #$000122,r0 */
    0x061E90, 0x000002, /* P:0x021E: dor #$001e,p:$0220 */
    0x07588E,           /* P:0x0220: movem a,p:(r0)+ */
    0x0D1080, 0x0000CE, /* P:0x0221: bsr p:$0002ef */
    0x050FCC,           /* P:0x0223: bra p:$020f */

    /* P:0x0224..0x022D: Scratch -> DSP X-RAM DMA Launcher (10 words) */
    0x64F400, 0x0001A2, /* P:0x0224: move #$0001a2,r4 */
    0x220C00,           /* P:0x0226: move r0,a1 */
    0x014080,           /* P:0x0227: add #$00,a */
    0x219000,           /* P:0x0228: move a1,r0 */
    0x0BF080, 0x000276, /* P:0x0229: jsr p:$0276 */
    0x0BF080, 0x0002E1, /* P:0x022B: jsr p:$02e1 */
    0x00000C,           /* P:0x022D: rts */

    /* P:0x022E..0x0237: DSP X-RAM -> Scratch DMA Launcher (10 words) */
    0x64F400, 0x0001A2, /* P:0x022E: move #$0001a2,r4 */
    0x220C00,           /* P:0x0230: move r0,a1 */
    0x014080,           /* P:0x0231: add #$00,a */
    0x219000,           /* P:0x0232: move a1,r0 */
    0x0BF080, 0x000288, /* P:0x0233: jsr p:$0288 */
    0x0BF080, 0x0002E1, /* P:0x0235: jsr p:$02e1 */
    0x00000C,           /* P:0x0237: rts */

    /* P:0x0238..0x0249: Output FIFO DMA Launcher (18 words) */
    0x64F400, 0x0001A2, /* P:0x0238: move #$0001a2,r4 */
    0x220C00,           /* P:0x023A: move r0,a1 */
    0x014080,           /* P:0x023B: add #$00,a */
    0x219000,           /* P:0x023C: move a1,r0 */
    0x234E00,           /* P:0x023D: move n2,a */
    0x014085,           /* P:0x023E: cmp #$00,a */
    0x0D1042, 0x000006, /* P:0x023F: bne p:$000245 */
    0x0BF080, 0x00029A, /* P:0x0241: jsr p:$029a */
    0x0D10C0, 0x000004, /* P:0x0243: bra p:$000247 */
    0x0BF080, 0x0002B1, /* P:0x0245: jsr p:$02b1 */
    0x0BF080, 0x0002E1, /* P:0x0247: jsr p:$02e1 */
    0x00000C,           /* P:0x0249: rts */

    /* P:0x024A..0x0254: Scratch -> DSP Y-RAM DMA Launcher (11 words) */
    0x64F400, 0x0001A2, /* P:0x024A: move #$0001a2,r4 */
    0x220C00,           /* P:0x024C: move r0,a1 */
    0x0140C0, 0x001800, /* P:0x024D: add #$1800,a */
    0x219000,           /* P:0x024F: move a1,r0 */
    0x0BF080, 0x000276, /* P:0x0250: jsr p:$0276 */
    0x0BF080, 0x0002E1, /* P:0x0252: jsr p:$02e1 */
    0x00000C,           /* P:0x0254: rts */

    /* P:0x0255..0x025F: DSP Y-RAM -> Scratch DMA Launcher (11 words) */
    0x64F400, 0x0001A2, /* P:0x0255: move #$0001a2,r4 */
    0x220C00,           /* P:0x0257: move r0,a1 */
    0x0140C0, 0x001800, /* P:0x0258: add #$1800,a */
    0x219000,           /* P:0x025A: move a1,r0 */
    0x0BF080, 0x000288, /* P:0x025B: jsr p:$0288 */
    0x0BF080, 0x0002E1, /* P:0x025D: jsr p:$02e1 */
    0x00000C,           /* P:0x025F: rts */

    /* P:0x0260..0x026A: Scratch -> DSP P-RAM DMA Launcher (11 words) */
    0x64F400, 0x0001A2, /* P:0x0260: move #$0001a2,r4 */
    0x220C00,           /* P:0x0262: move r0,a1 */
    0x0140C0, 0x002800, /* P:0x0263: add #$2800,a */
    0x219000,           /* P:0x0265: move a1,r0 */
    0x0BF080, 0x000276, /* P:0x0266: jsr p:$0276 */
    0x0BF080, 0x0002E1, /* P:0x0268: jsr p:$02e1 */
    0x00000C,           /* P:0x026A: rts */

    /* P:0x026B..0x0275: DSP P-RAM -> Scratch DMA Launcher (11 words) */
    0x64F400, 0x0001A2, /* P:0x026B: move #$0001a2,r4 */
    0x220C00,           /* P:0x026D: move r0,a1 */
    0x0140C0, 0x002800, /* P:0x026E: add #$2800,a */
    0x219000,           /* P:0x0270: move a1,r0 */
    0x0BF080, 0x000288, /* P:0x0271: jsr p:$0288 */
    0x0BF080, 0x0002E1, /* P:0x0273: jsr p:$02e1 */
    0x00000C,           /* P:0x0275: rts */

    /* P:0x0276..0x0287: Build Scratch -> DSP Read DMA Descriptor (18 words) */
    0x0BF080, 0x0002D0, /* P:0x0276: jsr p:$02d0 */
    0x229500,           /* P:0x0278: move r4,r5 */
    0x228C00,           /* P:0x0279: move r4,a1 */
    0x0140C6, 0x003FFF, /* P:0x027A: and #$3fff,a */
    0x0140C2, 0x004000, /* P:0x027C: or #$4000,a */
    0x075D8C,           /* P:0x027E: movem a1,p:(r5)+ */
    0x54F400, 0x005BE0, /* P:0x027F: move #$005be0,a1 */
    0x075D8C,           /* P:0x0281: movem a1,p:(r5)+ */
    0x075D98,           /* P:0x0282: movem n0,p:(r5)+ */
    0x075D90,           /* P:0x0283: movem r0,p:(r5)+ */
    0x222E00,           /* P:0x0284: move r1,a */
    0x0C1E84,           /* P:0x0285: lsl #$02, a */
    0x075D8C,           /* P:0x0286: movem a1,p:(r5)+ */
    0x00000C,           /* P:0x0287: rts */

    /* P:0x0288..0x0299: Build DSP -> Scratch Write DMA Descriptor (18 words) */
    0x0BF080, 0x0002D0, /* P:0x0288: jsr p:$02d0 */
    0x229500,           /* P:0x028A: move r4,r5 */
    0x228C00,           /* P:0x028B: move r4,a1 */
    0x0140C6, 0x003FFF, /* P:0x028C: and #$3fff,a */
    0x0140C2, 0x004000, /* P:0x028E: or #$4000,a */
    0x075D8C,           /* P:0x0290: movem a1,p:(r5)+ */
    0x54F400, 0x005BE2, /* P:0x0291: move #$005be2,a1 */
    0x075D8C,           /* P:0x0293: movem a1,p:(r5)+ */
    0x075D98,           /* P:0x0294: movem n0,p:(r5)+ */
    0x075D90,           /* P:0x0295: movem r0,p:(r5)+ */
    0x222E00,           /* P:0x0296: move r1,a */
    0x0C1E84,           /* P:0x0297: lsl #$02, a */
    0x075D8C,           /* P:0x0298: movem a1,p:(r5)+ */
    0x00000C,           /* P:0x0299: rts */

    /* P:0x029A..0x02B0: Build 16-bit Non-Interleaved FIFO Descriptor (23 words) */
    0x0BF080, 0x0002D0, /* P:0x029A: jsr p:$02d0 */
    0x229500,           /* P:0x029C: move r4,r5 */
    0x228C00,           /* P:0x029D: move r4,a1 */
    0x0140C6, 0x003FFF, /* P:0x029E: and #$3fff,a */
    0x0140C2, 0x004000, /* P:0x02A0: or #$4000,a */
    0x075D8C,           /* P:0x02A2: movem a1,p:(r5)+ */
    0x54F400, 0x004602, /* P:0x02A3: move #$004602,a1 */
    0x222F00,           /* P:0x02A5: move r1,b */
    0x0C1E8B,           /* P:0x02A6: lsl #$05, b */
    0x21E500,           /* P:0x02A7: move b,x1 */
    0x200062,           /* P:0x02A8: or x1,a */
    0x075D8C,           /* P:0x02A9: movem a1,p:(r5)+ */
    0x075D98,           /* P:0x02AA: movem n0,p:(r5)+ */
    0x238E00,           /* P:0x02AB: move n4,a */
    0x0C1E9C,           /* P:0x02AC: lsl #$0e, a */
    0x220400,           /* P:0x02AD: move r0,x0 */
    0x200040,           /* P:0x02AE: add x0,a */
    0x075D8C,           /* P:0x02AF: movem a1,p:(r5)+ */
    0x00000C,           /* P:0x02B0: rts */

    /* P:0x02B1..0x02CF: Build 16-bit Interleaved Stereo FIFO Descriptor (31 words) */
    0x0BF080, 0x0002D0, /* P:0x02B1: jsr p:$02d0 */
    0x229500,           /* P:0x02B3: move r4,r5 */
    0x228C00,           /* P:0x02B4: move r4,a1 */
    0x0140C6, 0x003FFF, /* P:0x02B5: and #$3fff,a */
    0x0140C2, 0x004000, /* P:0x02B7: or #$4000,a */
    0x075D8C,           /* P:0x02B9: movem a1,p:(r5)+ */
    0x54F400, 0x000603, /* P:0x02BA: move #$000603,a1 */
    0x222F00,           /* P:0x02BC: move r1,b */
    0x0C1E8B,           /* P:0x02BD: lsl #$05, b */
    0x21E500,           /* P:0x02BE: move b,x1 */
    0x200062,           /* P:0x02BF: or x1,a */
    0x230F00,           /* P:0x02C0: move n0,b */
    0x0C1E9D,           /* P:0x02C1: lsl #$0e, b */
    0x21E500,           /* P:0x02C2: move b,x1 */
    0x200062,           /* P:0x02C3: or x1,a */
    0x075D8C,           /* P:0x02C4: movem a1,p:(r5)+ */
    0x230F00,           /* P:0x02C5: move n0,b */
    0x0C1E89,           /* P:0x02C6: lsl #$04, b */
    0x234500,           /* P:0x02C7: move n2,x1 */
    0x200068,           /* P:0x02C8: add x1,b */
    0x075D8D,           /* P:0x02C9: movem b1,p:(r5)+ */
    0x238E00,           /* P:0x02CA: move n4,a */
    0x0C1E9C,           /* P:0x02CB: lsl #$0e, a */
    0x220400,           /* P:0x02CC: move r0,x0 */
    0x200040,           /* P:0x02CD: add x0,a */
    0x075D8C,           /* P:0x02CE: movem a1,p:(r5)+ */
    0x00000C,           /* P:0x02CF: rts */

    /* P:0x02D0..0x02D4: Poll DMA Running & Clear EOL (5 words) */
    0x0A96A4, 0x0002D0, /* P:0x02D0: jset #4,x:$ffffd6,p:$02d0 */
    0x08F485, 0x000080, /* P:0x02D2: movep #$000080,x:$ffffc5 */
    0x00000C,           /* P:0x02D4: rts */

    /* P:0x02D5..0x02D9: Start DMA & Confirm Running (5 words) */
    0x08F496, 0x000001, /* P:0x02D5: movep #$000001,x:$ffffd6 */
    0x0A9684, 0x0002D7, /* P:0x02D7: jclr #4,x:$ffffd6,p:$02d7 */
    0x00000C,           /* P:0x02D9: rts */

    /* P:0x02DA..0x02E0: Poll DMA EOL & Check Error (7 words) */
    0x0A85AA, 0x0002FE, /* P:0x02DA: jset #10,x:$ffffc5,p:$02fe */
    0x0A8587, 0x0002DA, /* P:0x02DC: jclr #7,x:$ffffc5,p:$02da */
    0x08F485, 0x000080, /* P:0x02DE: movep #$000080,x:$ffffc5 */
    0x00000C,           /* P:0x02E0: rts */

    /* P:0x02E1..0x02EE: Launch DMA Block & Wait Completion (14 words) */
    0x228E00,           /* P:0x02E1: move r4,a */
    0x0140C0, 0x002800, /* P:0x02E2: add #$2800,a */
    0x08CE14,           /* P:0x02E4: movep a,x:$ffffd4 */
    0x0BF080, 0x0002D5, /* P:0x02E5: jsr p:$02d5 */
    0x20001B,           /* P:0x02E7: clr b */
    0x23AF00,           /* P:0x02E8: move n5,b */
    0x01408D,           /* P:0x02E9: cmp #$00,b */
    0x0D104A, 0x000004, /* P:0x02EA: beq p:$0002ee */
    0x0BF080, 0x0002DA, /* P:0x02EC: jsr p:$02da */
    0x00000C,           /* P:0x02EE: rts */

    /* P:0x02EF..0x02FD: Cold-Boot Peripheral & DMA Reset (15 words) */
    0x08F485, 0x000FFF, /* P:0x02EF: movep #$000fff,x:$ffffc5 (Clear HBAR) */
    0x08F484, 0x000001, /* P:0x02F1: movep #$000001,x:$ffffc4 (Set HPCR = 1) */
    0x08F48A, 0x000000, /* P:0x02F3: movep #$000000,x:$ffffca (Clear 0xFFFFCA) */
    0x44F400, 0x004000, /* P:0x02F5: move #$004000,x0 */
    0x447000, 0xFFFFD5, /* P:0x02F7: move x0,x:$ffffd5 (DMA_START_BLOCK = 0x4000) */
    0x447000, 0xFFFFD4, /* P:0x02F9: move x0,x:$ffffd4 (DMA_NEXT_BLOCK = 0x4000) */
    0x08F497, 0x000000, /* P:0x02FB: movep #$000000,x:$ffffd7 (DMA_CONFIG = 0) */
    0x00000C,           /* P:0x02FD: rts */

    /* P:0x02FE..0x02FF: Error Halt Trap & Final Word (2 words) */
    0x050C00,           /* P:0x02FE: bra p:$02fe (Halt spinloop on error) */
    0x000000            /* P:0x02FF: nop (Final alignment word) */
};
// clang-format on

#define symphony_ep_seg1 (&symphony_ep_rom[0])
#define symphony_ep_seg2 (&symphony_ep_rom[SYMPHONY_EP_SEG1_WORDS])

#endif /* HW_XBOX_MCPX_APU_DSP_SYMPHONY_EP_ROM_H */

