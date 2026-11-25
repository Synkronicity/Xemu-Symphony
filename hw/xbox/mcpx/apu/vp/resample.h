/* SPDX-License-Identifier: LGPL-2.0-or-later */

#ifndef HW_XBOX_MCPX_APU_VP_RESAMPLE_H
#define HW_XBOX_MCPX_APU_VP_RESAMPLE_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MCPX_RESAMPLER_INPUT_FRAMES 64

typedef long (*mcpx_resample_callback_t)(void *cb_data, float **data);

/*
 * Precomputed 8-point Lagrange interpolation tables (257 phases u = r / 256
 * quantized to 16-bit fractional precision, stored as base low[r][k] and
 * linear delta step[r][k] = rows[r + 1][k] - rows[r][k] for r = 0..255).
 */
typedef struct MCPXLagrangeTables {
    float low[256][8];
    float step[256][8];
    bool initialized;
} MCPXLagrangeTables;

typedef struct MCPXVoiceResampler {
    mcpx_resample_callback_t cb;
    void *cb_data;
    int channels;
    bool primed;
    double phase;
    int valid_ahead;
    float history[2][8];
    float input_buf[MCPX_RESAMPLER_INPUT_FRAMES * 2];
    int input_pos;
    int input_len;
} MCPXVoiceResampler;

static inline MCPXLagrangeTables *mcpx_lagrange_tables_storage(void)
{
    static MCPXLagrangeTables tables;
    return &tables;
}

static inline void lagrange_tables_init(void)
{
    MCPXLagrangeTables *t = mcpx_lagrange_tables_storage();
    if (t->initialized) {
        return;
    }

    float rows[257][8];
    for (int r = 0; r <= 256; r++) {
        double u = (double)r / 256.0;
        for (int k = -3; k <= 4; k++) {
            double w = 1.0;
            for (int j = -3; j <= 4; j++) {
                if (j != k) {
                    w *= (u - (double)j) / (double)(k - j);
                }
            }
            rows[r][k + 3] = (float)(round(w * 65536.0) / 65536.0);
        }
    }

    for (int r = 0; r < 256; r++) {
        for (int k = 0; k < 8; k++) {
            t->low[r][k] = rows[r][k];
            t->step[r][k] = rows[r + 1][k] - rows[r][k];
        }
    }

    t->initialized = true;
}

static inline void mcpx_voice_resampler_reset(MCPXVoiceResampler *r)
{
    memset(r->history, 0, sizeof(r->history));
    r->phase = 0.0;
    r->valid_ahead = 0;
    r->input_pos = 0;
    r->input_len = 0;
    r->primed = false;
}

static inline void mcpx_voice_resampler_init(MCPXVoiceResampler *r,
                                             mcpx_resample_callback_t cb,
                                             void *cb_data, int channels)
{
    r->cb = cb;
    r->cb_data = cb_data;
    r->channels = channels;
    mcpx_voice_resampler_reset(r);
}

static inline void mcpx_voice_resampler_destroy(MCPXVoiceResampler *r)
{
    mcpx_voice_resampler_reset(r);
    r->cb = NULL;
    r->cb_data = NULL;
    r->channels = 0;
}

static inline bool mcpx_voice_resampler_refill(MCPXVoiceResampler *r,
                                               bool *cb_exhausted)
{
    if (r->input_pos < r->input_len) {
        return true;
    }
    if (*cb_exhausted || !r->cb) {
        return false;
    }

    float *src = NULL;
    long n = r->cb(r->cb_data, &src);
    if (n <= 0 || !src) {
        *cb_exhausted = true;
        r->input_pos = 0;
        r->input_len = 0;
        return false;
    }

    int frames = (n > MCPX_RESAMPLER_INPUT_FRAMES)
                     ? MCPX_RESAMPLER_INPUT_FRAMES
                     : (int)n;
    memcpy(r->input_buf, src,
           (size_t)frames * (size_t)r->channels * sizeof(float));
    r->input_pos = 0;
    r->input_len = frames;
    return true;
}

