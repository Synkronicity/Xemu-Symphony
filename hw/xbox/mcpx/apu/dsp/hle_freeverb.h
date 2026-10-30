/*
 * Xemu Symphony - MCPX APU High-Level Emulation (HLE) Freeverb Reverb Engine
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

#ifndef HW_XBOX_MCPX_APU_DSP_HLE_FREEVERB_H
#define HW_XBOX_MCPX_APU_DSP_HLE_FREEVERB_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <math.h>
#include <string.h>

#define FREEVERB_NUM_COMBS       8
#define FREEVERB_NUM_ALLPASSES   4
#define FREEVERB_STEREO_SPREAD   25
#define FREEVERB_MAX_COMB_LEN    2048
#define FREEVERB_MAX_AP_LEN      768
#define FREEVERB_FIXED_GAIN      0.015f
#define FREEVERB_SCALE_WET       3.0f

#define FREEVERB_ER_BUF_SIZE     1024
#define FREEVERB_ER_BUF_MASK     (FREEVERB_ER_BUF_SIZE - 1)

/* Delay lengths calibrated for native 48,000 Hz APU subframes */
static const int hle_freeverb_comb_lengths_L[FREEVERB_NUM_COMBS] = {
    1215, 1293, 1390, 1476, 1548, 1623, 1695, 1760
};

static const int hle_freeverb_allpass_lengths_L[FREEVERB_NUM_ALLPASSES] = {
    605, 480, 371, 245
};

static inline float hle_undenormalize(float v)
{
    return (fabsf(v) < 1.0e-15f) ? 0.0f : v;
}

typedef struct {
    float buf[FREEVERB_MAX_COMB_LEN];
    int len;
    int pos;
    float filterstore;
} HleCombFilter;

typedef struct {
    float buf[FREEVERB_MAX_AP_LEN];
    int len;
    int pos;
} HleAllpassFilter;

typedef struct {
    HleCombFilter combs_L[FREEVERB_NUM_COMBS];
    HleCombFilter combs_R[FREEVERB_NUM_COMBS];
    HleAllpassFilter allpasses_L[FREEVERB_NUM_ALLPASSES];
    HleAllpassFilter allpasses_R[FREEVERB_NUM_ALLPASSES];

    float roomsize;
    float damp;
    float wet;
    float dry;
    float width;
    float scaled_wet;

    float feedback;
    float damp1;
    float damp2;
    float wet1;
    float wet2;

    float er_buf_L[FREEVERB_ER_BUF_SIZE];
    float er_buf_R[FREEVERB_ER_BUF_SIZE];
    int er_pos;

    bool initialized;
} HleFreeverbState;

static inline void hle_freeverb_reset(HleFreeverbState *fv)
{
    memset(fv->er_buf_L, 0, sizeof(fv->er_buf_L));
    memset(fv->er_buf_R, 0, sizeof(fv->er_buf_R));
    fv->er_pos = 0;

    for (int i = 0; i < FREEVERB_NUM_COMBS; i++) {
        memset(fv->combs_L[i].buf, 0, sizeof(fv->combs_L[i].buf));
        fv->combs_L[i].pos = 0;
        fv->combs_L[i].filterstore = 0.0f;

        memset(fv->combs_R[i].buf, 0, sizeof(fv->combs_R[i].buf));
        fv->combs_R[i].pos = 0;
        fv->combs_R[i].filterstore = 0.0f;
    }

    for (int i = 0; i < FREEVERB_NUM_ALLPASSES; i++) {
        memset(fv->allpasses_L[i].buf, 0, sizeof(fv->allpasses_L[i].buf));
        fv->allpasses_L[i].pos = 0;

        memset(fv->allpasses_R[i].buf, 0, sizeof(fv->allpasses_R[i].buf));
        fv->allpasses_R[i].pos = 0;
    }
}

