

# **6 Core Configuration** 

## **6.1 Introduction** 

This chapter contains DSP56300 core configuration information details specific to the DSP56367. These include the following: 

- Operating modes 

- Bootstrap program 

- Interrupt sources and priorities 

- DMA request sources 

- OMR 

- PLL control register 

- AA control registers 

- JTAG BSR 

For more information on specific registers or modules in the DSP56300 core, refer to the _DSP56300 Family Manual_ (DSP56300FM). 

## **6.2 Operating Mode Register (OMR)** 

The contents of the Operating Mode Register (OMR) are shown in Table 6-1. Refer to the _DSP56300 24-Bit Digital Signal Processor Family Manual_ , Freescale Publication DSP56300FM for a description of the OMR bits. 

**Table 6-1   Operating Mode Register (OMR)** 

||SC|S|||||EO|M|||CO|M|||
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
|23<br>22|21<br>20|19|18<br>17|16|15|14<br>13|12|11<br>10|9<br>8|7|6<br>5<br>4|3<br>2|1|0|
|PEN MS|W 1: 0 SEN|WRP|EOV EUN|XYS|ATE|APD ABE|BRT|TAS<br>BE|CDP1:0|MS|SD<br>EBD|MD<br>MC|MB|MA|
|PEN|- Patch Enab|le|||ATE|- Addr|ess T|racing Enabl|e|MS|- Master me|mory Switc|h Mo|de|
|MSW1|- Memory sw|itch|mode 1||APD|- Addr|ess P|riority Disabl|e|SD|- Stop Dela|y|||
|MSW0|- Memory sw|itch|mode 0||ABE|- Asyn|. Bus|Arbitration E|nable||||||
|SEN|- Stack Exte|nsion|Enable||BRT|- Bus|Relea|se Timing||EBD|- External B|us Disable|||
|WRP|- Extended S|tack|Wrap Flag||TAS|- TA S|ynchr|onize Select||MD|- Operating|Mode D|||
|EOV|- Extended S|tack|Overflow Fla|g|BE|- Burs|t Mod|e Enable||MC|- Operating|Mode C|||
|EUN|- Extended S|tack|Underflow Fl|ag|CDP1|- Core|-Dma|Priority 1||MB|- Operating|Mode B|||
|XYS|- Stack Exte|nsion|Space Selec|t|CDP0|- Core|-Dma|Priority 0||MA|- Operating|Mode A|||



- Reserved bit. Read as zero, should be written with zero for future compatibility 

**DSP56367 24-Bit Digital Signal Processor User Manual, Rev. 2.1** 

Freescale Semiconductor 

6-1 

**Operating Mode Register (OMR)** 



### **6.2.1 Asynchronous Bus Arbitration Enable (ABE) - Bit 13** 

The asynchronous bus arbitration mode is activated by setting the ABE bit in the OMR register. Hardware reset clears the ABE bit. 

### **6.2.2 Address Attribute Priority Disable (APD) - Bit 14** 

The Address Attribute Priority Disable (APD) bit is used to turn off the address attribute priority mechanism. When this bit is set, more than one address attribute pin AA/RAS(2:0) may be simultaneously asserted according to its AAR settings. The APD bit is cleared by hardware reset. 

### **6.2.3 Address Tracing Enable (ATE) - Bit 15** 

The Address Tracing Enable (ATE) bit is used to turn on Address Tracing (AT) Mode. When the AT Mode is enabled, the DSP56300 Core reflects the addresses of internal fetches and program space moves (MOVEM) to the Address Bus (A0-A17), if the Address Bus is not needed by the DSP56300 Core for external accesses. The ATE bit is cleared on hardware reset. 

### **6.2.4 Patch Enable (PEN) - Bit 23** 

The Patch Enable function is used for patching Program ROM locations. i.e. to replace during program execution, the contents of the Program ROM. This is done by using the Instruction Cache to supply the instruction word instead of the Program ROM. 

The Patch Enable function is activated by setting bit 23 (PEN) in the OMR Register. The PEN bit is cleared by hardware reset. 

The Instruction Cache should be initialized with the new instructions according to the following procedure: 

These steps should be executed from external memory or by download via host interface: 

1. Set Cache Enable = 1 

2. Set Patch Enable = 1 

3. Initialize TAGs to different values by unlock eight different external sectors 

4. Lock the PATCH sector(s) 