static inline void mcpx_voice_resampler_top_up(MCPXVoiceResampler *r,
                                               bool *cb_exhausted)
{
    if (!r->primed) {
        memset(r->history, 0, sizeof(r->history));
        r->phase = 0.0;
        r->valid_ahead = 0;
        r->primed = true;
    }

    if (r->channels == 1) {
        while (r->valid_ahead < 5) {
            if (!mcpx_voice_resampler_refill(r, cb_exhausted)) {
                break;
            }
            r->history[0][3 + r->valid_ahead] = r->input_buf[r->input_pos++];
            r->valid_ahead++;
        }
    } else {
        while (r->valid_ahead < 5) {
            if (!mcpx_voice_resampler_refill(r, cb_exhausted)) {
                break;
            }
            int idx = r->input_pos * 2;
            r->history[0][3 + r->valid_ahead] = r->input_buf[idx];
            r->history[1][3 + r->valid_ahead] = r->input_buf[idx + 1];
            r->input_pos++;
            r->valid_ahead++;
        }
    }
}

static inline void mcpx_voice_resampler_advance_mono(MCPXVoiceResampler *r,
                                                     int adv,
                                                     bool *cb_exhausted)
{
    float *h = r->history[0];
    for (int a = 0; a < adv; a++) {
        if (r->valid_ahead == 0 && (adv - a) >= 3) {
            memset(h, 0, sizeof(r->history[0]));
            break;
        }
        for (int k = 0; k < 7; k++) {
            h[k] = h[k + 1];
        }
        if (r->valid_ahead == 5 &&
            mcpx_voice_resampler_refill(r, cb_exhausted)) {
            h[7] = r->input_buf[r->input_pos++];
        } else {
            h[7] = 0.0f;
            if (r->valid_ahead > 0) {
                r->valid_ahead--;
            }
        }
    }
}

static inline void mcpx_voice_resampler_advance_stereo(MCPXVoiceResampler *r,
                                                       int adv,
                                                       bool *cb_exhausted)
{
    float *h0 = r->history[0];
    float *h1 = r->history[1];
    for (int a = 0; a < adv; a++) {
        if (r->valid_ahead == 0 && (adv - a) >= 3) {
            memset(r->history, 0, sizeof(r->history));
            break;
        }
        for (int k = 0; k < 7; k++) {
            h0[k] = h0[k + 1];
            h1[k] = h1[k + 1];
        }
        if (r->valid_ahead == 5 &&
            mcpx_voice_resampler_refill(r, cb_exhausted)) {
            int idx = r->input_pos * 2;
            h0[7] = r->input_buf[idx];
            h1[7] = r->input_buf[idx + 1];
            r->input_pos++;
        } else {
            h0[7] = 0.0f;
            h1[7] = 0.0f;
            if (r->valid_ahead > 0) {
                r->valid_ahead--;
            }
        }
    }
}

/*
 * Resample up to requested_num frames using hardware 8-point Lagrange
 * interpolation. src_ratio follows libsamplerate's output_rate / input_rate
 * convention, so source cursor step per output frame is 1.0 / src_ratio.
 */
