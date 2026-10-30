/*
 * QEMU MCPX Audio Processing Unit implementation
 *
 * Copyright (c) 2019-2025 Matt Borgerson
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

#include "apu_int.h"
#include "hw/xbox/mcpx/aci.h"
#include "ui/xemu-settings.h"

static int s_active_sdl_channels = 0;

/*
 * Recalculate 32-bit one's-complement checksum over Xbox EEPROM data blocks.
 * Calibrated to exact algorithm in hw/xbox/eeprom_generation.c.
 */
static uint32_t xbox_eeprom_calc_checksum(const uint8_t *data, size_t len)
{
    uint32_t high = 0;
    uint32_t low = 0;
    for (size_t i = 0; i < len / 4; i++) {
        uint32_t val;
        memcpy(&val, data + i * 4, sizeof(uint32_t));
        val = le32_to_cpu(val);
        uint64_t sum = ((uint64_t)high << 32) | low;

        high = (sum + val) >> 32;
        low += val;
    }
    return ~(high + low);
}

int xemu_eeprom_get_audio_mode(void)
{
    const char *eeprom_path = g_config.sys.files.eeprom_path;
    if (!eeprom_path || !eeprom_path[0]) {
        eeprom_path = xemu_settings_get_default_eeprom_path();
    }
    if (!eeprom_path || !eeprom_path[0]) {
        return XEMU_EEPROM_AUDIO_STEREO;
    }

    FILE *fp = fopen(eeprom_path, "rb");
    if (!fp) {
        return XEMU_EEPROM_AUDIO_STEREO;
    }

    uint32_t user_block[2] = {0};
    if (fseek(fp, 0x98, SEEK_SET) != 0 ||
        fread(user_block, sizeof(uint32_t), 2, fp) != 2) {
        fclose(fp);
        return XEMU_EEPROM_AUDIO_STEREO;
    }
    fclose(fp);

    uint32_t audio_flags = le32_to_cpu(user_block[0]);
    uint32_t fallback_flags = le32_to_cpu(user_block[1]);

    uint32_t flags = audio_flags;
    if (flags == 0 && fallback_flags != 0) {
        flags = fallback_flags;
    }

    if ((flags & 0x00010000) || (flags & 0x00000002)) {
        return XEMU_EEPROM_AUDIO_DOLBY_DIGITAL_51;
    } else if (flags & 0x00000001) {
        return XEMU_EEPROM_AUDIO_DOLBY_SURROUND;
    }

    return XEMU_EEPROM_AUDIO_STEREO;
}

bool xemu_eeprom_set_audio_mode(int mode)
{
    const char *eeprom_path = g_config.sys.files.eeprom_path;
    if (!eeprom_path || !eeprom_path[0]) {
        eeprom_path = xemu_settings_get_default_eeprom_path();
    }
    if (!eeprom_path || !eeprom_path[0]) {
        fprintf(stderr, "[APU MONITOR] Error: No EEPROM path configured\n");
        return false;
    }

    FILE *fp = fopen(eeprom_path, "rb+");
    if (!fp) {
        fprintf(stderr, "[APU MONITOR] Error: Failed to open EEPROM at '%s' for writing\n",
                eeprom_path);
        return false;
    }

    uint32_t eeprom_words[64];
    uint8_t *eeprom = (uint8_t *)eeprom_words;
    if (fread(eeprom, 1, 256, fp) != 256) {
        fprintf(stderr, "[APU MONITOR] Error: EEPROM file '%s' is not 256 bytes\n",
                eeprom_path);
        fclose(fp);
        return false;
    }

    uint32_t audio_flags = 0;
    switch (mode) {
    case XEMU_EEPROM_AUDIO_DOLBY_SURROUND:
        audio_flags = 0x00000001;
        break;
    case XEMU_EEPROM_AUDIO_DOLBY_DIGITAL_51:
        audio_flags = 0x00010002;
        break;
    case XEMU_EEPROM_AUDIO_STEREO:
    default:
        audio_flags = 0x00000000;
        break;
    }

    /* Write 32-bit little-endian audio flags at offset 0x98 and clear 0x9C */
    uint32_t le_audio_flags = cpu_to_le32(audio_flags);
    uint32_t zero_word = 0;
    memcpy(eeprom + 0x98, &le_audio_flags, sizeof(uint32_t));
    memcpy(eeprom + 0x9C, &zero_word, sizeof(uint32_t));

    /* Recalculate 32-bit User Configuration Section checksum (0x64..0xBF, 92 bytes / 0x5C) */
    uint32_t new_checksum = xbox_eeprom_calc_checksum(eeprom + 0x64, 0x5C);
    uint32_t le_checksum = cpu_to_le32(new_checksum);
    memcpy(eeprom + 0x60, &le_checksum, sizeof(uint32_t));

    if (fseek(fp, 0, SEEK_SET) != 0 ||
        fwrite(eeprom, 1, 256, fp) != 256) {
        fprintf(stderr, "[APU MONITOR] Error: Failed writing updated EEPROM to '%s'\n",
                eeprom_path);
        fclose(fp);
        return false;
    }

    fflush(fp);
    fclose(fp);

    fprintf(stderr,
            "[APU MONITOR] Updated EEPROM (%s) audio flags to 0x%08X (checksum=0x%08X)\n",
            eeprom_path, audio_flags, new_checksum);
    return true;
}