5. Move new code to locked sector(s), to the addresses that should be replaced 

6. Start regular PROM program 

**DSP56367 24-Bit Digital Signal Processor User Manual, Rev. 2.1** 

Freescale Semiconductor 

6-2 

**Operating Mode Register (OMR)** 



- ;**************************************************************************** ; PATCH initialization example 

- ;**************************************************************************** 

page    132,55,0,0,0 nolist INCLUDE "ioequ.asm" INCLUDE "intequ.asm" list START equ $100 ; main program starting address PATCH_OFSET equ 128 ; patch offset M_PAE equ 23 ; Patch Enable M_PROMS equ $ffafec ; ROM area Start M_PROME equ $ffafff ; ROM area End org P:START move #M_PROMS,r0 bset #M_CE,sr ; CacheEnable = 1 bset #M_PAE,omr ; PatchEnable = 1 move #$800000,r1 ; any external address move #128,n1 ; 128 for 1K ICACHE, sector size move #(M_PROMS+PATCH_OFSET),r2 dup 8 punlock (r1)+n1 ; initialize TAGs to different ; values endm plock (r2) ; lock patch's sector ; (start/mid/end) move #PATCH_DATA_START,r1 ; ; replace ROM code by PATCH ; do #(PATCH_DATA_END-PATCH_DATA_START+1),PATCH_LOOP movem p:(r1)+,x0 movem x0,p:(r2)+ nop ; Do-loop restriction PATCH_LOOP jsr #M_PROMS ; start ROM code execution ENDTEST jmp ENDTEST nop nop nop nop 

##### **DSP56367 24-Bit Digital Signal Processor User Manual, Rev. 2.1** 

Freescale Semiconductor 

6-3 



##### **Operating Modes** 

; ; patch data 

; 

PATCH_DATA_START 

move #5,m0 move #6,m1 move #7,m2 

PATCH_DATA_END 

;**************************************************************************** 

## **6.3 Operating Modes** 

The operating modes are defined as shown in Table 6-2. The operating modes are latched from MODA, MODB, MODC and MODD pins during reset. Each operating mode is briefly described below. Except for modes 0 and 8, the operation of all other modes is defined by the Bootstrap ROM source code in Appendix A, "Bootstrap ROM Contents". 

**Table 6-2   DSP56367 Operating Modes** 

|**Mode**|**MOD**<br>**D**|**MOD**<br>**C**|**MOD**<br>**B**|**MOD**<br>**A**|**Reset**<br>**Vector**|**Description**|
|---|---|---|---|---|---|---|
|0|0|0|0|0|$C00000|Expanded mode|
|1|0|0|0|1|$FF0000|Bootstrap from byte-wide memory|
|2|0|0|1|0|$FF0000|Jump to PROM starting address|
|3|0|0|1|1|$FF0000|_Reserved_|
|4|0|1|0|0|$FF0000|_Reserved_|
|5|0|1|0|1|$FF0000|Bootstrap from SHI (slave SPI mode)|
|6|0|1|1|0|$FF0000|Bootstrap from SHI (slave I<sup>2</sup>C mode)(HCKFR=1, 100ns filter enabled)|
|7|0|1|1|1|$FF0000|Bootstrap from SHI (slave I<sup>2</sup>C mode)(HCKR=0)|
|8|1|0|0|0|$008000|Expanded mode|
|9|1|0|0|1|$FF0000|_Reserved_for Burn-in testing|
|A|1|0|1|0|$FF0000|_Reserved_|
|B|1|0|1|1|$FF0000|_Reserved_|
|C|1|1|0|0|$FF0000|HDI08 Bootstrap in ISA Mode|
|D|1|1|0|1|$FF0000|HDI08 Bootstrap in HC11 non-multiplexed mode|
|E|1|1|1|0|$FF0000|HDI08 Bootstrap in 8051 multiplexed bus mode|
|F|1|1|1|1|$FF0000|HDI08 Bootstrap in 68302 bus mode|



**DSP56367 24-Bit Digital Signal Processor User Manual, Rev. 2.1** 

Freescale Semiconductor 

6-4 

**Operating Modes** 



#### **Table 6-3   DSP56367 Mode Descriptions** 

- **Mode 0** The DSP starts fetching instructions beginning at address $C00000. Memory accesses are performed using SRAM memory access type with 31 wait states and no address attributes selected. Address $C00000 is reflected as address $00000 on Port A pins A0-A17. 

