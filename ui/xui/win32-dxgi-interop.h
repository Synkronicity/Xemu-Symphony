/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef XEMU_WIN32_DXGI_INTEROP_H
#define XEMU_WIN32_DXGI_INTEROP_H

#include <stdbool.h>

typedef enum XemuWin32DxgiInteropResult {
    XEMU_WIN32_DXGI_INTEROP_TRANSFERRED,
    XEMU_WIN32_DXGI_INTEROP_NOT_READY,
    XEMU_WIN32_DXGI_INTEROP_LOCK_FAILED,
    XEMU_WIN32_DXGI_INTEROP_UNLOCK_FAILED,
} XemuWin32DxgiInteropResult;

typedef enum XemuWin32DxgiInteropOwnership {
    XEMU_WIN32_DXGI_INTEROP_UNLOCKED,
    XEMU_WIN32_DXGI_INTEROP_LOCKED_BY_OPENGL,
    XEMU_WIN32_DXGI_INTEROP_QUARANTINED,
} XemuWin32DxgiInteropOwnership;

typedef enum XemuWin32DxgiInteropReleaseResult {
    XEMU_WIN32_DXGI_INTEROP_RELEASED,
    XEMU_WIN32_DXGI_INTEROP_OWNERSHIP_UNCERTAIN,
    XEMU_WIN32_DXGI_INTEROP_UNREGISTER_FAILED,
} XemuWin32DxgiInteropReleaseResult;

typedef struct XemuWin32DxgiInteropOps {
    bool (*lock)(void *opaque);
    void (*blit)(void *opaque);
    bool (*unlock)(void *opaque);
    void (*use_from_d3d)(void *opaque);
} XemuWin32DxgiInteropOps;

typedef struct XemuWin32DxgiInteropReleaseOps {
    bool (*unregister)(void *opaque);
    void (*destroy)(void *opaque);
} XemuWin32DxgiInteropReleaseOps;

static inline XemuWin32DxgiInteropResult
xemu_win32_dxgi_interop_transfer(const XemuWin32DxgiInteropOps *ops,
                                 XemuWin32DxgiInteropOwnership *ownership,
                                 void *opaque)
{
    if (*ownership != XEMU_WIN32_DXGI_INTEROP_UNLOCKED) {
        return XEMU_WIN32_DXGI_INTEROP_NOT_READY;
    }

    if (!ops->lock(opaque)) {
        return XEMU_WIN32_DXGI_INTEROP_LOCK_FAILED;
    }
    *ownership = XEMU_WIN32_DXGI_INTEROP_LOCKED_BY_OPENGL;

    ops->blit(opaque);

    if (!ops->unlock(opaque)) {
        *ownership = XEMU_WIN32_DXGI_INTEROP_QUARANTINED;
        return XEMU_WIN32_DXGI_INTEROP_UNLOCK_FAILED;
    }
    *ownership = XEMU_WIN32_DXGI_INTEROP_UNLOCKED;

    ops->use_from_d3d(opaque);
    return XEMU_WIN32_DXGI_INTEROP_TRANSFERRED;
}

static inline XemuWin32DxgiInteropReleaseResult
xemu_win32_dxgi_interop_release(const XemuWin32DxgiInteropReleaseOps *ops,
                                XemuWin32DxgiInteropOwnership *ownership,
                                bool registered, void *opaque)
{
    if (*ownership != XEMU_WIN32_DXGI_INTEROP_UNLOCKED) {
        return XEMU_WIN32_DXGI_INTEROP_OWNERSHIP_UNCERTAIN;
    }

    if (registered && !ops->unregister(opaque)) {
        *ownership = XEMU_WIN32_DXGI_INTEROP_QUARANTINED;
        return XEMU_WIN32_DXGI_INTEROP_UNREGISTER_FAILED;
    }

    ops->destroy(opaque);
    return XEMU_WIN32_DXGI_INTEROP_RELEASED;
}

#endif /* XEMU_WIN32_DXGI_INTEROP_H */
