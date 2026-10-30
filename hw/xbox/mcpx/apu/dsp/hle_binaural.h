/*
 * Xemu Symphony - MCPX APU 5.1-to-2.0 Binaural HRTF & Pure Stereo Downmixer
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

#ifndef HW_XBOX_MCPX_APU_DSP_HLE_BINAURAL_H
#define HW_XBOX_MCPX_APU_DSP_HLE_BINAURAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <math.h>
#include <string.h>

#define BINAURAL_MAX_DELAY  64
#define BINAURAL_DELAY_MASK (BINAURAL_MAX_DELAY - 1)

/* Biquad rear pinna notch filter coefficients (48 kHz, f0 = 6500 Hz, Q = 2.0, -6 dB) */
#define BINAURAL_PINNA_B0   0.89535072f
#define BINAURAL_PINNA_B1  -1.04203206f
#define BINAURAL_PINNA_B2   0.68505224f
#define BINAURAL_PINNA_A1  -1.04203206f
#define BINAURAL_PINNA_A2   0.58040296f

static inline float hle_binaural_undenormalize(float v)
{
    return (fabsf(v) < 1.0e-15f) ? 0.0f : v;
}

static inline int16_t hle_float_to_s16_clamp(float v)
{
    if (v >= 1.0f) {
        return 32767;
    } else if (v <= -1.0f) {
        return -32768;
    }
    return (int16_t)lrintf(v * 32767.0f);
}

typedef struct {
    float buf[BINAURAL_MAX_DELAY];
    int write_pos;
} BinauralDelayLine;

typedef struct {
    float state;
} BinauralOnePole;

typedef struct {
    float s1;
    float s2;
} BinauralBiquad;

typedef struct {
    BinauralDelayLine delay_fl;
    BinauralDelayLine delay_fr;
    BinauralDelayLine delay_rl;
    BinauralDelayLine delay_rr;

    BinauralOnePole shadow_fl_r;
    BinauralOnePole shadow_fr_l;
    BinauralOnePole shadow_rl_r;
    BinauralOnePole shadow_rr_l;
    BinauralOnePole lfe_lpf;

    BinauralBiquad rl_pinna;
    BinauralBiquad rr_pinna;

    BinauralBiquad ltrt_ap_rl;
    BinauralBiquad ltrt_ap_rr;

    bool initialized;
} HleBinauralState;

static inline void binaural_delay_write(BinauralDelayLine *d, float sample)
{
    d->buf[d->write_pos] = hle_binaural_undenormalize(sample);
    d->write_pos = (d->write_pos + 1) & BINAURAL_DELAY_MASK;
}

static inline float binaural_delay_read(const BinauralDelayLine *d, int delay_samples)
{
    int read_pos = (d->write_pos - 1 - delay_samples) & BINAURAL_DELAY_MASK;
    return d->buf[read_pos];
}

static inline float binaural_onepole_step(BinauralOnePole *p, float in, float alpha)
{
    p->state += alpha * (in - p->state);
    p->state = hle_binaural_undenormalize(p->state);
    return p->state;
}

static inline float binaural_biquad_step(BinauralBiquad *bq, float in)
{
    float out = (in * BINAURAL_PINNA_B0) + bq->s1;
    bq->s1 = (in * BINAURAL_PINNA_B1) - (BINAURAL_PINNA_A1 * out) + bq->s2;
    bq->s2 = (in * BINAURAL_PINNA_B2) - (BINAURAL_PINNA_A2 * out);
    bq->s1 = hle_binaural_undenormalize(bq->s1);
    bq->s2 = hle_binaural_undenormalize(bq->s2);
    return out;
}

