/*
 * QEMU MCPX Audio Codec Interface bridge
 *
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

#ifndef HW_XBOX_MCPX_ACI_H
#define HW_XBOX_MCPX_ACI_H

#include <stdint.h>
#include <stddef.h>

void mcpx_aci_push_pcm(const void *buf, size_t bytes);
size_t mcpx_aci_read_pcm(int16_t *dest, size_t samples);

#endif

