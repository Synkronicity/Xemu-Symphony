/*
 * NV2A inline element packet helpers
 *
 * Copyright (c) 2026 xemu project contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_INLINE_ELEMENTS_H
#define HW_XBOX_NV2A_PGRAPH_INLINE_ELEMENTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum PGRAPHInlinePacketMode {
    PGRAPH_INLINE_PACKET_BULK,
    PGRAPH_INLINE_PACKET_SCALAR_INCREMENTING,
    PGRAPH_INLINE_PACKET_SCALAR_TRACE,
} PGRAPHInlinePacketMode;

static inline PGRAPHInlinePacketMode pgraph_inline_packet_mode(
    bool incrementing, bool tracing)
{
    if (incrementing) {
        return PGRAPH_INLINE_PACKET_SCALAR_INCREMENTING;
    }
    if (tracing) {
        return PGRAPH_INLINE_PACKET_SCALAR_TRACE;
    }
    return PGRAPH_INLINE_PACKET_BULK;
}

static inline bool pgraph_inline_packet_plan_length(size_t current_length,
                                                    size_t pending_values,
                                                    size_t packet_words,
                                                    size_t values_per_word,
                                                    size_t capacity,
                                                    size_t *output_length)
{
    if (!values_per_word || current_length > capacity ||
        pending_values > capacity - current_length) {
        return false;
    }

    size_t length_after_pending = current_length + pending_values;
    if (packet_words >
        (capacity - length_after_pending) / values_per_word) {
        return false;
    }

    if (output_length) {
        *output_length =
            length_after_pending + packet_words * values_per_word;
    }
    return true;
}

static inline bool pgraph_inline_packet_fits(size_t current_length,
                                             size_t packet_words,
                                             size_t values_per_word,
                                             size_t capacity)
{
    return pgraph_inline_packet_plan_length(current_length, 0, packet_words,
                                             values_per_word, capacity, NULL);
}

static inline void pgraph_inline_element16_store(uint32_t *destination,
                                                 uint32_t parameter)
{
    destination[0] = parameter & 0xffff;
    destination[1] = parameter >> 16;
}

#endif
