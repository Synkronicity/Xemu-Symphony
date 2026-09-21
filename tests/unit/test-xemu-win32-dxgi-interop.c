/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "ui/xui/win32-dxgi-interop.h"

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
    return g_test_run();
}
