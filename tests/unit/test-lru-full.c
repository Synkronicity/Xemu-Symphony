/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "qemu/queue.h"

static unsigned long free_visits, eviction_visits;

static bool count_visit(const char *caller)
{
    if (!strcmp(caller, "lru_get_one_free")) {
        free_visits++;
    } else if (!strcmp(caller, "lru_try_evict_one")) {
        eviction_visits++;
    }
    return true;
}

/* Count actual loop-body visits; retain the production queue links/mutations. */
#undef QTAILQ_FOREACH_REVERSE
#define QTAILQ_FOREACH_REVERSE(var, head, field)                         \
    for ((var) = QTAILQ_LAST(head);                                    \
         (var) && count_visit(__func__);                              \
         (var) = QTAILQ_PREV(var, field))
#include "qemu/lru.h"

typedef struct Entry {
    LruNode node;
    uint64_t key;
    bool veto;
} Entry;

static Lru cache;
static Entry entries[4096];
static unsigned pre_calls, post_calls, init_calls;
static LruNode *pre_order[4096];

static void init_entry(Lru *lru, LruNode *node, const void *key)
{
    ((Entry *)node)->key = *(const uint64_t *)key;
    init_calls++;
}

static bool compare_entry(Lru *lru, LruNode *node, const void *key)
{
    return ((Entry *)node)->key != *(const uint64_t *)key;
}

static bool pre_evict(Lru *lru, LruNode *node)
{
    assert(pre_calls < 4096);
    pre_order[pre_calls++] = node;
    return !((Entry *)node)->veto;
}

static void post_evict(Lru *lru, LruNode *node)
{
    /* Removal from the hash bin precedes resource cleanup. */
    assert(!lru_is_node_in_use(lru, node));
    post_calls++;
}

static void reset_counts(void)
{
    free_visits = eviction_visits = 0;
    pre_calls = post_calls = init_calls = 0;
}

static void verify(unsigned total)
{
    bool global_seen[4096] = { 0 }, bin_seen[4096] = { 0 };
    unsigned used = 0, free_count = 0, bins = 0;
    LruNode *node;

    QTAILQ_FOREACH(node, &cache.global, next_global) {
        ptrdiff_t i = (Entry *)node - entries;
        assert(i >= 0 && i < total && !global_seen[i]);
        global_seen[i] = true;
        if (node->next_bin.tqe_circ.tql_prev) {
            used++;
        } else {
            free_count++;
        }
    }
    for (unsigned b = 0; b < LRU_NUM_BINS; b++) {
        QTAILQ_FOREACH(node, &cache.bins[b], next_bin) {
            ptrdiff_t i = (Entry *)node - entries;
            assert(i >= 0 && i < total && global_seen[i] && !bin_seen[i]);
            assert(node->hash % LRU_NUM_BINS == b);
            assert(node->next_bin.tqe_circ.tql_prev);
            bin_seen[i] = true;
            bins++;
        }
    }
    assert(used + free_count == total && bins == used);
    assert(cache.num_used == (int)used && cache.num_free == (int)free_count);
}

static void setup(unsigned count)
{
    memset(entries, 0, sizeof(entries));
    lru_init(&cache);
    cache.init_node = init_entry;
    cache.compare_nodes = compare_entry;
    cache.pre_node_evict = pre_evict;
    cache.post_node_evict = post_evict;
    for (unsigned i = 0; i < count; i++) {
        lru_add_free(&cache, &entries[i].node);
    }
    reset_counts();
    verify(count);
}

static void test_partial_collision_hit(void)
{
    uint64_t a = 1, b = 2, c = 3;
    setup(3);
    assert(lru_lookup(&cache, 7, &a) == &entries[2].node);
    verify(3);
    assert(lru_lookup(&cache, 7, &b) == &entries[1].node);
    verify(3);
    reset_counts();
    assert(lru_lookup(&cache, 7, &a) == &entries[2].node);
    assert(!free_visits && !eviction_visits && !init_calls);
    assert(!pre_calls && !post_calls);
    verify(3);
    assert(lru_lookup(&cache, 8, &c) == &entries[0].node);
    assert(free_visits == 1 && !eviction_visits && init_calls == 1);
    verify(3);
}

static void test_veto_eviction_flush(void)
{
    uint64_t keys[] = { 1, 2, 3, 4 };
    setup(3);
    for (unsigned i = 0; i < 3; i++) {
        lru_lookup(&cache, keys[i], &keys[i]);
        verify(3);
    }
    entries[2].veto = true; /* Oldest node must be visited and retained. */
    reset_counts();
    assert(lru_lookup(&cache, 4, &keys[3]) == &entries[1].node);
    assert(free_visits == 0 && eviction_visits == 2);
    assert(pre_calls == 2 && post_calls == 1 && init_calls == 1);
    assert(pre_order[0] == &entries[2].node);
    assert(pre_order[1] == &entries[1].node);
    verify(3);
    reset_counts();
    lru_flush(&cache);
    assert(pre_calls == 3 && post_calls == 2);
    assert(cache.num_used == 1 && cache.num_free == 2);
    verify(3);
    entries[2].veto = false;
    lru_flush(&cache);
    verify(3);
    assert(cache.num_free == 3);
    reset_counts();
    lru_lookup(&cache, 1, &keys[0]);
    assert(free_visits == 1 && !eviction_visits);
    verify(3);
    lru_evict_node(&cache, &entries[2].node);
    verify(3);
}

static void test_exhaustion(void)
{
    uint64_t key = 1;
    setup(0);
    assert(lru_try_evict_one(&cache) == NULL);
    verify(0);
    setup(1);
    lru_lookup(&cache, key, &key);
    verify(1);
    entries[0].veto = true;
    reset_counts();
    assert(lru_try_evict_one(&cache) == NULL);
    assert(pre_calls == 1 && !post_calls && !free_visits);
    verify(1);
#ifndef _WIN32
    /* Nullable eviction remains nullable; mandatory allocation still aborts. */
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        freopen("/dev/null", "w", stderr);
        lru_get_one_free(&cache);
        _exit(0);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#endif
}

static void test_saturated_churn(void)
{
    setup(4096);
    /* Initial reverse free selection gives keys 0..4095 an LRU FIFO order. */
    for (uint64_t key = 0; key < 4096; key++) {
        lru_lookup(&cache, key, &key);
    }
    verify(4096);
    reset_counts();
    for (uint64_t key = 4096; key < 5120; key++) {
        unsigned i = 4095 - (key - 4096);
        assert(lru_lookup(&cache, key, &key) == &entries[i].node);
        verify(4096);
    }
    printf("# saturated churn: 1024 misses, free visits=%lu, "
           "eviction visits=%lu, callbacks=%u/%u\n",
           free_visits, eviction_visits, pre_calls, post_calls);
    fflush(stdout);
    assert(pre_calls == 1024 && post_calls == 1024 && init_calls == 1024);
    assert(free_visits == 0 && eviction_visits == 1024);
}

int main(void)
{
    puts("TAP version 13");
    puts("1..4");
    test_partial_collision_hit();
    puts("ok 1 - partial cache, collisions and lookup hits");
    test_exhaustion();
    puts("ok 2 - empty and all-vetoed exhaustion contracts");
    test_saturated_churn();
    puts("ok 3 - saturated misses skip free traversal and preserve eviction");
    test_veto_eviction_flush();
    puts("ok 4 - veto order, cleanup callbacks, flush and reuse");
    return 0;
}