static inline int mcpx_voice_resampler_process(MCPXVoiceResampler *r,
                                               double src_ratio,
                                               int requested_num,
                                               float out[][2])
{
    if (requested_num <= 0 || !out || (r->channels != 1 && r->channels != 2) ||
        !(src_ratio > 0.0) || !isfinite(src_ratio)) {
        return 0;
    }

    MCPXLagrangeTables *tables = mcpx_lagrange_tables_storage();
    if (!tables->initialized) {
        lagrange_tables_init();
    }

    double step = 1.0 / src_ratio;
    if (!isfinite(step) || step <= 0.0) {
        return 0;
    }
    if (step > 4096.0) {
        step = 4096.0;
    }

    bool cb_exhausted = false;
    mcpx_voice_resampler_top_up(r, &cb_exhausted);

    int produced = 0;
    if (r->channels == 1) {
        const float *h = r->history[0];
        while (produced < requested_num && r->valid_ahead > 0) {
            float frac = (float)r->phase;
            float y;
            if (frac == 0.0f) {
                y = h[3];
            } else {
                float scaled = frac * 256.0f;
                int row = (int)scaled;
                if (row < 0) {
                    row = 0;
                } else if (row >= 256) {
                    row = 255;
                }
                float a = scaled - (float)row;
                const float *low = tables->low[row];
                const float *tstep = tables->step[row];
                y = (low[0] + a * tstep[0]) * h[0]
                  + (low[1] + a * tstep[1]) * h[1]
                  + (low[2] + a * tstep[2]) * h[2]
                  + (low[3] + a * tstep[3]) * h[3]
                  + (low[4] + a * tstep[4]) * h[4]
                  + (low[5] + a * tstep[5]) * h[5]
                  + (low[6] + a * tstep[6]) * h[6]
                  + (low[7] + a * tstep[7]) * h[7];
            }
            out[produced][0] = y;
            out[produced][1] = y;
            produced++;

            r->phase += step;
            int adv = (int)r->phase;
            r->phase -= (double)adv;
            if (r->phase < 0.0) {
                r->phase = 0.0;
            } else if (r->phase >= 1.0) {
                int extra = (int)r->phase;
                adv += extra;
                r->phase -= (double)extra;
            }
            if (adv > 0) {
                mcpx_voice_resampler_advance_mono(r, adv, &cb_exhausted);
            }
        }
    } else {
        const float *h0 = r->history[0];
        const float *h1 = r->history[1];
        while (produced < requested_num && r->valid_ahead > 0) {
            float frac = (float)r->phase;
            float y0, y1;
            if (frac == 0.0f) {
                y0 = h0[3];
                y1 = h1[3];
            } else {
                float scaled = frac * 256.0f;
                int row = (int)scaled;
                if (row < 0) {
                    row = 0;
                } else if (row >= 256) {
                    row = 255;
                }
                float a = scaled - (float)row;
                const float *low = tables->low[row];
                const float *tstep = tables->step[row];
                float w0 = low[0] + a * tstep[0];
                float w1 = low[1] + a * tstep[1];
                float w2 = low[2] + a * tstep[2];
                float w3 = low[3] + a * tstep[3];
                float w4 = low[4] + a * tstep[4];
                float w5 = low[5] + a * tstep[5];
                float w6 = low[6] + a * tstep[6];
                float w7 = low[7] + a * tstep[7];
                y0 = w0 * h0[0] + w1 * h0[1] + w2 * h0[2] + w3 * h0[3]
                   + w4 * h0[4] + w5 * h0[5] + w6 * h0[6] + w7 * h0[7];
                y1 = w0 * h1[0] + w1 * h1[1] + w2 * h1[2] + w3 * h1[3]
                   + w4 * h1[4] + w5 * h1[5] + w6 * h1[6] + w7 * h1[7];
            }
            out[produced][0] = y0;
            out[produced][1] = y1;
            produced++;

            r->phase += step;
            int adv = (int)r->phase;
            r->phase -= (double)adv;
            if (r->phase < 0.0) {
                r->phase = 0.0;
            } else if (r->phase >= 1.0) {
                int extra = (int)r->phase;
                adv += extra;
                r->phase -= (double)extra;
            }
            if (adv > 0) {
                mcpx_voice_resampler_advance_stereo(r, adv, &cb_exhausted);
            }
        }
    }

    return produced;
}

static inline void mcpx_apu_pack_mono_samples(const float stereo[][2],
                                              float mono[], int frames)
{
    for (int i = 0; i < frames; i++) {
        mono[i] = stereo[i][0];
    }
}

static inline void mcpx_apu_float_to_short_array(const float *in,
                                                 int16_t *out, int len)
{
    for (int i = 0; i < len; i++) {
        float v = in[i];
        if (isnan(v)) {
            out[i] = 0;
        } else if (v >= 1.0f) {
            out[i] = 32767;
        } else if (v <= -1.0f) {
            out[i] = -32768;
        } else {
            long s = lrintf(v * 32767.0f);
            out[i] = (s > 32767) ? 32767 : ((s < -32768) ? -32768 : (int16_t)s);
        }
    }
}

#endif