- **Mode 1** The bootstrap program loads instructions through Port A from external byte-wide memory, connected to the least significant byte of the data bus (bits 7-0), and starting at address P:$D00000. The bootstrap code expects to read 3 bytes specifying the number of program words, 3 bytes specifying the address to start loading the program words and then 3 bytes for each program word to be loaded. The number of words, the starting address and the program words are read least significant byte first followed by the mid and then by the most significant byte. The program words will be stored in contiguous PRAM memory locations starting at the specified starting address. After reading the program words, program execution starts from the same address where loading started.The SRAM memory access type is selected by the values in Address Attribute Register 1 (AAR1), with 31 wait states for each memory access. Address $D00000 is reflected as address $00000 on Port A pins A0-A17. 

- **Mode 2** The DSP starts fetching instructions from the starting address of the on-chip Program ROM. 

- **Mode 3** _Reserved_ . 

- **Mode 4** _Reserved_ . 

- **Mode 5** In this mode, the internal PRAM is loaded from the Serial Host Interface (SHI). The SHI operates in the SPI slave mode, with 24-bit word width.The bootstrap code expects to read a 24-bit word specifying the number of program words, a 24-bit word specifying the address to start loading the program words and then a 24-bit word for each program word to be loaded. The program words will be stored in contiguous PRAM memory locations starting at the specified starting address. After reading the program words, program execution starts from the same address where loading started. 

- **Mode 6** Same as Mode 5 except SHI interface operates in the I<sup>2</sup> C slave mode with HCKFR set to 1 and the 100ns filter enabled. 

- **Mode 7** Same as Mode 5 except SHI interface operates in the I<sup>2</sup> C slave mode with HCKFR set to 0. 

- **Mode 8** The DSP starts fetching instructions beginning at address $008000. Memory accesses are performed using SRAM memory access type with 31 wait states and no address attributes selected. 

- **Mode 9** _Reserved_ . Used for Burn-In testing. 

- **Mode A** _Reserved_ . 

- **Mode B** _Reserved_ . 

- **Mode C** Instructions are loaded through the HDI08, which is configured to interface with an ISA bus. The HOST ISA bootstrap code expects to read a 24-bit word specifying the number of program words, a 24-bit word specifying the address to start loading the program words and then a 24-bit word for each program word to be loaded. The program words will be stored in contiguous PRAM memory locations starting at the specified starting address. After reading the program words, program execution starts from the same address where loading started. The Host Interface bootstrap load program may be stopped by setting the Host Flag 0 (HF0). This will start execution of the loaded program from the specified starting address. 

- **Mode D** As in Mode C, but HDI08 is set for interfacing to Freescale HC11 microcontroller in non-multiplexed mode 

- **Mode E** As in Mode C, but HDI08 is set for interfacing to Intel 8051 multiplexed bus 

**Mode F** As in Mode C, but HDI08 is set for interfacing to Freescale 68302 bus. 

**DSP56367 24-Bit Digital Signal Processor User Manual, Rev. 2.1** 

Freescale Semiconductor 

6-5 



##### **Interrupt Priority Registers** 

## **6.4 Interrupt Priority Registers** 

There are two interrupt priority registers in the DSP56367: 

1. IPR-C is dedicated for DSP56300 Core interrupt sources. 

2. IPR-P is dedicated for DSP56367 peripheral interrupt sources. 

The interrupt priority registers are shown in Figure 6-1 and Figure 6-2. The Interrupt Priority Level bits are defined in Table 6-4. The interrupt vectors are shown in Table 6-6 and the interrupt priorities are shown in Table 6-5. 

**Table 6-4   Interrupt Priority Level Bits** 



<!-- Start of picture text -->
IPL bits Interrupt<br>Interrupts<br>Priority<br>Enabled<br>xxL1 xxL0 Level<br>0 0 No —<br>0 1 Yes 0<br>1 0 Yes 1<br>1 1 Yes 2<br>11 10 9 8 7 6 5 4 3 2 1 0<br>ESL11 ESL10 TAL1 TAL0 DAL1 DAL0 HDL1 HDL0 SHL1 SHL0 ESL1 ESL0<br>ESAI IPL<br>SHI IPL<br>HDI08 IPL<br>DAX IPL<br>TRIPLE TIMER IPL<br>ESAI_1 IPL<br>23 22 21 20 19 18 17 16 15 14 13 12<br>reserved<br>Reserved bit. Read as zero, should be written with zero for future compatibility.<br><!-- End of picture text -->

