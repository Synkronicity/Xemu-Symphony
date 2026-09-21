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

typedef enum XemuWin32DxgiInteropShutdownResult {
    XEMU_WIN32_DXGI_INTEROP_SHUTDOWN_COMPLETE,
    XEMU_WIN32_DXGI_INTEROP_SHUTDOWN_QUARANTINED,
    XEMU_WIN32_DXGI_INTEROP_SHUTDOWN_UNREGISTER_FAILED,
    XEMU_WIN32_DXGI_INTEROP_SHUTDOWN_CLOSE_FAILED,
} XemuWin32DxgiInteropShutdownResult;

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

typedef struct XemuWin32DxgiInteropShutdownOps {
    bool (*unregister)(void *opaque);
    void (*destroy)(void *opaque);
    bool (*close)(void *opaque);
    void (*release_owners)(void *opaque);
    void (*abandon_owners)(void *opaque);
} XemuWin32DxgiInteropShutdownOps;

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

/*
 * Shut down while the OpenGL context is current. If ownership cannot be
 * proven released, detach the process-lifetime owners instead of invoking
 * more GL/WGL/D3D APIs or allowing automatic COM destruction.
 */
static inline XemuWin32DxgiInteropShutdownResult
xemu_win32_dxgi_interop_shutdown(const XemuWin32DxgiInteropShutdownOps *ops,
                                 XemuWin32DxgiInteropOwnership *ownership,
                                 bool registered, bool device_open,
                                 void *opaque)
{
    if (*ownership != XEMU_WIN32_DXGI_INTEROP_UNLOCKED) {
        *ownership = XEMU_WIN32_DXGI_INTEROP_QUARANTINED;
        ops->abandon_owners(opaque);
        return XEMU_WIN32_DXGI_INTEROP_SHUTDOWN_QUARANTINED;
    }

    if (registered && !ops->unregister(opaque)) {
        *ownership = XEMU_WIN32_DXGI_INTEROP_QUARANTINED;
        ops->abandon_owners(opaque);
        return XEMU_WIN32_DXGI_INTEROP_SHUTDOWN_UNREGISTER_FAILED;
    }

    ops->destroy(opaque);

    if (device_open && !ops->close(opaque)) {
        *ownership = XEMU_WIN32_DXGI_INTEROP_QUARANTINED;
        ops->abandon_owners(opaque);
        return XEMU_WIN32_DXGI_INTEROP_SHUTDOWN_CLOSE_FAILED;
    }

    ops->release_owners(opaque);
    return XEMU_WIN32_DXGI_INTEROP_SHUTDOWN_COMPLETE;
}

#endif /* XEMU_WIN32_DXGI_INTEROP_H */
