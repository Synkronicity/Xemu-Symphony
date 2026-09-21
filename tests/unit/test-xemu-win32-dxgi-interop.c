/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "ui/xui/win32-dxgi-interop.h"
#include "ui/xui/win32-dxgi-present-state.h"

typedef struct InteropFixture {
    bool lock_result;
    bool unlock_result;
    unsigned int locks;
    unsigned int blits;
    unsigned int unlocks;
    unsigned int d3d_uses;
    bool unregister_result;
    unsigned int unregisters;
    unsigned int destroys;
    bool close_result;
    unsigned int closes;
    unsigned int releases;
    unsigned int abandons;
    unsigned int resizes;
} InteropFixture;

static bool lock_object(void *opaque)
{
    InteropFixture *fixture = opaque;
    fixture->locks++;
    return fixture->lock_result;
}

static void blit_object(void *opaque)
{
    InteropFixture *fixture = opaque;
    fixture->blits++;
}

static bool unlock_object(void *opaque)
{
    InteropFixture *fixture = opaque;
    fixture->unlocks++;
    return fixture->unlock_result;
}

static void use_from_d3d(void *opaque)
{
    InteropFixture *fixture = opaque;
    fixture->d3d_uses++;
}

static bool unregister_object(void *opaque)
{
    InteropFixture *fixture = opaque;
    fixture->unregisters++;
    return fixture->unregister_result;
}

static void destroy_object(void *opaque)
{
    InteropFixture *fixture = opaque;
    fixture->destroys++;
}

static bool close_device(void *opaque)
{
    InteropFixture *fixture = opaque;
    fixture->closes++;
    return fixture->close_result;
}

static void release_owners(void *opaque)
{
    InteropFixture *fixture = opaque;
    fixture->releases++;
}

static void abandon_owners(void *opaque)
{
    InteropFixture *fixture = opaque;
    fixture->abandons++;
}

static const XemuWin32DxgiInteropOps ops = {
    .lock = lock_object,
    .blit = blit_object,
    .unlock = unlock_object,
    .use_from_d3d = use_from_d3d,
};

static const XemuWin32DxgiInteropReleaseOps release_ops = {
    .unregister = unregister_object,
    .destroy = destroy_object,
};

static const XemuWin32DxgiInteropShutdownOps shutdown_ops = {
    .unregister = unregister_object,
    .destroy = destroy_object,
    .close = close_device,
    .release_owners = release_owners,
    .abandon_owners = abandon_owners,
};

typedef struct PresentFixture {
    bool init_result;
    bool begin_result;
    bool end_result;
    bool quarantined;
    bool quarantine_on_init;
    unsigned int inits;
    unsigned int begins;
    unsigned int ends;
    unsigned int shutdowns;
} PresentFixture;

static bool present_init(void *opaque)
{
    PresentFixture *fixture = opaque;
    fixture->inits++;
    if (fixture->quarantine_on_init) {
        fixture->quarantined = true;
    }
    return fixture->init_result;
}

static bool present_begin(void *opaque)
{
    PresentFixture *fixture = opaque;
    fixture->begins++;
    return fixture->begin_result;
}

static bool present_end(void *opaque)
{
    PresentFixture *fixture = opaque;
    fixture->ends++;
    return fixture->end_result;
}

static void present_shutdown(void *opaque)
{
    PresentFixture *fixture = opaque;
    fixture->shutdowns++;
}

static bool present_is_quarantined(void *opaque)
{
    PresentFixture *fixture = opaque;
    return fixture->quarantined;
}

static const XemuWin32DxgiPresentOps present_ops = {
    .init = present_init,
    .begin_frame = present_begin,
    .end_frame = present_end,
    .shutdown = present_shutdown,
    .is_quarantined = present_is_quarantined,
};