**Figure 6-1  Interrupt Priority Register P** 

**DSP56367 24-Bit Digital Signal Processor User Manual, Rev. 2.1** 

Freescale Semiconductor 

6-6 

**Interrupt Priority Registers** 





<!-- Start of picture text -->
11 10 9 8 7 6 5 4 3 2 1 0<br>IDL2 IDL1 IDL0 ICL2 ICL1 ICL0 IBL2 IBL1 IBL0 IAL2 IAL1 IAL0<br>IRQA IPL<br>IRQA mode<br>IRQB IPL<br>IRQB mode<br>IRQC IPL<br>IRQC mode<br>IRQD IPL<br>IRQD mode<br>23 22 21 20 19 18 17 16 15 14 13 12<br>D5L1 D5L0 D4L1 D4L0 D3L1 D3L0 D2L1 D2L0 D1L1 D1L0 D0L1 D0L0<br>DMA0 IPL<br>DMA1 IPL<br>DMA2 IPL<br>DMA3 IPL<br>DMA4 IPL<br>DMA5 IPL<br><!-- End of picture text -->

**Figure 6-2  Interrupt Priority Register C** 

**Table 6-5   Interrupt Sources Priorities Within an IPL** 



<!-- Start of picture text -->
Priority Interrupt Source<br>Level 3 (Nonmaskable)<br>Highest Hardware RESET<br>Stack Error<br>Illegal Instruction<br>Debug Request Interrupt<br>Trap<br>Lowest Non-Maskable Interrupt<br>Levels 0, 1, 2 (Maskable)<br>Highest IRQA (External Interrupt)<br>IRQB (External Interrupt)<br>IRQC (External Interrupt)<br>IRQD (External Interrupt)<br>DMA Channel 0 Interrupt<br>DMA Channel 1 Interrupt<br>DMA Channel 2 Interrupt<br><!-- End of picture text -->

**DSP56367 24-Bit Digital Signal Processor User Manual, Rev. 2.1** 

Freescale Semiconductor 

6-7 

**Interrupt Priority Registers** 



**Table 6-5   Interrupt Sources Priorities Within an IPL (continued)** 

|**Priority**|**Interrupt Source**|
|---|---|
||DMA Channel 3 Interrupt|
||DMA Channel 4 Interrupt|
||DMA Channel 5 Interrupt|
||ESAI Receive Data with Exception Status|
||ESAI Receive Even Data|
||ESAI Receive Data|
||ESAI Receive Last Slot|
||ESAI Transmit Data with Exception Status|
||ESAI Transmit Last Slot|
||ESAI Transmit Even Data|
||ESAI Transmit Data|
||SHI Bus Error|
||SHI Receive Overrun Error|
||SHI Transmit Underrun Error|
||SHI Receive FIFO Full|
||SHI Transmit Data|
||SHI Receive FIFO Not Empty|
||HOST Command Interrupt|
||HOST Receive Data Interrupt|
||HOST Transmit Data Interrupt|
||DAX Transmit Underrun Error|
||DAX Block Transferred|
||DAX Transmit Register Empty|
||TIMER0 Overflow Interrupt|
||TIMER0 Compare Interrupt|
||TIMER1 Overflow Interrupt|
||TIMER1 Compare Interrupt|
||TIMER2 Overflow Interrupt|
||TIMER2 Compare Interrupt|
||ESAI_1 Receive Data with Exception Status|



**DSP56367 24-Bit Digital Signal Processor User Manual, Rev. 2.1** 

Freescale Semiconductor 

6-8 

**Interrupt Priority Registers** 



**Table 6-5   Interrupt Sources Priorities Within an IPL (continued)** 

|**Priority**|**Interrupt Source**|
|---|---|
||ESAI_1 Receive Even Data|
||ESAI_1 Receive Data|
||ESAI_1 Receive Last Slot|
||ESAI_1 Transmit Data with Exception Status|
||ESAI_1 Transmit Last Slot|
||ESAI_1 Transmit Even Data|
|Lowest|ESAI_1 Transmit Data|



**Table 6-6   DSP56367 Interrupt Vectors** 

