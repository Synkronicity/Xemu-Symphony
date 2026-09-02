/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "qemu/lru.h"

#define TEST_CONTAINER_OF(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

typedef struct TestEntry {
    LruNode node;
    uint64_t key;
    unsigned int index;
} TestEntry;

typedef struct VisitResult {
    unsigned int count;
    uint32_t mask;
} VisitResult;

static void entry_init(Lru *lru, LruNode *node, const void *key)
{
    TestEntry *entry = TEST_CONTAINER_OF(node, TestEntry, node);

    (void)lru;
    entry->key = *(const uint64_t *)key;
}

static bool entry_compare(Lru *lru, LruNode *node, const void *key)
{
    TestEntry *entry = TEST_CONTAINER_OF(node, TestEntry, node);

    (void)lru;
    return entry->key != *(const uint64_t *)key;
}

static void record_visit(Lru *lru, LruNode *node, void *opaque)
{
    TestEntry *entry = TEST_CONTAINER_OF(node, TestEntry, node);
    VisitResult *result = opaque;

    (void)lru;
    result->count++;
    result->mask |= 1U << entry->index;
}

int main(void)
{
    Lru lru;
    TestEntry entries[3] = {
        { .index = 0 },
        { .index = 1 },
        { .index = 2 },
    };
    VisitResult active = { 0 };
    VisitResult flushed = { 0 };
    LruNode *node0;
    LruNode *node1;
    uint32_t expected_mask;
    uint64_t key0 = 1;
    uint64_t key1 = 2;

    lru_init(&lru);
    lru.init_node = entry_init;
    lru.compare_nodes = entry_compare;
    for (size_t i = 0; i < 3; i++) {
        lru_add_free(&lru, &entries[i].node);
    }

    node0 = lru_lookup(&lru, key0, &key0);
    node1 = lru_lookup(&lru, key1, &key1);
    expected_mask =
        1U << TEST_CONTAINER_OF(node0, TestEntry, node)->index |
        1U << TEST_CONTAINER_OF(node1, TestEntry, node)->index;
    lru_visit_active(&lru, record_visit, &active);
    lru_flush(&lru);
    lru_visit_active(&lru, record_visit, &flushed);

    puts("TAP version 13");
    puts("1..2");
    printf("%s 1 - visit includes every active node and no free node\n",
           active.count == 2 && active.mask == expected_mask ?
           "ok" : "not ok");
    printf("%s 2 - visit excludes nodes after flush\n",
           flushed.count == 0 && flushed.mask == 0 ? "ok" : "not ok");

    return active.count == 2 && active.mask == expected_mask &&
           flushed.count == 0 && flushed.mask == 0 ? 0 : 1;
}