static void test_successful_transfer(void)
{
    InteropFixture fixture = {
        .lock_result = true,
        .unlock_result = true,
    };
    XemuWin32DxgiInteropOwnership ownership = XEMU_WIN32_DXGI_INTEROP_UNLOCKED;

    g_assert_cmpint(
        xemu_win32_dxgi_interop_transfer(&ops, &ownership, &fixture), ==,
        XEMU_WIN32_DXGI_INTEROP_TRANSFERRED);
    g_assert_cmpuint(fixture.locks, ==, 1);
    g_assert_cmpuint(fixture.blits, ==, 1);
    g_assert_cmpuint(fixture.unlocks, ==, 1);
    g_assert_cmpuint(fixture.d3d_uses, ==, 1);
    g_assert_cmpint(ownership, ==, XEMU_WIN32_DXGI_INTEROP_UNLOCKED);
}

static void test_lock_failure_stops_transfer(void)
{
    InteropFixture fixture = { .unlock_result = true };
    XemuWin32DxgiInteropOwnership ownership = XEMU_WIN32_DXGI_INTEROP_UNLOCKED;

    g_assert_cmpint(
        xemu_win32_dxgi_interop_transfer(&ops, &ownership, &fixture), ==,
        XEMU_WIN32_DXGI_INTEROP_LOCK_FAILED);
    g_assert_cmpuint(fixture.locks, ==, 1);
    g_assert_cmpuint(fixture.blits, ==, 0);
    g_assert_cmpuint(fixture.unlocks, ==, 0);
    g_assert_cmpuint(fixture.d3d_uses, ==, 0);
    g_assert_cmpint(ownership, ==, XEMU_WIN32_DXGI_INTEROP_UNLOCKED);
}

static void test_unlock_failure_blocks_d3d(void)
{
    InteropFixture fixture = { .lock_result = true };
    XemuWin32DxgiInteropOwnership ownership = XEMU_WIN32_DXGI_INTEROP_UNLOCKED;

    g_assert_cmpint(
        xemu_win32_dxgi_interop_transfer(&ops, &ownership, &fixture), ==,
        XEMU_WIN32_DXGI_INTEROP_UNLOCK_FAILED);
    g_assert_cmpuint(fixture.locks, ==, 1);
    g_assert_cmpuint(fixture.blits, ==, 1);
    g_assert_cmpuint(fixture.unlocks, ==, 1);
    g_assert_cmpuint(fixture.d3d_uses, ==, 0);
    g_assert_cmpint(ownership, ==, XEMU_WIN32_DXGI_INTEROP_QUARANTINED);

    g_assert_cmpint(
        xemu_win32_dxgi_interop_transfer(&ops, &ownership, &fixture), ==,
        XEMU_WIN32_DXGI_INTEROP_NOT_READY);
    g_assert_cmpuint(fixture.locks, ==, 1);
    g_assert_cmpuint(fixture.blits, ==, 1);
    g_assert_cmpuint(fixture.unlocks, ==, 1);
    g_assert_cmpuint(fixture.d3d_uses, ==, 0);
}

static void test_non_unlocked_entry_stops_transfer(void)
{
    InteropFixture fixture = {
        .lock_result = true,
        .unlock_result = true,
    };

    XemuWin32DxgiInteropOwnership ownership =
        XEMU_WIN32_DXGI_INTEROP_LOCKED_BY_OPENGL;
    g_assert_cmpint(
        xemu_win32_dxgi_interop_transfer(&ops, &ownership, &fixture), ==,
        XEMU_WIN32_DXGI_INTEROP_NOT_READY);

    ownership = XEMU_WIN32_DXGI_INTEROP_QUARANTINED;
    g_assert_cmpint(
        xemu_win32_dxgi_interop_transfer(&ops, &ownership, &fixture), ==,
        XEMU_WIN32_DXGI_INTEROP_NOT_READY);

    g_assert_cmpuint(fixture.locks, ==, 0);
    g_assert_cmpuint(fixture.blits, ==, 0);
    g_assert_cmpuint(fixture.unlocks, ==, 0);
    g_assert_cmpuint(fixture.d3d_uses, ==, 0);
}

static void test_release_success_allows_resize(void)
{
    InteropFixture fixture = { .unregister_result = true };
    XemuWin32DxgiInteropOwnership ownership = XEMU_WIN32_DXGI_INTEROP_UNLOCKED;

    if (xemu_win32_dxgi_interop_release(&release_ops, &ownership, true,
                                        &fixture) ==
        XEMU_WIN32_DXGI_INTEROP_RELEASED) {
        fixture.resizes++;
    }

    g_assert_cmpuint(fixture.unregisters, ==, 1);
    g_assert_cmpuint(fixture.destroys, ==, 1);
    g_assert_cmpuint(fixture.resizes, ==, 1);
    g_assert_cmpint(ownership, ==, XEMU_WIN32_DXGI_INTEROP_UNLOCKED);
}