static inline int16_t clamp16(int32_t val)
{
    if (val > 32767) {
        return 32767;
    }
    if (val < -32768) {
        return -32768;
    }
    return (int16_t)val;
}

void mcpx_apu_monitor_init(MCPXAPUState *d, Error **errp)
{
    bool eeprom_wants_surround = false;
    uint32_t audio_flags = 0;
    const char *eeprom_path = g_config.sys.files.eeprom_path;
    if (!eeprom_path || !eeprom_path[0]) {
        eeprom_path = xemu_settings_get_default_eeprom_path();
    }
    if (eeprom_path) {
        FILE *fp = fopen(eeprom_path, "rb");
        if (fp) {
            uint32_t user_block[4] = {0};
            if (fseek(fp, 0x90, SEEK_SET) == 0 &&
                fread(user_block, sizeof(uint32_t), 4, fp) == 4) {
                user_block[0] = le32_to_cpu(user_block[0]);
                user_block[1] = le32_to_cpu(user_block[1]);
                user_block[2] = le32_to_cpu(user_block[2]);
                user_block[3] = le32_to_cpu(user_block[3]);

                fprintf(stderr,
                        "[APU MONITOR] EEPROM Config Block: [0x90]=0x%08X [0x94]=0x%08X [0x98]=0x%08X [0x9C]=0x%08X\n",
                        user_block[0], user_block[1], user_block[2], user_block[3]);

                audio_flags = user_block[2];
                if ((audio_flags & 0x00010000) || (audio_flags & 0x00000002) || (audio_flags & 0x00000001)) {
                    eeprom_wants_surround = true;
                } else if ((user_block[3] & 0x00010000) || (user_block[3] & 0x00000002) || (user_block[3] & 0x00000001)) {
                    audio_flags = user_block[3];
                    eeprom_wants_surround = true;
                }
            }
            fclose(fp);
        }
    }

    const char *env_surround = getenv("XEMU_SURROUND");
    bool env_wants_surround = env_surround &&
                              strcmp(env_surround, "0") != 0 &&
                              strcasecmp(env_surround, "false") != 0;
    bool surround_requested = eeprom_wants_surround || env_wants_surround;

    fprintf(stderr,
            "[APU MONITOR] EEPROM audio flags: 0x%08X (Surround requested: %s)\n",
            audio_flags, surround_requested ? "yes" : "no");

    /* Firmware-gated surround activation: verify EP P-RAM is populated */
    uint32_t reset_vec = dsp_read_memory(d->ep.dsp, 'P', 0x0000) & 0x00FFFFFF;
    if (reset_vec == 0 || reset_vec == 0x00CACACA) {
        dsp_bootstrap_ep_firmware(d->ep.dsp);
        reset_vec = dsp_read_memory(d->ep.dsp, 'P', 0x0000) & 0x00FFFFFF;
    }

    bool fw_present = (reset_vec != 0 && reset_vec != 0x00CACACA);
    d->is_5_1_active = (surround_requested && fw_present);

    bool use_6ch_output = d->is_5_1_active && (g_config.audio.endpoint_mode == 0) && !g_config.audio.dolby_surround_ltrt;
    int want_channels = use_6ch_output ? 6 : 2;
    s_active_sdl_channels = want_channels;

    fprintf(stderr, "[APU MONITOR] Audio mode: %s (%d channels)%s%s\n",
            d->is_5_1_active ? "5.1 Surround" : "Stereo",
            want_channels,
            (!fw_present && surround_requested) ? " [Fallback: EP firmware missing]" : "",
            g_config.audio.dolby_surround_ltrt ? " [Dolby Surround Lt/Rt Active]" : "");

    SDL_AudioSpec spec = {
        .freq = 48000,
        .format = SDL_AUDIO_S16LE,
        .channels = want_channels,
    };

    d->monitor.stream = NULL;

    if (!SDL_Init(SDL_INIT_AUDIO)) {
        error_setg(errp, "SDL_Init failed: %s", SDL_GetError());
        return;
    }

    d->monitor.stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (d->monitor.stream == NULL) {
        error_setg(errp, "SDL_OpenAudioDeviceStream failed: %s",
                   SDL_GetError());
        return;
    }

    SDL_AudioDeviceID dev = SDL_GetAudioStreamDevice(d->monitor.stream);

    SDL_AudioSpec dev_spec;
    int dev_buf_frames = 0;
    int dev_drain_bytes = 0;
    if (SDL_GetAudioDeviceFormat(dev, &dev_spec, &dev_buf_frames)) {
        dev_drain_bytes = dev_buf_frames * spec.channels *
                          SDL_AUDIO_BYTESIZE(spec.format) *
                          spec.freq / dev_spec.freq;
    }
    int frame_bytes = (want_channels == 6) ? sizeof(d->monitor.surround_buf)
                                           : sizeof(d->monitor.frame_buf);
    int drain = MAX(dev_drain_bytes, frame_bytes);
    d->monitor.queued_bytes_low = drain;
    d->monitor.queued_bytes_high = 3 * drain;

    SDL_ResumeAudioDevice(dev);
}