|**Interrupt**<br>**Starting Address**|**Interrupt**<br>**Priority**<br>**Level**<br>**Range**|**Interrupt Source**|
|---|---|---|
|VBA:$00|3|Hardware RESET|
|VBA:$02|3|Stack Error|
|VBA:$04|3|Illegal Instruction|
|VBA:$06|3|Debug Request Interrupt|
|VBA:$08|3|Trap|
|VBA:$0A|3|Non-Maskable Interrupt (NMI<br>)|
|VBA:$0C|3|Reserved For Future Level-3 Interrupt Source|
|VBA:$0E|3|Reserved For Future Level-3 Interrupt Source|
|VBA:$10|0 - 2|IRQA|
|VBA:$12|0 - 2|IRQB|
|VBA:$14|0 - 2|IRQC|
|VBA:$16|0 - 2|IRQD|
|VBA:$18|0 - 2|DMA Channel 0|
|VBA:$1A|0 - 2|DMA Channel 1|
|VBA:$1C|0 - 2|DMA Channel 2|
|VBA:$1E|0 - 2|DMA Channel 3|
|VBA:$20|0 - 2|DMA Channel 4|
|VBA:$22|0 - 2|DMA Channel 5|
|VBA:$24|0 - 2|_Reserved_|
|VBA:$26|0 - 2|_Reserved_|
|VBA:$28|0 - 2|DAX Underrun Error|



**DSP56367 24-Bit Digital Signal Processor User Manual, Rev. 2.1** 

Freescale Semiconductor 

6-9 

**Interrupt Priority Registers** 



**Table 6-6   DSP56367 Interrupt Vectors (continued)** 

|**Interrupt**<br>**Starting Address**|**Interrupt**<br>**Priority**<br>**Level**<br>**Range**|**Interrupt Source**|
|---|---|---|
|VBA:$2A|0 - 2|DAX Block Transferred|
|VBA:$2C|0 - 2|_Reserved_|
|VBA:$2E|0 - 2|DAX Audio Data Empty|
|VBA:$30|0 - 2|ESAI Receive Data|
|VBA:$32|0 - 2|ESAI Receive Even Data|
|VBA:$34|0 - 2|ESAI Receive Data With Exception Status|
|VBA:$36|0 - 2|ESAI Receive Last Slot|
|VBA:$38|0 - 2|ESAI Transmit Data|
|VBA:$3A|0 - 2|ESAI Transmit Even Data|
|VBA:$3C|0 - 2|ESAI Transmit Data with Exception Status|
|VBA:$3E|0 - 2|ESAI Transmit Last Slot|
|VBA:$40|0 - 2|SHI Transmit Data|
|VBA:$42|0 - 2|SHI Transmit Underrun Error|
|VBA:$44|0 - 2|SHI Receive FIFO Not Empty|
|VBA:$46|0 - 2|_Reserved_|
|VBA:$48|0 - 2|SHI Receive FIFO Full|
|VBA:$4A|0 - 2|SHI Receive Overrun Error|
|VBA:$4C|0 - 2|SHI Bus Error|
|VBA:$4E|0 - 2|_Reserved_|
|VBA:$50|0 - 2|_Reserved_|
|VBA:$52|0 - 2|_Reserved_|
|VBA:$54|0 - 2|TIMER0 Compare|
|VBA:$56|0 - 2|TIMER0 Overflow|
|VBA:$58|0 - 2|TIMER1 Compare|
|VBA:$5A|0 - 2|TIMER1 Overflow|
|VBA:$5C|0 - 2|TIMER2 Compare|
|VBA:$5E|0 - 2|TIMER2 Overflow|
|VBA:$60|0 - 2|Host Receive Data Full|
|VBA:$62|0 - 2|Host Transmit Data Empty|
|VBA:$64|0 - 2|Host Command (Default)|
|VBA:$66|0 - 2|_Reserved_|
|VBA:$68|0 - 2|_Reserved_|



**DSP56367 24-Bit Digital Signal Processor User Manual, Rev. 2.1** 

Freescale Semiconductor 

6-10 

**DMA Request Sources** 



**Table 6-6   DSP56367 Interrupt Vectors (continued)** 