static void test_uncertain_release_preserves_resources(void)
{
    InteropFixture fixture = { .unregister_result = true };
    XemuWin32DxgiInteropOwnership ownership =
        XEMU_WIN32_DXGI_INTEROP_LOCKED_BY_OPENGL;

    g_assert_cmpint(xemu_win32_dxgi_interop_release(&release_ops, &ownership,
                                                    true, &fixture),
                    ==, XEMU_WIN32_DXGI_INTEROP_OWNERSHIP_UNCERTAIN);

    ownership = XEMU_WIN32_DXGI_INTEROP_QUARANTINED;
    g_assert_cmpint(xemu_win32_dxgi_interop_release(&release_ops, &ownership,
                                                    true, &fixture),
                    ==, XEMU_WIN32_DXGI_INTEROP_OWNERSHIP_UNCERTAIN);
    g_assert_cmpuint(fixture.unregisters, ==, 0);
    g_assert_cmpuint(fixture.destroys, ==, 0);
    g_assert_cmpuint(fixture.resizes, ==, 0);
}

static void test_unregister_failure_preserves_resources(void)
{
    InteropFixture fixture = {};
    XemuWin32DxgiInteropOwnership ownership = XEMU_WIN32_DXGI_INTEROP_UNLOCKED;

    if (xemu_win32_dxgi_interop_release(&release_ops, &ownership, true,
                                        &fixture) ==
        XEMU_WIN32_DXGI_INTEROP_RELEASED) {
        fixture.resizes++;
    }

    g_assert_cmpuint(fixture.unregisters, ==, 1);
    g_assert_cmpuint(fixture.destroys, ==, 0);
    g_assert_cmpuint(fixture.resizes, ==, 0);
    g_assert_cmpint(ownership, ==, XEMU_WIN32_DXGI_INTEROP_QUARANTINED);
}

static void test_normal_shutdown_releases_once(void)
{
    InteropFixture fixture = {
        .unregister_result = true,
        .close_result = true,
    };
    XemuWin32DxgiInteropOwnership ownership = XEMU_WIN32_DXGI_INTEROP_UNLOCKED;

    g_assert_cmpint(xemu_win32_dxgi_interop_shutdown(
                        &shutdown_ops, &ownership, true, true, &fixture),
                    ==, XEMU_WIN32_DXGI_INTEROP_SHUTDOWN_COMPLETE);
    g_assert_cmpuint(fixture.unregisters, ==, 1);
    g_assert_cmpuint(fixture.destroys, ==, 1);
    g_assert_cmpuint(fixture.closes, ==, 1);
    g_assert_cmpuint(fixture.releases, ==, 1);
    g_assert_cmpuint(fixture.abandons, ==, 0);
}

static void test_quarantined_shutdown_abandons_without_api_calls(void)
{
    InteropFixture fixture = {
        .unregister_result = true,
        .close_result = true,
    };
    XemuWin32DxgiInteropOwnership ownership =
        XEMU_WIN32_DXGI_INTEROP_QUARANTINED;

    g_assert_cmpint(xemu_win32_dxgi_interop_shutdown(
                        &shutdown_ops, &ownership, true, true, &fixture),
                    ==, XEMU_WIN32_DXGI_INTEROP_SHUTDOWN_QUARANTINED);
    g_assert_cmpuint(fixture.unregisters, ==, 0);
    g_assert_cmpuint(fixture.destroys, ==, 0);
    g_assert_cmpuint(fixture.closes, ==, 0);
    g_assert_cmpuint(fixture.releases, ==, 0);
    g_assert_cmpuint(fixture.abandons, ==, 1);
}