void mcpx_apu_monitor_finalize(MCPXAPUState *d)
{
    if (d->monitor.stream) {
        SDL_DestroyAudioStream(d->monitor.stream);
    }
}

void mcpx_apu_monitor_frame(MCPXAPUState *d)
{
    if ((d->ep_frame_div + 1) % 8) {
        return;
    }

    bool use_6ch_output = d->is_5_1_active && (g_config.audio.endpoint_mode == 0) && !g_config.audio.dolby_surround_ltrt;
    int want_channels = use_6ch_output ? 6 : 2;

    if (d->monitor.stream && want_channels != s_active_sdl_channels) {
        SDL_DestroyAudioStream(d->monitor.stream);
        d->monitor.stream = NULL;

        SDL_AudioSpec spec = {
            .freq = 48000,
            .format = SDL_AUDIO_S16LE,
            .channels = want_channels,
        };

        d->monitor.stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);

        if (d->monitor.stream != NULL) {
            SDL_AudioDeviceID dev = SDL_GetAudioStreamDevice(d->monitor.stream);
            SDL_ResumeAudioDevice(dev);
            s_active_sdl_channels = want_channels;
            fprintf(stderr, "[APU MONITOR] Switched SDL3 audio stream to %d channels (%s)\n",
                    want_channels, use_6ch_output ? "5.1 Surround" : "Stereo / Headphones");
        }
    }

    int16_t aci_pcm[256 * 2];
    size_t aci_samples = mcpx_aci_read_pcm(aci_pcm, 256);
    if (aci_samples > 0) {
        if (use_6ch_output) {
            for (size_t i = 0; i < aci_samples; i++) {
                int32_t fl = d->monitor.surround_buf[i][0] + aci_pcm[i * 2];
                int32_t fr = d->monitor.surround_buf[i][1] + aci_pcm[i * 2 + 1];
                d->monitor.surround_buf[i][0] = clamp16(fl);
                d->monitor.surround_buf[i][1] = clamp16(fr);
            }
        } else {
            for (size_t i = 0; i < aci_samples; i++) {
                int32_t l = d->monitor.frame_buf[i][0] + aci_pcm[i * 2];
                int32_t r = d->monitor.frame_buf[i][1] + aci_pcm[i * 2 + 1];
                d->monitor.frame_buf[i][0] = clamp16(l);
                d->monitor.frame_buf[i][1] = clamp16(r);
            }
        }
    }

    if (d->monitor.stream) {
        float vu = pow(fmax(0.0, fmin(g_config.audio.volume_limit, 1.0)), M_E);
        SDL_SetAudioStreamGain(d->monitor.stream, vu);
        if (use_6ch_output) {
            SDL_PutAudioStreamData(d->monitor.stream, d->monitor.surround_buf,
                                   sizeof(d->monitor.surround_buf));
        } else {
            SDL_PutAudioStreamData(d->monitor.stream, d->monitor.frame_buf,
                                   sizeof(d->monitor.frame_buf));
        }
    }

    memset(d->monitor.frame_buf, 0, sizeof(d->monitor.frame_buf));
    memset(d->monitor.surround_buf, 0, sizeof(d->monitor.surround_buf));
}
