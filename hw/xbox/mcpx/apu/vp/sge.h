/* SPDX-License-Identifier: LGPL-2.0-or-later */

#ifndef HW_XBOX_MCPX_APU_VP_SGE_H
#define HW_XBOX_MCPX_APU_VP_SGE_H

#include "qemu/rcu.h"
#include "system/memory.h"

typedef struct MCPXAPUSGETranslationCache {
    hwaddr descriptor;
    MemoryRegionCache mapping;
} MCPXAPUSGETranslationCache;

static inline void mcpx_apu_sge_cache_clear(MCPXAPUSGETranslationCache *cache)
{
    address_space_cache_destroy(&cache->mapping);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(MCPXAPUSGETranslationCache,
                                 mcpx_apu_sge_cache_clear)

static uint32_t
mcpx_apu_sge_read_descriptor_slow(MCPXAPUSGETranslationCache *cache,
                                  hwaddr descriptor)
{
    RCU_READ_LOCK_GUARD();

    mcpx_apu_sge_cache_clear(cache);
    int64_t len =
        address_space_cache_init(&cache->mapping, &address_space_memory,
                                 descriptor, sizeof(uint32_t), false);
    if (len != sizeof(uint32_t) || !cache->mapping.ptr) {
        mcpx_apu_sge_cache_clear(cache);
        return ldl_le_phys(&address_space_memory, descriptor);
    }
    cache->descriptor = descriptor;
    return ldl_le_phys_cached(&cache->mapping, 0);
}

static inline uint32_t
mcpx_apu_sge_read_descriptor(MCPXAPUSGETranslationCache *cache,
                             hwaddr descriptor)
{
    MemoryRegionCache *mapping = &cache->mapping;

    /* Retain a RAM mapping, never a guest-written descriptor value. */
    if (likely(mapping->mrs.mr && mapping->ptr &&
               cache->descriptor == descriptor &&
               mapping->fv ==
                   address_space_to_flatview(&address_space_memory))) {
        return ldl_le_phys_cached(mapping, 0);
    }

    return mcpx_apu_sge_read_descriptor_slow(cache, descriptor);
}

#endif