static void test_close_failure_abandons_and_blocks_reinit(void)
{
    InteropFixture fixture = { .unregister_result = true };
    XemuWin32DxgiInteropOwnership ownership = XEMU_WIN32_DXGI_INTEROP_UNLOCKED;

    g_assert_cmpint(xemu_win32_dxgi_interop_shutdown(
                        &shutdown_ops, &ownership, true, true, &fixture),
                    ==, XEMU_WIN32_DXGI_INTEROP_SHUTDOWN_CLOSE_FAILED);
    g_assert_cmpint(ownership, ==, XEMU_WIN32_DXGI_INTEROP_QUARANTINED);
    g_assert_cmpuint(fixture.unregisters, ==, 1);
    g_assert_cmpuint(fixture.destroys, ==, 1);
    g_assert_cmpuint(fixture.closes, ==, 1);
    g_assert_cmpuint(fixture.releases, ==, 0);
    g_assert_cmpuint(fixture.abandons, ==, 1);

    PresentFixture present_fixture = { .quarantined = true };
    XemuWin32DxgiPresentState state = XEMU_WIN32_DXGI_PRESENT_STATE_INIT;
    g_assert_cmpint(xemu_win32_dxgi_present_prepare(
                        &state, &present_ops, &present_fixture, true,
                        XEMU_WIN32_CAPTURE_NONE),
                    ==, XEMU_WIN32_PRESENT_QUARANTINED);
    g_assert_cmpuint(present_fixture.inits, ==, 0);
}

static void test_hook_init_failure_never_uses_sdl(void)
{
    PresentFixture fixture = {};
    XemuWin32DxgiPresentState state = XEMU_WIN32_DXGI_PRESENT_STATE_INIT;

    g_assert_cmpint(xemu_win32_dxgi_present_prepare(
                        &state, &present_ops, &fixture, false,
                        XEMU_WIN32_CAPTURE_HOOK_MODULE_PRESENT),
                    ==, XEMU_WIN32_PRESENT_DROP);
    g_assert_cmpuint(fixture.inits, ==, 1);
    g_assert_cmpuint(fixture.begins, ==, 0);

    /* A failed hook-safe route is not retried every frame. */
    g_assert_cmpint(xemu_win32_dxgi_present_prepare(
                        &state, &present_ops, &fixture, false,
                        XEMU_WIN32_CAPTURE_HOOK_MODULE_PRESENT),
                    ==, XEMU_WIN32_PRESENT_DROP);
    g_assert_cmpuint(fixture.inits, ==, 1);
}

static void test_obs_hint_init_failure_may_use_sdl(void)
{
    PresentFixture fixture = {};
    XemuWin32DxgiPresentState state = XEMU_WIN32_DXGI_PRESENT_STATE_INIT;

    g_assert_cmpint(xemu_win32_dxgi_present_prepare(
                        &state, &present_ops, &fixture, false,
                        XEMU_WIN32_CAPTURE_OBS_PROCESS_HINT),
                    ==, XEMU_WIN32_PRESENT_SDL);
    g_assert_cmpuint(fixture.inits, ==, 1);
}

static void test_obs_hint_quarantine_never_uses_sdl(void)
{
    PresentFixture fixture = { .quarantine_on_init = true };
    XemuWin32DxgiPresentState state = XEMU_WIN32_DXGI_PRESENT_STATE_INIT;

    g_assert_cmpint(xemu_win32_dxgi_present_prepare(
                        &state, &present_ops, &fixture, false,
                        XEMU_WIN32_CAPTURE_OBS_PROCESS_HINT),
                    ==, XEMU_WIN32_PRESENT_QUARANTINED);
    g_assert_cmpuint(fixture.inits, ==, 1);
}

static void test_active_failure_during_capture_never_uses_sdl(void)
{
    PresentFixture fixture = {
        .init_result = true,
        .begin_result = true,
    };
    XemuWin32DxgiPresentState state = XEMU_WIN32_DXGI_PRESENT_STATE_INIT;
    XemuWin32PresentRoute route = xemu_win32_dxgi_present_prepare(
        &state, &present_ops, &fixture, false,
        XEMU_WIN32_CAPTURE_HOOK_MODULE_PRESENT);
    g_assert_cmpint(route, ==, XEMU_WIN32_PRESENT_DXGI);

    g_assert_cmpint(xemu_win32_dxgi_present_finish(
                        &state, &present_ops, &fixture, route,
                        XEMU_WIN32_CAPTURE_HOOK_MODULE_PRESENT),
                    ==, XEMU_WIN32_PRESENT_DROP);
    g_assert_cmpuint(fixture.ends, ==, 1);
}

