/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef XEMU_WIN32_DXGI_PRESENT_STATE_H
#define XEMU_WIN32_DXGI_PRESENT_STATE_H

#include <stdbool.h>

typedef enum XemuWin32CaptureEvidence {
    XEMU_WIN32_CAPTURE_NONE,
    XEMU_WIN32_CAPTURE_OBS_PROCESS_HINT,
    XEMU_WIN32_CAPTURE_HOOK_MODULE_PRESENT,
} XemuWin32CaptureEvidence;

typedef enum XemuWin32PresentRoute {
    XEMU_WIN32_PRESENT_SDL,
    XEMU_WIN32_PRESENT_DXGI,
    XEMU_WIN32_PRESENT_DROP,
    XEMU_WIN32_PRESENT_QUARANTINED,
} XemuWin32PresentRoute;

typedef struct XemuWin32DxgiPresentState {
    bool init_attempted;
    bool active;
    bool requested;
    XemuWin32CaptureEvidence evidence;
} XemuWin32DxgiPresentState;

#define XEMU_WIN32_DXGI_PRESENT_STATE_INIT \
    { false, false, false, XEMU_WIN32_CAPTURE_NONE }

typedef struct XemuWin32DxgiPresentOps {
    bool (*init)(void *opaque);
    bool (*begin_frame)(void *opaque);
    bool (*end_frame)(void *opaque);
    void (*shutdown)(void *opaque);
    bool (*is_quarantined)(void *opaque);
} XemuWin32DxgiPresentOps;

static inline void xemu_win32_dxgi_present_observe(
    XemuWin32DxgiPresentState *state, XemuWin32CaptureEvidence evidence)
{
    if (evidence > state->evidence) {
        state->evidence = evidence;
    }
}

static inline XemuWin32PresentRoute xemu_win32_dxgi_present_inactive_route(
    const XemuWin32DxgiPresentState *state)
{
    if (state->evidence == XEMU_WIN32_CAPTURE_HOOK_MODULE_PRESENT) {
        return XEMU_WIN32_PRESENT_DROP;
    }
    return XEMU_WIN32_PRESENT_SDL;
}

static inline XemuWin32PresentRoute xemu_win32_dxgi_present_prepare(
    XemuWin32DxgiPresentState *state, const XemuWin32DxgiPresentOps *ops,
    void *opaque, bool requested_by_vsync,
    XemuWin32CaptureEvidence evidence)
{
    xemu_win32_dxgi_present_observe(state, evidence);
    bool requested = requested_by_vsync ||
                     state->evidence != XEMU_WIN32_CAPTURE_NONE;

    if (ops->is_quarantined(opaque)) {
        state->active = false;
        state->requested = requested;
        return XEMU_WIN32_PRESENT_QUARANTINED;
    }

    if (!requested) {
        if (state->requested || state->active) {
            ops->shutdown(opaque);
        }
        if (ops->is_quarantined(opaque)) {
            state->active = false;
            state->requested = false;
            return XEMU_WIN32_PRESENT_QUARANTINED;
        }
        state->init_attempted = false;
        state->active = false;
        state->requested = false;
        return XEMU_WIN32_PRESENT_SDL;
    }

    state->requested = true;
    if (!state->active && !state->init_attempted) {
        state->init_attempted = true;
        state->active = ops->init(opaque);
    }

    if (!state->active) {
        if (ops->is_quarantined(opaque)) {
            return XEMU_WIN32_PRESENT_QUARANTINED;
        }
        return xemu_win32_dxgi_present_inactive_route(state);
    }

    if (!ops->begin_frame(opaque)) {
        state->active = false;
        if (ops->is_quarantined(opaque)) {
            return XEMU_WIN32_PRESENT_QUARANTINED;
        }
        return xemu_win32_dxgi_present_inactive_route(state);
    }

    return XEMU_WIN32_PRESENT_DXGI;
}

static inline XemuWin32PresentRoute xemu_win32_dxgi_present_finish(
    XemuWin32DxgiPresentState *state, const XemuWin32DxgiPresentOps *ops,
    void *opaque, XemuWin32PresentRoute route,
    XemuWin32CaptureEvidence late_evidence)
{
    xemu_win32_dxgi_present_observe(state, late_evidence);

    if (route == XEMU_WIN32_PRESENT_DXGI) {
        if (ops->end_frame(opaque)) {
            return route;
        }
        state->active = false;
        return ops->is_quarantined(opaque) ?
                   XEMU_WIN32_PRESENT_QUARANTINED :
                   XEMU_WIN32_PRESENT_DROP;
    }

    if (route == XEMU_WIN32_PRESENT_SDL &&
        state->evidence == XEMU_WIN32_CAPTURE_HOOK_MODULE_PRESENT) {
        /*
         * The frame was rendered to SDL's default framebuffer. Initialize
         * DXGI for the next frame, but do not enter a newly installed
         * SwapBuffers hook with the current one.
         */
        if (!state->active && !state->init_attempted) {
            state->init_attempted = true;
            state->requested = true;
            state->active = ops->init(opaque);
        }
        return ops->is_quarantined(opaque) ?
                   XEMU_WIN32_PRESENT_QUARANTINED :
                   XEMU_WIN32_PRESENT_DROP;
    }

    return route;
}

#endif /* XEMU_WIN32_DXGI_PRESENT_STATE_H */
