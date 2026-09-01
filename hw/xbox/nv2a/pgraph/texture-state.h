/*
 * NV2A texture state helpers
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_TEXTURE_STATE_H
#define HW_XBOX_NV2A_PGRAPH_TEXTURE_STATE_H

#include <stdbool.h>

static inline void pgraph_texture_stage_invalidate(bool *dirty,
                                                   unsigned int stage)
{
    dirty[stage] = true;
}

#endif