static void test_late_hook_drops_once_then_uses_dxgi(void)
{
    PresentFixture fixture = {
        .init_result = true,
        .begin_result = true,
        .end_result = true,
    };
    XemuWin32DxgiPresentState state = XEMU_WIN32_DXGI_PRESENT_STATE_INIT;
    XemuWin32PresentRoute route = xemu_win32_dxgi_present_prepare(
        &state, &present_ops, &fixture, false, XEMU_WIN32_CAPTURE_NONE);
    g_assert_cmpint(route, ==, XEMU_WIN32_PRESENT_SDL);

    route = xemu_win32_dxgi_present_finish(
        &state, &present_ops, &fixture, route,
        XEMU_WIN32_CAPTURE_HOOK_MODULE_PRESENT);
    g_assert_cmpint(route, ==, XEMU_WIN32_PRESENT_DROP);
    g_assert_cmpuint(fixture.inits, ==, 1);
    g_assert_cmpuint(fixture.begins, ==, 0);

    route = xemu_win32_dxgi_present_prepare(
        &state, &present_ops, &fixture, false,
        XEMU_WIN32_CAPTURE_HOOK_MODULE_PRESENT);
    g_assert_cmpint(route, ==, XEMU_WIN32_PRESENT_DXGI);
    g_assert_cmpuint(fixture.begins, ==, 1);
}

static void test_resize_failure_rejects_dxgi_frame(void)
{
    PresentFixture fixture = {
        .init_result = true,
        .begin_result = false,
    };
    XemuWin32DxgiPresentState state = XEMU_WIN32_DXGI_PRESENT_STATE_INIT;

    g_assert_cmpint(xemu_win32_dxgi_present_prepare(
                        &state, &present_ops, &fixture, true,
                        XEMU_WIN32_CAPTURE_NONE),
                    ==, XEMU_WIN32_PRESENT_SDL);
    g_assert_cmpuint(fixture.begins, ==, 1);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xemu/win32-dxgi/success", test_successful_transfer);
    g_test_add_func("/xemu/win32-dxgi/lock-failure",
                    test_lock_failure_stops_transfer);
    g_test_add_func("/xemu/win32-dxgi/unlock-failure",
                    test_unlock_failure_blocks_d3d);
    g_test_add_func("/xemu/win32-dxgi/non-unlocked-entry",
                    test_non_unlocked_entry_stops_transfer);
    g_test_add_func("/xemu/win32-dxgi/release-success",
                    test_release_success_allows_resize);
    g_test_add_func("/xemu/win32-dxgi/uncertain-release",
                    test_uncertain_release_preserves_resources);
    g_test_add_func("/xemu/win32-dxgi/unregister-failure",
                    test_unregister_failure_preserves_resources);
    g_test_add_func("/xemu/win32-dxgi/shutdown-normal",
                    test_normal_shutdown_releases_once);
    g_test_add_func("/xemu/win32-dxgi/shutdown-quarantined",
                    test_quarantined_shutdown_abandons_without_api_calls);
    g_test_add_func("/xemu/win32-dxgi/shutdown-close-failure",
                    test_close_failure_abandons_and_blocks_reinit);
    g_test_add_func("/xemu/win32-dxgi/present-hook-init-failure",
                    test_hook_init_failure_never_uses_sdl);
    g_test_add_func("/xemu/win32-dxgi/present-obs-init-failure",
                    test_obs_hint_init_failure_may_use_sdl);
    g_test_add_func("/xemu/win32-dxgi/present-obs-init-quarantine",
                    test_obs_hint_quarantine_never_uses_sdl);
    g_test_add_func("/xemu/win32-dxgi/present-active-failure",
                    test_active_failure_during_capture_never_uses_sdl);
    g_test_add_func("/xemu/win32-dxgi/present-late-hook",
                    test_late_hook_drops_once_then_uses_dxgi);
    g_test_add_func("/xemu/win32-dxgi/present-resize-failure",
                    test_resize_failure_rejects_dxgi_frame);
    return g_test_run();
}
