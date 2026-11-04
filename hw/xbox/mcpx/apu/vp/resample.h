/* SPDX-License-Identifier: LGPL-2.0-or-later */

#ifndef HW_XBOX_MCPX_APU_VP_RESAMPLE_H
#define HW_XBOX_MCPX_APU_VP_RESAMPLE_H

#include <samplerate.h>

static inline void mcpx_apu_resampler_destroy(SRC_STATE **resampler,
                                               int *channels)
{
    if (*resampler != NULL) {
        src_delete(*resampler);
        *resampler = NULL;
    }
    *channels = 0;
}

static inline void mcpx_apu_pack_mono_samples(const float stereo[][2],
                                               float mono[], int frames)
{
    for (int i = 0; i < frames; i++) {
        mono[i] = stereo[i][0];
    }
}

static inline void mcpx_apu_expand_mono_samples(const float mono[],
                                                 float stereo[][2], int frames)
{
    for (int i = 0; i < frames; i++) {
        stereo[i][0] = mono[i];
        stereo[i][1] = mono[i];
    }
}

#endif