|**Interrupt**<br>**Starting Address**|**Interrupt**<br>**Priority**<br>**Level**<br>**Range**|**Interrupt Source**|
|---|---|---|
|VBA:$6A|0 - 2|_Reserved_|
|VBA:$6C|0 - 2|_Reserved_|
|VBA:$6E|0 - 2|_Reserved_|
|VBA:$70|0 - 2|ESAI_1 Receive Data|
|VBA:$72|0 - 2|ESAI_1 Receive Even Data|
|VBA:$74|0 - 2|ESAI_1 Receive Data With Exception Status|
|VBA:$76|0 - 2|ESAI_1 Receive Last Slot|
|VBA:$78|0 - 2|ESAI_1 Transmit Data|
|VBA:$7A|0 - 2|ESAI_1 Transmit Even Data|
|VBA:$7C|0 - 2|ESAI_1 Transmit Data with Exception Status|
|VBA:$7E|0 - 2|ESAI_1 Transmit Last Slot|
|VBA:$80|0 - 2|_Reserved_|
|:|:|:|
|VBA:$FE|0 - 2|_Reserved_|



## **6.5 DMA Request Sources** 

The DMA Request Source bits (DRS0-DRS4 bits in the DMA Control/Status registers) encode the source of DMA requests used to trigger the DMA transfers. The DMA request sources may be the internal peripherals or external devices requesting service through the IRQA, IRQB, IRQC and IRQD pins. The DMA Request Sources are shown in Table 6-7. 

**Table 6-7   DMA Request Sources** 

|**DMA Request Source Bits**<br>**DRS4...DRS0**|**Requesting Device**|
|---|---|
|00000|External (IRQA<br>pin)|
|00001|External (IRQB<br>pin)|
|00010|External (IRQC<br>pin)|
|00011|External (IRQD<br>pin)|
|00100|Transfer Done from DMA channel 0|
|00101|Transfer Done from DMA channel 1|
|00110|Transfer Done from DMA channel 2|



**DSP56367 24-Bit Digital Signal Processor User Manual, Rev. 2.1** 

Freescale Semiconductor 

6-11 

**PLL Initialization** 



**Table 6-7   DMA Request Sources (continued)** 

|**DMA Request Source Bits**<br>**DRS4...DRS0**|**Requesting Device**|
|---|---|
|00111|Transfer Done from DMA channel 3|
|01000|Transfer Done from DMA channel 4|
|01001|Transfer Done from DMA channel 5|
|01010|DAX Transmit Data|
|01011|ESAI Receive Data (RDF=1)|
|01100|ESAI Transmit Data (TDE=1)|
|01101|SHI HTX Empty|
|01110|SHI FIFO Not Empty|
|01111|SHI FIFO Full|
|10000|HDI08 Receive Data|
|10001|HDI08 Transmit Data|
|10010|TIMER0 (TCF=1)|
|10011|TIMER1 (TCF=1)|
|10100|TIMER2 (TCF=1)|
|10101|ESAI_1 Receive Data (RDF=1)|
|10110|ESAI_1 Transmit Data (TDE=1)|
|10111-11111|_Reserved_|



## **6.6 PLL Initialization** 

### **6.6.1 PLL Multiplication Factor (MF0-MF11)** 

The DSP56367 PLL multiplication factor is set to 6 during hardware reset, i.e. the Multiplication Factor Bits MF0-MF11 in the PLL Control Register (PCTL) are set to $005. 

### **6.6.2 PLL Pre-Divider Factor (PD0-PD3)** 

The DSP56367 PLL Pre-Divider factor is set to 1 during hardware reset, i.e. the Pre-Divider Factor Bits PD0-PD3 in the PLL Control Register (PCTL) are set to $0. 

**DSP56367 24-Bit Digital Signal Processor User Manual, Rev. 2.1** 

Freescale Semiconductor 

6-12 

**Device Identification (ID) Register** 



### **6.6.3 Crystal Range Bit (XTLR)** 

The Crystal Range (XTLR) bit controls the on-chip crystal oscillator transconductance. The on-chip crystal oscillator is not used on the DSP56367 since no XTAL pin is available. The XTLR bit is set to zero during hardware reset in the DSP56367. 

### **6.6.4 XTAL Disable Bit (XTLD)** 

The XTAL Disable Bit (XTLD) is set to 1 (XTAL disabled) during hardware reset in the DSP56367. 

## **6.7 Device Identification (ID) Register** 

The Device Identification Register (IDR) is a 24 bit read only factory programmed register used to identify the different DSP56300 core-based family members. This register specifies the derivative number and revision number. This information may be used in testing or by software. Table 6-8 shows the ID register configuration. 

**Table 6-8   Identification Register Configuration** 

