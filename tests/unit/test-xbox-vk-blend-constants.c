/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/blend-constants-cache.h"

static void test_first_value_emits(void)
{
    PGRAPHVkBlendConstantsCache cache = {0};

    g_assert_true(pgraph_vk_blend_constants_cache_update(&cache, 0));
    g_assert_true(cache.valid);
    g_assert_cmphex(cache.guest_color, ==, 0);
}

static void test_identical_value_skips(void)
{
    PGRAPHVkBlendConstantsCache cache = {0};

    g_assert_true(pgraph_vk_blend_constants_cache_update(&cache, 0x12345678));
    g_assert_false(pgraph_vk_blend_constants_cache_update(&cache, 0x12345678));
    g_assert_true(pgraph_vk_blend_constants_cache_update(&cache, 0x12345679));
}

static void test_invalidation_forces_emit(void)
{
    PGRAPHVkBlendConstantsCache cache = {0};

    g_assert_true(pgraph_vk_blend_constants_cache_update(&cache, 0x89abcdef));
    pgraph_vk_blend_constants_cache_invalidate(&cache);
    g_assert_false(cache.valid);
    g_assert_true(pgraph_vk_blend_constants_cache_update(&cache, 0x89abcdef));
}

static void test_dynamic_static_dynamic_transition(void)
{
    PGRAPHVkBlendConstantsCache cache = {0};

    pgraph_vk_blend_constants_cache_pipeline_bound(&cache);
    g_assert_true(pgraph_vk_blend_constants_cache_update(&cache, 0x10203040));
    g_assert_false(pgraph_vk_blend_constants_cache_update(&cache, 0x10203040));

    /* Bind a static-state pipeline, then return to a dynamic-state pipeline. */
    pgraph_vk_blend_constants_cache_pipeline_bound(&cache);
    pgraph_vk_blend_constants_cache_pipeline_bound(&cache);
    g_assert_true(pgraph_vk_blend_constants_cache_update(&cache, 0x10203040));
}

static void test_dynamic_clear_dynamic_transition(void)
{
    PGRAPHVkBlendConstantsCache cache = {0};

    pgraph_vk_blend_constants_cache_pipeline_bound(&cache);
    g_assert_true(pgraph_vk_blend_constants_cache_update(&cache, 0x50607080));

    /* Full/depth clear binds a pipeline without setting blend constants. */
    pgraph_vk_blend_constants_cache_pipeline_bound(&cache);
    pgraph_vk_blend_constants_cache_pipeline_bound(&cache);
    g_assert_true(pgraph_vk_blend_constants_cache_update(&cache, 0x50607080));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vulkan/blend-constants/first-value",
                    test_first_value_emits);
    g_test_add_func("/xbox/vulkan/blend-constants/identical-value",
                    test_identical_value_skips);
    g_test_add_func("/xbox/vulkan/blend-constants/invalidation",
                    test_invalidation_forces_emit);
    g_test_add_func("/xbox/vulkan/blend-constants/dynamic-static-dynamic",
                    test_dynamic_static_dynamic_transition);
    g_test_add_func("/xbox/vulkan/blend-constants/dynamic-clear-dynamic",
                    test_dynamic_clear_dynamic_transition);

    return g_test_run();
}
