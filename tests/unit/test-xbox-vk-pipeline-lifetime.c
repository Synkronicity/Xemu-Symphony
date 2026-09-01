/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qemu/lru.h"
#include "hw/xbox/nv2a/pgraph/vk/pipeline-cache-lifetime.h"

#define TEST_CONTAINER_OF(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

typedef struct TestCacheEntry {
    LruNode node;
    uint64_t key;
    bool pinned;
} TestCacheEntry;

static void test_cache_entry_init(Lru *lru, LruNode *node, const void *key)
{
    TestCacheEntry *entry = TEST_CONTAINER_OF(node, TestCacheEntry, node);

    (void)lru;
    entry->key = *(const uint64_t *)key;
}

static bool test_cache_entry_compare(Lru *lru, LruNode *node,
                                     const void *key)
{
    TestCacheEntry *entry = TEST_CONTAINER_OF(node, TestCacheEntry, node);

    (void)lru;
    return entry->key != *(const uint64_t *)key;
}

static bool test_cache_entry_pre_evict(Lru *lru, LruNode *node)
{
    TestCacheEntry *entry = TEST_CONTAINER_OF(node, TestCacheEntry, node);

    (void)lru;
    return !entry->pinned;
}

static TestCacheEntry *test_cache_lookup(Lru *cache, uint64_t key)
{
    LruNode *node = lru_try_lookup(cache, key, &key);

    return node ? TEST_CONTAINER_OF(node, TestCacheEntry, node) : NULL;
}

static void test_graphics_pipeline_active_command_lifetime(void)
{
    assert(pgraph_vk_graphics_pipeline_can_evict(false, 7, 7));
    assert(pgraph_vk_graphics_pipeline_can_evict(true, 6, 7));
    assert(!pgraph_vk_graphics_pipeline_can_evict(true, 7, 7));
    assert(!pgraph_vk_graphics_pipeline_can_evict(true, 8, 7));
}

static void test_lru_exhaustion_is_recoverable(void)
{
    Lru cache;
    TestCacheEntry entries[2] = { 0 };
    TestCacheEntry *first;
    TestCacheEntry *second;

    lru_init(&cache);
    cache.init_node = test_cache_entry_init;
    cache.compare_nodes = test_cache_entry_compare;
    cache.pre_node_evict = test_cache_entry_pre_evict;
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        lru_add_free(&cache, &entries[i].node);
    }

    first = test_cache_lookup(&cache, 1);
    second = test_cache_lookup(&cache, 2);
    assert(first);
    assert(second);
    first->pinned = true;
    second->pinned = true;

    assert(test_cache_lookup(&cache, 1) == first);
    assert(!test_cache_lookup(&cache, 3));
    assert(cache.num_used == 2);
    assert(cache.num_free == 0);
    assert(first->key == 1);
    assert(second->key == 2);

    first->pinned = false;
    assert(test_cache_lookup(&cache, 3) == first);
    assert(first->key == 3);
}

int main(void)
{
    test_graphics_pipeline_active_command_lifetime();
    test_lru_exhaustion_is_recoverable();

    return 0;
}
