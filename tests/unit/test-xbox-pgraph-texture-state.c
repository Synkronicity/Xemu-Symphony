/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <stdio.h>

#include "hw/xbox/nv2a/pgraph/texture-state.h"

static bool test_texture_stage_invalidation_is_scoped(void)
{
    bool dirty[4] = { false };

    pgraph_texture_stage_invalidate(dirty, 2);

    return !dirty[0] && !dirty[1] && dirty[2] && !dirty[3];
}

static bool test_texture_stage_invalidation_preserves_dirty(void)
{
    bool dirty[4] = { false, true, false, false };

    pgraph_texture_stage_invalidate(dirty, 1);

    return dirty[1];
}

int main(void)
{
    bool scoped = test_texture_stage_invalidation_is_scoped();
    bool preserves = test_texture_stage_invalidation_preserves_dirty();

    puts("TAP version 13");
    puts("1..2");
    printf("%s 1 - texture invalidation is stage scoped\n",
           scoped ? "ok" : "not ok");
    printf("%s 2 - texture invalidation preserves dirty state\n",
           preserves ? "ok" : "not ok");
    return scoped && preserves ? 0 : 1;
}