static inline float ltrt_allpass_step(BinauralBiquad *ap, float in)
{
    // 2nd-order all-pass filter at 1 kHz (48 kHz sample rate, 90-degree phase shift)
    const float b0 =  0.8309f;
    const float b1 = -1.8152f;
    const float b2 =  1.0000f;
    const float a1 = -1.8152f;
    const float a2 =  0.8309f;

    float out = (in * b0) + ap->s1;
    ap->s1 = (in * b1) - (a1 * out) + ap->s2;
    ap->s2 = (in * b2) - (a2 * out);
    ap->s1 = hle_binaural_undenormalize(ap->s1);
    ap->s2 = hle_binaural_undenormalize(ap->s2);
    return out;
}

static inline void hle_binaural_reset(HleBinauralState *b)
{
    memset(b->delay_fl.buf, 0, sizeof(b->delay_fl.buf));
    b->delay_fl.write_pos = 0;

    memset(b->delay_fr.buf, 0, sizeof(b->delay_fr.buf));
    b->delay_fr.write_pos = 0;

    memset(b->delay_rl.buf, 0, sizeof(b->delay_rl.buf));
    b->delay_rl.write_pos = 0;

    memset(b->delay_rr.buf, 0, sizeof(b->delay_rr.buf));
    b->delay_rr.write_pos = 0;

    b->shadow_fl_r.state = 0.0f;
    b->shadow_fr_l.state = 0.0f;
    b->shadow_rl_r.state = 0.0f;
    b->shadow_rr_l.state = 0.0f;
    b->lfe_lpf.state = 0.0f;

    b->rl_pinna.s1 = 0.0f;
    b->rl_pinna.s2 = 0.0f;
    b->rr_pinna.s1 = 0.0f;
    b->rr_pinna.s2 = 0.0f;

    b->ltrt_ap_rl.s1 = 0.0f;
    b->ltrt_ap_rl.s2 = 0.0f;
    b->ltrt_ap_rr.s1 = 0.0f;
    b->ltrt_ap_rr.s2 = 0.0f;
}

static inline void hle_binaural_init(HleBinauralState *b)
{
    memset(b, 0, sizeof(*b));
    hle_binaural_reset(b);
    b->initialized = true;
}

static inline void hle_binaural_downmix_subframe(HleBinauralState *b,
                                                 const float in_6ch[32][6],
                                                 int16_t out_stereo[32][2])
{
    if (!b->initialized) {
        hle_binaural_init(b);
    }

    for (int i = 0; i < 32; i++) {
        float fl  = in_6ch[i][0];
        float fr  = in_6ch[i][1];
        float fc  = in_6ch[i][2];
        float lfe = in_6ch[i][3];
        float rl  = in_6ch[i][4];
        float rr  = in_6ch[i][5];

        /* 1. Front Center (FC, 0°): Equal Left and Right (-3 dB) */
        float fc_out = fc * 0.7071f;

        /* 2. Front Left (FL, -30°) and Front Right (FR, +30°) */
        binaural_delay_write(&b->delay_fl, fl);
        binaural_delay_write(&b->delay_fr, fr);

        /* Ipsilateral (FL -> L, FR -> R): Delay 0, gain 0.82f */
        float fl_ipsi = fl * 0.82f;
        float fr_ipsi = fr * 0.82f;

        /* Contralateral (FL -> R, FR -> L): ITD delay 13 samples (~270 us), LPF fc ~2200 Hz (alpha = 0.25f), gain 0.48f */
        float fl_del = binaural_delay_read(&b->delay_fl, 13);
        float fr_del = binaural_delay_read(&b->delay_fr, 13);
        float fl_contra = binaural_onepole_step(&b->shadow_fl_r, fl_del, 0.25f) * 0.48f;
        float fr_contra = binaural_onepole_step(&b->shadow_fr_l, fr_del, 0.25f) * 0.48f;

        /* 3. Rear Left (RL, -110°) and Rear Right (RR, +110°) */
        /* Pre-filter through 6.5 kHz pinna notch biquad to introduce anatomical rear cues */
        float rl_notch = binaural_biquad_step(&b->rl_pinna, rl);
        float rr_notch = binaural_biquad_step(&b->rr_pinna, rr);

        binaural_delay_write(&b->delay_rl, rl_notch);
        binaural_delay_write(&b->delay_rr, rr_notch);

        /* Ipsilateral (RL -> L, RR -> R): Delay 2 samples, gain 0.72f */
        float rl_ipsi = binaural_delay_read(&b->delay_rl, 2) * 0.72f;
        float rr_ipsi = binaural_delay_read(&b->delay_rr, 2) * 0.72f;

        /* Contralateral (RL -> R, RR -> L): ITD delay 35 samples (~729 us), heavy LPF fc ~1200 Hz (alpha = 0.145f), gain 0.34f */
        float rl_del = binaural_delay_read(&b->delay_rl, 35);
        float rr_del = binaural_delay_read(&b->delay_rr, 35);
        float rl_contra = binaural_onepole_step(&b->shadow_rl_r, rl_del, 0.145f) * 0.34f;
        float rr_contra = binaural_onepole_step(&b->shadow_rr_l, rr_del, 0.145f) * 0.34f;

        /* 4. LFE (Subwoofer): 1-pole LPF at 120 Hz (alpha = 0.0155f), gain 0.45f */
        float lfe_out = binaural_onepole_step(&b->lfe_lpf, lfe, 0.0155f) * 0.45f;

        /* 5. Master Stereo Summation */
        float out_l = fl_ipsi + fr_contra + fc_out + rl_ipsi + rr_contra + lfe_out;
        float out_r = fr_ipsi + fl_contra + fc_out + rr_ipsi + rl_contra + lfe_out;

        out_stereo[i][0] = hle_float_to_s16_clamp(out_l);
        out_stereo[i][1] = hle_float_to_s16_clamp(out_r);
    }
}

