/*
 * QEMU MCPX Audio Codec Interface implementation
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2020-2021 Matt Borgerson
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

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "hw/audio/ac97_int.h"
#include "migration/vmstate.h"
#include "hw/xbox/mcpx/aci.h"
#include "qemu/timer.h"

#define ACI_RING_BUFFER_BYTES 65536
#define ACI_RING_BUFFER_SAMPLES (ACI_RING_BUFFER_BYTES / sizeof(int16_t))

typedef struct ACIStream {
    QemuMutex lock;
    int16_t buffer[ACI_RING_BUFFER_SAMPLES];
    size_t read_pos;
    size_t write_pos;

    QEMUTimer *timer;
    int64_t last_time_ns;
    AC97LinkState *ac97;
    bool initialized;
} ACIStream;

static ACIStream s_aci_stream;

void mcpx_aci_push_pcm(const void *buf, size_t bytes)
{
    if (!buf || bytes < sizeof(int16_t)) {
        return;
    }

    const int16_t *src = (const int16_t *)buf;
    size_t num_samples = bytes / sizeof(int16_t);
    size_t max_samples = ACI_RING_BUFFER_SAMPLES;

    if (num_samples > max_samples) {
        src += (num_samples - max_samples);
        num_samples = max_samples;
    }

    qemu_mutex_lock(&s_aci_stream.lock);
    for (size_t i = 0; i < num_samples; i++) {
        size_t next_write = (s_aci_stream.write_pos + 1) % max_samples;
        if (next_write == s_aci_stream.read_pos) {
            /* Discard oldest sample on overflow */
            s_aci_stream.read_pos = (s_aci_stream.read_pos + 1) % max_samples;
        }
        s_aci_stream.buffer[s_aci_stream.write_pos] = src[i];
        s_aci_stream.write_pos = next_write;
    }
    qemu_mutex_unlock(&s_aci_stream.lock);
}

size_t mcpx_aci_read_pcm(int16_t *dest, size_t samples)
{
    size_t read_samples = 0;
    size_t total_samples = samples * 2; /* 16-bit stereo interleaved */

    qemu_mutex_lock(&s_aci_stream.lock);
    while (read_samples < total_samples && s_aci_stream.read_pos != s_aci_stream.write_pos) {
        dest[read_samples++] = s_aci_stream.buffer[s_aci_stream.read_pos];
        s_aci_stream.read_pos = (s_aci_stream.read_pos + 1) % (ACI_RING_BUFFER_BYTES / sizeof(int16_t));
    }
    qemu_mutex_unlock(&s_aci_stream.lock);

    /* Zero-fill any remaining samples to prevent stale stack leakage */
    if (read_samples < total_samples) {
        memset(&dest[read_samples], 0, (total_samples - read_samples) * sizeof(int16_t));
    }

    return read_samples / 2;
}

static void mcpx_aci_timer_cb(void *opaque)
{
    (void)opaque;
    if (!s_aci_stream.ac97) {
        return;
    }

    int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t delta_ns = now_ns - s_aci_stream.last_time_ns;

    if (delta_ns < 0) {
        s_aci_stream.last_time_ns = now_ns;
        delta_ns = 0;
    } else if (delta_ns > 500000000LL) {
        /* Cap at 500 ms to avoid huge catch-up bursts after host stalls */
        delta_ns = 500000000LL;
        s_aci_stream.last_time_ns = now_ns - delta_ns;
    }

    /* 48,000 Hz, 16-bit stereo = 192,000 bytes/sec */
    int64_t bytes = (delta_ns * 192000LL) / 1000000000LL;
    if (bytes >= 128) {
        bytes &= ~3;
        ac97_step_playback(s_aci_stream.ac97, (int)bytes);
        /* Advance virtual timestamp by the exact nanosecond duration of consumed PCM bytes */
        s_aci_stream.last_time_ns += ((int64_t)bytes * 1000000000LL) / 192000LL;
    }

    timer_mod(s_aci_stream.timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000); /* +1 ms */
}

typedef struct MCPXACIState {
    PCIDevice dev;

    AC97LinkState ac97;

    MemoryRegion io_nam, io_nabm;

    MemoryRegion mmio;
    MemoryRegion nam_mmio, nabm_mmio;
} MCPXACIState;

#define MCPX_ACI_DEVICE(obj) \
    OBJECT_CHECK(MCPXACIState, (obj), "mcpx-aci")

static void mcpx_aci_realize(PCIDevice *dev, Error **errp)
{
    MCPXACIState *d = MCPX_ACI_DEVICE(dev);

    dev->config[PCI_INTERRUPT_PIN] = 0x01;

    memory_region_init(&d->mmio, OBJECT(dev), "mcpx-aci-mmio", 0x1000);

    memory_region_init_io(&d->io_nam, OBJECT(dev), &ac97_io_nam_ops, &d->ac97,
                          "mcpx-aci-nam", 0x100);
    memory_region_init_io(&d->io_nabm, OBJECT(dev), &ac97_io_nabm_ops, &d->ac97,
                          "mcpx-aci-nabm", 0x80);

    /*pci_register_bar(&d->dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &d->io_nam);
    pci_register_bar(&d->dev, 1, PCI_BASE_ADDRESS_SPACE_IO, &d->io_nabm);

    memory_region_init_alias(&d->nam_mmio, NULL, &d->io_nam, 0, 0x100);
    memory_region_add_subregion(&d->mmio, 0x0, &d->nam_mmio);

    memory_region_init_alias(&d->nabm_mmio, NULL, &d->io_nabm, 0, 0x80);
    memory_region_add_subregion(&d->mmio, 0x100, &d->nabm_mmio);*/

    memory_region_add_subregion(&d->mmio, 0x0, &d->io_nam);
    memory_region_add_subregion(&d->mmio, 0x100, &d->io_nabm);

    pci_register_bar(&d->dev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);
    ac97_common_init(&d->ac97, &d->dev, pci_get_address_space(&d->dev));

    if (!s_aci_stream.initialized) {
        qemu_mutex_init(&s_aci_stream.lock);
        s_aci_stream.initialized = true;
    }
    s_aci_stream.ac97 = &d->ac97;
    s_aci_stream.read_pos = 0;
    s_aci_stream.write_pos = 0;
    s_aci_stream.last_time_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (!s_aci_stream.timer) {
        s_aci_stream.timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, mcpx_aci_timer_cb, NULL);
    }
    timer_mod(s_aci_stream.timer, s_aci_stream.last_time_ns + 1000000);
}

static const VMStateDescription vmstate_mcpx_aci = {
    .name = "mcpx-aci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, MCPXACIState),
        // FIXME
        VMSTATE_END_OF_LIST()
    },
};

static void mcpx_aci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_NVIDIA;
    k->device_id = PCI_DEVICE_ID_NVIDIA_MCPX_ACI;
    k->revision = 177;
    k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    k->realize = mcpx_aci_realize;

    dc->desc = "MCPX Audio Codec Interface";
    dc->vmsd = &vmstate_mcpx_aci;
}

static const TypeInfo mcpx_aci_info = {
    .name          = "mcpx-aci",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MCPXACIState),
    .class_init    = mcpx_aci_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void mcpx_aci_register(void)
{
    type_register_static(&mcpx_aci_info);
}

type_init(mcpx_aci_register);
