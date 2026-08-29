/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Vulkan dynamic blend-constant command cache.
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_BLEND_CONSTANTS_CACHE_H
#define HW_XBOX_NV2A_PGRAPH_VK_BLEND_CONSTANTS_CACHE_H

typedef struct PGRAPHVkBlendConstantsCache {
    uint32_t guest_color;
    bool valid;
} PGRAPHVkBlendConstantsCache;

static inline void pgraph_vk_blend_constants_cache_invalidate(
    PGRAPHVkBlendConstantsCache *cache)
{
    cache->valid = false;
}

/* A graphics pipeline bind invalidates all cached dynamic-state assumptions. */
static inline void pgraph_vk_blend_constants_cache_pipeline_bound(
    PGRAPHVkBlendConstantsCache *cache)
{
    pgraph_vk_blend_constants_cache_invalidate(cache);
}

/* Returns true exactly when the caller must emit vkCmdSetBlendConstants. */
static inline bool pgraph_vk_blend_constants_cache_update(
    PGRAPHVkBlendConstantsCache *cache, uint32_t guest_color)
{
    if (cache->valid && cache->guest_color == guest_color) {
        return false;
    }

    cache->guest_color = guest_color;
    cache->valid = true;
    return true;
}

#endif
