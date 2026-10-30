/*
 * QEMU MCPX Audio Processing Unit implementation
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2018-2019 Jannik Vogel
 * Copyright (c) 2019-2021 Matt Borgerson
 * Copyright (c) 2026 Will Bonnett
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_MCPX_APU_H
#define HW_MCPX_APU_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    XEMU_EEPROM_AUDIO_STEREO           = 0, /* 0x00000000: Standard Stereo */
    XEMU_EEPROM_AUDIO_DOLBY_SURROUND   = 1, /* 0x00000001: Dolby Surround (Lt/Rt Matrix) */
    XEMU_EEPROM_AUDIO_DOLBY_DIGITAL_51 = 2  /* 0x00010002: Surround + Enable AC-3 5.1 */
} XemuEepromAudioMode;

int xemu_eeprom_get_audio_mode(void);
bool xemu_eeprom_set_audio_mode(int mode);

struct PCIBus;
struct MemoryRegion;
void mcpx_apu_init(struct PCIBus *bus, int devfn, struct MemoryRegion *ram);

#ifdef __cplusplus
}
#endif

#endif