|23|16|15<br>12|11<br>0|
|---|---|---|---|
||**Reserved**|**Revision Number**|**Derivative Number**|
||$00|$0|$367|



## **6.8 JTAG Identification (ID) Register** 

The JTAG Identification (ID) Register is a 32-bit, read-only thought JTAG, factory programmed register used to distinguish the component on a board according to the IEEE 1149.1 standard. Table 6-9 shows the JTAG ID register configuration. 

**Table 6-9   JTAG Identification Register Configuration** 

|31|28|27<br>22<br>|21<br>12|11<br>1|0|
|---|---|---|---|---|---|
||**Version**<br>**Information**|**Customer Part**<br>**Number**|**Sequence**<br>**Number**|**Manufacturer**<br>**Identity**|**1**|
||0000|000111|0001010010|00000001110|1|



## **6.9 JTAG Boundary Scan Register (BSR)** 

The boundary scan register (BSR) in the DSP56367 JTAG implementation contains bits for all device signal and clock pins and associated control signals. All bidirectional pins have a single register bit in the boundary scan register for pin data, and are controlled by an associated control bit in the boundary scan register. The boundary scan register bit definitions are described in Table 6-10. 

**DSP56367 24-Bit Digital Signal Processor User Manual, Rev. 2.1** 

Freescale Semiconductor 

6-13 

**JTAG Boundary Scan Register (BSR)** 



**Table 6-10   DSP56367 BSR Bit Definition** 

|**Bit**<br>**#**|**Pin Name**|**Pin Type**|**BSR Cell**<br>**Type**|**Bit**<br>**#**|**Pin Name**|**Pin Type**|**BSR Cell**<br>**Type**|
|---|---|---|---|---|---|---|---|
|0|SDO4_1/SDI1_1|—|Control|76|FST_1|—|Control|
|1|SDO4_1/SDI1_1|Input/Output|Data|77|FST_1|Input/Output|Data|
|2|IRQA|Input|Data|78|SDO5_1/SDI0_1|—|Control|
|3|IRQB|Input|Data|79|SDO5_1/SDI0_1|Input/Output|Data|
|4|IRQC|Input|Data|80|RES|Input|Data|
|5|IRQD|Input|Data|81|HAD0|—|Control|
|6|D23|Input/Output|Data|82|HAD0|Input/Output|Data|
|7|D22|Input/Output|Data|83|HAD1|—|Control|
|8|D21|Input/Output|Data|84|HAD1|Input/Output|Data|
|9|D20|Input/Output|Data|85|HAD2|—|Control|
|10|D19|Input/Output|Data|86|HAD2|Input/Output|Data|
|11|D18|Input/Output|Data|87|HAD3|—|Control|
|12|D17|Input/Output|Data|88|HAD3|Input/Output|Data|
|13|D16|Input/Output|Data|89|HAD4|—|Control|
|14|D15|Input/Output|Data|90|HAD4|Input/Output|Data|
|15|D[23:13]|—|Control|91|HAD5|—|Control|
|16|D14|Input/Output|Data|92|HAD5|Input/Output|Data|
|17|D13|Input/Output|Data|93|HAD6|—|Control|
|18|D12|Input/Output|Data|94|HAD6|Input/Output|Data|
|19|D11|Input/Output|Data|95|HAD7|—|Control|
|20|D10|Input/Output|Data|96|HAD7|Input/Output|Data|
|21|D9|Input/Output|Data|97|HAS/A0|—|Control|
|22|D8|Input/Output|Data|98|HAS/A0|Input/Output|Data|
|23|D7|Input/Output|Data|99|HA8/A1|—|Control|
|24|D6|Input/Output|Data|100|HA8/A1|Input/Output|Data|
|25|D5|Input/Output|Data|101|HA9/A2|—|Control|



**DSP56367 24-Bit Digital Signal Processor User Manual, Rev. 2.1** 

Freescale Semiconductor 

6-14 

**JTAG Boundary Scan Register (BSR)** 



**Table 6-10   DSP56367 BSR Bit Definition (continued)** 

