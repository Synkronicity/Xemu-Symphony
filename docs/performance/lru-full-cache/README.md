# Full-cache free-search removal

When an LRU cache is full, every miss previously traversed the entire global
list looking for a free node before traversing it again for eviction. The existing
`num_free` count already establishes that the first traversal cannot succeed.
`lru_get_one_free()` now goes directly to its existing eviction operation when
that count is zero. Partial-cache free selection and eviction policy are unchanged.

Review parent: `d9bf8e1dcfd4ee72813c9f9f5572b780fc65cc76` on
`release/eng-2026-523-stable-v2-final`. This is a focused repair for issue #41,
not a new qualification of that combined release candidate.

## Results

The standalone test includes the production LRU header and real queue mutations.
Test-only loop instrumentation counts free-search visits independently from
candidate eviction visits and callbacks. No instrumentation is added to production.

| Saturated cache: 4,096 entries, 1,024 misses | Parent | Repair |
| --- | ---: | ---: |
| Unsuccessful free-node visits | 4,194,304 | 0 |
| Eviction-node visits | 1,024 | 1,024 |
| Pre-eviction callbacks | 1,024 | 1,024 |
| Post-eviction callbacks | 1,024 | 1,024 |
| Initialized replacement entries | 1,024 | 1,024 |
| Expected replacement selection | Pass | Pass |
| Counter and membership invariants during churn | Pass | Pass |
| Zero-free-search requirement | Fail | Pass |

The old-header negative control reaches the scan-bound assertion and aborts;
the repaired header passes. The test also covers partial caches, hash collisions,
lookup hits, veto order, nullable exhaustion, flush, reuse, and callback ordering.
On POSIX hosts a subprocess verifies that mandatory allocation still aborts when
all nodes are vetoed; Windows builds skip that subprocess check. Counters and
bin/global membership are checked after mutations in these scenarios.

Four grouped regression tests pass with GCC 14.2.0 on Linux, both optimized and
with AddressSanitizer/UndefinedBehaviorSanitizer. The four existing active-LRU
checks also pass. Both test executables emit TAP; `test-lru-full` is registered
in the existing Meson unit-test collection. The full Meson/emulator build was
not rerun for this header repair.

These are exact operation counts from a controlled workload, not emulator CPU,
GPU, elapsed-time, FPS, p95, or p99 measurements. No end-to-end performance gain
is claimed. Existing retail results do not qualify the new source revision.
The cache still requires its callers' existing serialization; no concurrency
policy or callback lifetime changes are introduced.

## Reproduction

From the source root, without emulator dependencies:

```sh
mkdir -p build-lru-checks
cc -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter \
  -Iinclude tests/unit/test-lru-full.c -o ./build-lru-checks/test-lru-full
./build-lru-checks/test-lru-full --tap -k
cc -std=gnu11 -O1 -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Wall -Wextra -Werror -Wno-unused-parameter \
  -Iinclude tests/unit/test-lru-full.c -o ./build-lru-checks/test-lru-full-sanitize
./build-lru-checks/test-lru-full-sanitize --tap -k
cc -std=gnu11 -O2 -Iinclude tests/unit/test-lru-active.c -o ./build-lru-checks/test-lru-active
./build-lru-checks/test-lru-active --tap -k
```

For the negative control, put the parent's `include/qemu/lru.h` into a temporary
`qemu/lru.h`, add that temporary directory before `-Iinclude`, and compile the
same regression source. This exercises the old production header without
changing the checkout. Expect 4,194,304 free visits and the scan-bound assertion.
See [counts.csv](counts.csv) for the compact comparison.