static inline void hle_freeverb_set_params(HleFreeverbState *fv, float roomsize,
                                           float damp, float wet, float width)
{
    fv->roomsize = roomsize;
    fv->damp = damp;
    fv->wet = wet;
    fv->width = width;

    /* Scale wet gain */
    float scaled_wet = wet * FREEVERB_SCALE_WET;
    fv->scaled_wet = scaled_wet;

    /* Scale roomsize to feedback [0.70f, 0.98f] */
    float fb = (roomsize * 0.28f) + 0.70f;
    if (fb < 0.70f) {
        fb = 0.70f;
    } else if (fb > 0.98f) {
        fb = 0.98f;
    }
    fv->feedback = fb;

    /* Scale damping */
    fv->damp1 = damp * 0.40f;
    fv->damp2 = 1.0f - fv->damp1;

    /* Stereo width matrix */
    fv->wet1 = scaled_wet * (width * 0.5f + 0.5f);
    fv->wet2 = scaled_wet * ((1.0f - width) * 0.5f);
}

static inline void hle_freeverb_init(HleFreeverbState *fv)
{
    memset(fv, 0, sizeof(*fv));

    for (int i = 0; i < FREEVERB_NUM_COMBS; i++) {
        fv->combs_L[i].len = hle_freeverb_comb_lengths_L[i];
        fv->combs_R[i].len = hle_freeverb_comb_lengths_L[i] + FREEVERB_STEREO_SPREAD;
    }

    for (int i = 0; i < FREEVERB_NUM_ALLPASSES; i++) {
        fv->allpasses_L[i].len = hle_freeverb_allpass_lengths_L[i];
        fv->allpasses_R[i].len = hle_freeverb_allpass_lengths_L[i] + FREEVERB_STEREO_SPREAD;
    }

    hle_freeverb_reset(fv);
    hle_freeverb_set_params(fv, 0.50f, 0.50f, 0.25f, 1.0f);
    fv->initialized = true;
}

static inline float hle_comb_step(HleCombFilter *c, float input, float feedback,
                                  float damp1, float damp2)
{
    float output = c->buf[c->pos];
    output = hle_undenormalize(output);

    c->filterstore = hle_undenormalize((output * damp2) + (c->filterstore * damp1));
    c->buf[c->pos] = hle_undenormalize(input + (c->filterstore * feedback));

    if (++c->pos >= c->len) {
        c->pos = 0;
    }

    return output;
}

static inline float hle_allpass_step(HleAllpassFilter *a, float input)
{
    float bufout = a->buf[a->pos];
    bufout = hle_undenormalize(bufout);

    float output = -input + bufout;
    a->buf[a->pos] = hle_undenormalize(input + (bufout * 0.5f));

    if (++a->pos >= a->len) {
        a->pos = 0;
    }

    return output;
}