|**Bit**<br>**#**|**Pin Name**|**Pin Type**|**BSR Cell**<br>**Type**|**Bit**<br>**#**|**Pin Name**|**Pin Type**|**BSR Cell**<br>**Type**|
|---|---|---|---|---|---|---|---|
|26|D4|Input/Output|Data|102|HA9/A2|Input/Output|Data|
|27|D3|Input/Output|Data|103|HCS/A10|—|Control|
|28|D[12:0]|—|Control|104|HCS/A10|Input/Output|Data|
|29|D2|Input/Output|Data|105|TIO0|—|Control|
|30|D1|Input/Output|Data|106|TIO0|Input/Output|Data|
|31|D0|Input/Output|Data|107|ACI|—|Control|
|32|A17|Output3|Data|108|ACI|Input/Output|Data|
|33|A16|Output3|Data|109|ADO|—|Control|
|34|A15|Output3|Data|110|ADO|Input/Output|Data|
|35|A[17:9]|—|Control|111|HREQ/HTRQ|—|Control|
|36|A14|Output3|Data|112|HREQ/HTRQ|Input/Output|Data|
|37|A13|Output3|Data|113|HACK/RRQ|—|Control|
|38|A12|Output3|Data|114|HACK/RRQ|Input/Output|Data|
|39|A11|Output3|Data|115|HRW/RD|—|Control|
|40|A10|Output3|Data|116|HRW/RD|Input/Output|Data|
|41|A9|Output3|Data|117|HDS/WR|—|Control|
|42|A8|Output3|Data|118|HDS/WR|Input/Output|Data|
|43|A7|Output3|Data|119|HSCKR|—|Control|
|44|A6|Output3|Data|120|HSCKR|Input/Output|Data|
|45|A[8:0]|—|Control|121|HSCKT|—|Control|
|46|A5|Output3|Data|122|HSCKT|Input/Output|Data|
|47|A4|Output3|Data|123|SCKR|—|Control|
|48|A3|Output3|Data|124|SCKR|Input/Output|Data|
|49|A2|Output3|Data|125|SCKT|—|Control|
|50|A1|Output3|Data|126|SCKT|Input/Output|Data|
|51|A0|Output3|Data|127|FSR|—|Control|



**DSP56367 24-Bit Digital Signal Processor User Manual, Rev. 2.1** 

Freescale Semiconductor 

6-15 

**JTAG Boundary Scan Register (BSR)** 



**Table 6-10   DSP56367 BSR Bit Definition (continued)** 

|**Bit**<br>**#**|**Pin Name**|**Pin Type**|**BSR Cell**<br>**Type**|**Bit**<br>**#**|**Pin Name**|**Pin Type**|**BSR Cell**<br>**Type**|
|---|---|---|---|---|---|---|---|
|52|BG|Input|Data|128|FSR|Input/Output|Data|
|53|AA0|—|Control|129|FST|—|Control|
|54|AA0|Output3|Data|130|FST|Input/Output|Data|
|55|AA1|—|Control|131|SDO5/SDI0|—|Control|
|56|AA1|Output3|Data|132|SDO5/SDI0|Input/Output|Data|
|57|RD|Output3|Data|133|SDO4/SDI1|—|Control|
|58|WR|Output3|Data|134|SDO4/SDI1|Input/Output|Data|
|59|BB|—|Control|135|SDO3/SDI2|—|Control|
|60|BB|Input/Output|Data|136|SDO3/SDI2|Input/Output|Data|
|61|BR|Output2|Data|137|SDO2/SDI3|—|Control|
|62|TA|Input|Data|138|SDO2/SDI3|Input/Output|Data|
|63|PINIT|Input|Data|139|SDO1|—|Control|
|64|SCKR_1|—|Control|140|SDO1|Input/Output|Data|
|65|SCKR_1|Input/Output|Data|141|SDO0|—|Control|
|66|FSR_1|—|Control|142|SDO0|Input/Output|Data|
|67|FSR_1|Input/Output|Data|143|HREQ|—|Control|
|68|RD<br>,WR|—|Control|144|HREQ|Input/Output|Data|
|69|EXTAL|Input|Data|145|SS|Input|Data|
|70|SCKT_1|—|Control|146|SCK/SCL|—|Control|
|71|SCKT_1|Input/Output|Data|147|SCK/SCL|Input/Output|Data|
|72|CAS|—|Control|148|MISO/SDA|—|Control|
|73|CAS|Output3|Data|149|MISO/SDA|Input/Output|Data|
|74|AA2|—|Control|150|MOSI/HA0|—|Control|
|75|AA2|Output3|Data|151|MOSI/HA0|Input/Output|Data|



**DSP56367 24-Bit Digital Signal Processor User Manual, Rev. 2.1** 

Freescale Semiconductor 

6-16 