static inline void hle_prologic_ltrt_downmix_subframe(HleBinauralState *b,
                                                      const float in_6ch[32][6],
                                                      int16_t out_stereo[32][2])
{
    if (!b->initialized) {
        hle_binaural_init(b);
    }

    for (int i = 0; i < 32; i++) {
        float fl  = in_6ch[i][0];
        float fr  = in_6ch[i][1];
        float fc  = in_6ch[i][2];
        float lfe = in_6ch[i][3];
        float rl  = in_6ch[i][4];
        float rr  = in_6ch[i][5];

        /* Center and low-passed LFE summed in-phase at 0.7071f (-3 dB) */
        float lfe_filtered = binaural_onepole_step(&b->lfe_lpf, lfe, 0.0155f);
        float mono_center = (fc + lfe_filtered) * 0.7071f;

        /* Surround channels passed through 90-degree all-pass filter */
        float surr_l = ltrt_allpass_step(&b->ltrt_ap_rl, rl);
        float surr_r = ltrt_allpass_step(&b->ltrt_ap_rr, rr);
        float surr = (surr_l + surr_r) * 0.7071f;

        /* Dolby Surround Pro Logic: Lt = FL + 0.7071*C - 0.7071*Surround
         *                          Rt = FR + 0.7071*C + 0.7071*Surround
         */
        float lt = fl + mono_center - surr;
        float rt = fr + mono_center + surr;

        out_stereo[i][0] = hle_float_to_s16_clamp(lt);
        out_stereo[i][1] = hle_float_to_s16_clamp(rt);
    }
}

static inline void hle_stereo_pure_downmix_subframe(const float in_6ch[32][6],
                                                    int16_t out_stereo[32][2])
{
    for (int i = 0; i < 32; i++) {
        float fl = in_6ch[i][0];
        float fr = in_6ch[i][1];
        float fc = in_6ch[i][2] * 0.7071f;
        float rl = in_6ch[i][4] * 0.7071f;
        float rr = in_6ch[i][5] * 0.7071f;

        float l = fl + fc + rl;
        float r = fr + fc + rr;

        out_stereo[i][0] = hle_float_to_s16_clamp(l);
        out_stereo[i][1] = hle_float_to_s16_clamp(r);
    }
}

#endif /* HW_XBOX_MCPX_APU_DSP_HLE_BINAURAL_H */