static inline void hle_freeverb_process_subframe(HleFreeverbState *fv,
                                                 const float mixbins[32][32],
                                                 float wet_fl[32],
                                                 float wet_fr[32],
                                                 float wet_rl[32],
                                                 float wet_rr[32])
{
    if (!fv->initialized) {
        hle_freeverb_init(fv);
    }

    for (int i = 0; i < 32; i++) {
        /* 1. Primary Xbox DirectSound I3DL2 Environmental Reverb Send (Mixbin 10) at unity gain */
        float i3dl2 = mixbins[10][i];

        /* 2. Secondary 3D/FX Send Mixbins (6..9 and 11..30) */
        float fx_l = mixbins[6][i] + mixbins[8][i];
        float fx_r = mixbins[7][i] + mixbins[9][i];
        for (int b = 11; b < 30; b += 2) {
            fx_l += mixbins[b][i];
            fx_r += mixbins[b + 1][i];
        }
        if (fx_r == 0.0f && fx_l != 0.0f) {
            fx_r = fx_l;
        } else if (fx_l == 0.0f && fx_r != 0.0f) {
            fx_l = fx_r;
        }

        /* 3. Subtle ambient room bleed (0.22f) from main 5.1 dry channels (0..5) */
        float dry_bleed_l = 0.0f * (mixbins[0][i] + 0.5f * mixbins[2][i] + 0.7f * mixbins[4][i]);
        float dry_bleed_r = 0.0f * (mixbins[1][i] + 0.5f * mixbins[2][i] + 0.7f * mixbins[5][i]);

        float rev_in_l = i3dl2 + (0.35f * fx_l) + dry_bleed_l;
        float rev_in_r = i3dl2 + (0.35f * fx_r) + dry_bleed_r;

        /* Write to 4-tap I3DL2 Early Reflections delay line */
        fv->er_buf_L[fv->er_pos] = rev_in_l * 0.20f;
        fv->er_buf_R[fv->er_pos] = rev_in_r * 0.20f;

        /* 4-Tap I3DL2 Early Reflections (calibrated for 48 kHz):
         * Tap 0: 3.5 ms = 168 samples, gain 0.42f (ipsilateral)
         * Tap 1: 7.8 ms = 374 samples, gain 0.30f (contralateral cross-reflection)
         * Tap 2: 12.4 ms = 595 samples, gain 0.24f (ipsilateral)
         * Tap 3: 18.5 ms = 888 samples, gain 0.18f (rear surround emphasis)
         */
        int tap0_idx = (fv->er_pos - 168) & FREEVERB_ER_BUF_MASK;
        int tap1_idx = (fv->er_pos - 374) & FREEVERB_ER_BUF_MASK;
        int tap2_idx = (fv->er_pos - 595) & FREEVERB_ER_BUF_MASK;
        int tap3_idx = (fv->er_pos - 888) & FREEVERB_ER_BUF_MASK;

        fv->er_pos = (fv->er_pos + 1) & FREEVERB_ER_BUF_MASK;

        float er_l = (fv->er_buf_L[tap0_idx] * 0.42f) +
                     (fv->er_buf_R[tap1_idx] * 0.30f) +
                     (fv->er_buf_L[tap2_idx] * 0.24f);

        float er_r = (fv->er_buf_R[tap0_idx] * 0.42f) +
                     (fv->er_buf_L[tap1_idx] * 0.30f) +
                     (fv->er_buf_R[tap2_idx] * 0.24f);

        float er_rear_l = (fv->er_buf_L[tap3_idx] * 0.18f) + (fv->er_buf_R[tap1_idx] * 0.12f);
        float er_rear_r = (fv->er_buf_R[tap3_idx] * 0.18f) + (fv->er_buf_L[tap1_idx] * 0.12f);

        /* Late reverberation (Schroeder-Moorer parallel combs + series all-passes) */
        float in_l = rev_in_l * FREEVERB_FIXED_GAIN;
        float in_r = rev_in_r * FREEVERB_FIXED_GAIN;

        /* Accumulate 8 parallel comb filters per channel */
        float out_l = 0.0f;
        float out_r = 0.0f;

        for (int k = 0; k < FREEVERB_NUM_COMBS; k++) {
            out_l += hle_comb_step(&fv->combs_L[k], in_l, fv->feedback,
                                   fv->damp1, fv->damp2);
            out_r += hle_comb_step(&fv->combs_R[k], in_r, fv->feedback,
                                   fv->damp1, fv->damp2);
        }

        /* Feed through 4 series all-pass filters per channel */
        for (int k = 0; k < FREEVERB_NUM_ALLPASSES; k++) {
            out_l = hle_allpass_step(&fv->allpasses_L[k], out_l);
            out_r = hle_allpass_step(&fv->allpasses_R[k], out_r);
        }

        /* Blend early reflections (scaled by wet) and late reverb tail */
        wet_fl[i] = (out_l * fv->wet1) + (out_r * fv->wet2) + (er_l * fv->wet);
        wet_fr[i] = (out_r * fv->wet1) + (out_l * fv->wet2) + (er_r * fv->wet);

        /* Decorrelated rear surround reverb with rear early reflections */
        wet_rl[i] = (out_l * fv->scaled_wet) * 0.7071f + (er_rear_l * fv->wet);
        wet_rr[i] = (out_r * fv->scaled_wet) * 0.7071f + (er_rear_r * fv->wet);
    }
}

#endif /* HW_XBOX_MCPX_APU_DSP_HLE_FREEVERB_H */

