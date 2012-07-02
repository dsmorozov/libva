/*
 * Copyright (c) 2012 Intel Corporation. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "sysdeps.h"
#include <xf86drm.h>
#include "va_drm.h"
#include "va_backend.h"
#include "va_backend_drm.h"
#include "va_drmcommon.h"
#include "va_drm_auth.h"

static int
va_DisplayContextIsValid(VADisplayContextP pDisplayContext)
{
    VADriverContextP const pDriverContext = pDisplayContext->pDriverContext;

    return (pDriverContext &&
            pDriverContext->display_type == VA_DISPLAY_DRM);
}

static void
va_DisplayContextDestroy(VADisplayContextP pDisplayContext)
{
    if (!pDisplayContext)
        return;

    free(pDisplayContext->pDriverContext->vtable_drm);
    free(pDisplayContext->pDriverContext->drm_state);
    free(pDisplayContext->pDriverContext);
    free(pDisplayContext);
}

struct driver_name_map {
    const char *key;
    int         key_len;
    const char *name;
};

static const struct driver_name_map g_driver_name_map[] = {
    { "i915",       4, "i965"   }, // Intel OTC GenX driver
    { "pvrsrvkm",   8, "pvr"    }, // Intel UMG PVR driver
    { "emgd",       4, "emgd"   }, // Intel ECG PVR driver
    { NULL, }
};

static VAStatus
va_DisplayContextGetDriverName(
    VADisplayContextP pDisplayContext,
    char            **driver_name_ptr
)
{

    VADriverContextP const ctx = pDisplayContext->pDriverContext;
    struct drm_state * const drm_state = ctx->drm_state;
    drmVersionPtr drm_version;
    char *driver_name = NULL;
    const struct driver_name_map *m;
    drm_magic_t magic;
    int ret;

    *driver_name_ptr = NULL;

    drm_version = drmGetVersion(drm_state->fd);
    if (!drm_version)
        return VA_STATUS_ERROR_UNKNOWN;

    for (m = g_driver_name_map; m->key != NULL; m++) {
        if (drm_version->name_len >= m->key_len &&
            strncmp(drm_version->name, m->key, m->key_len) == 0)
            break;
    }
    drmFreeVersion(drm_version);

    if (!m->name)
        return VA_STATUS_ERROR_UNKNOWN;

    driver_name = strdup(m->name);
    if (!driver_name)
        return VA_STATUS_ERROR_ALLOCATION_FAILED;

    *driver_name_ptr = driver_name;

    ret = drmGetMagic(drm_state->fd, &magic);
    if (ret < 0)
        return VA_STATUS_ERROR_OPERATION_FAILED;

    if (!va_drm_is_authenticated(drm_state->fd)) {
        if (!va_drm_authenticate(drm_state->fd, magic))
            return VA_STATUS_ERROR_OPERATION_FAILED;
        if (!va_drm_is_authenticated(drm_state->fd))
            return VA_STATUS_ERROR_OPERATION_FAILED;
    }

    drm_state->auth_type = VA_DRM_AUTH_CUSTOM;

    return VA_STATUS_SUCCESS;
}

VADisplay
vaGetDisplayDRM(int fd)
{
    VADisplayContextP pDisplayContext = NULL;
    VADriverContextP  pDriverContext  = NULL;
    struct drm_state *drm_state       = NULL;
    struct VADriverVTableDRM *vtable  = NULL;

    if (fd < 0)
        return NULL;

    /* Create new entry */
    /* XXX: handle cache? */
    drm_state = calloc(1, sizeof(*drm_state));
    if (!drm_state)
        goto error;
    drm_state->fd = fd;

    pDriverContext = calloc(1, sizeof(*pDriverContext));
    if (!pDriverContext)
        goto error;
    pDriverContext->native_dpy   = NULL;
    pDriverContext->display_type = VA_DISPLAY_DRM;
    pDriverContext->drm_state    = drm_state;

    vtable = calloc(1, sizeof(*vtable));
    if (!vtable)
        goto error;
    vtable->version = VA_DRM_API_VERSION;
    pDriverContext->vtable_drm = vtable;

    pDisplayContext = calloc(1, sizeof(*pDisplayContext));
    if (!pDisplayContext)
        goto error;

    pDisplayContext->vadpy_magic     = VA_DISPLAY_MAGIC;
    pDisplayContext->pDriverContext  = pDriverContext;
    pDisplayContext->vaIsValid       = va_DisplayContextIsValid;
    pDisplayContext->vaDestroy       = va_DisplayContextDestroy;
    pDisplayContext->vaGetDriverName = va_DisplayContextGetDriverName;
    return pDisplayContext;

error:
    free(pDisplayContext);
    free(pDriverContext);
    free(drm_state);
    free(vtable);
    return NULL;
}

#define INIT_CONTEXT(ctx, dpy) do {                             \
        if (!vaDisplayIsValid(dpy))                             \
            return VA_STATUS_ERROR_INVALID_DISPLAY;             \
                                                                \
        ctx = ((VADisplayContextP)(dpy))->pDriverContext;       \
        if (!(ctx))                                             \
            return VA_STATUS_ERROR_INVALID_DISPLAY;             \
    } while (0)

#define INVOKE(ctx, func, args) do {                            \
        struct VADriverVTableDRM * const vtable =               \
            (ctx)->vtable_drm;                                  \
        if (!vtable || !vtable->va##func##DRM)                  \
            return VA_STATUS_ERROR_UNIMPLEMENTED;               \
        status = vtable->va##func##DRM args;                    \
    } while (0)

// Returns the underlying DRM buffer to the supplied VA surface
VAStatus
vaGetSurfaceBufferDRM(
    VADisplay           dpy,
    VASurfaceID         surface,
    VABufferInfoDRM    *out_buffer_info
)
{
    VADriverContextP ctx;
    VAStatus status;

    INIT_CONTEXT(ctx, dpy);

    INVOKE(ctx, GetSurfaceBuffer, (ctx, surface, out_buffer_info));
    return status;
}

// Returns the underlying DRM buffer to the supplied VA image
VAStatus
vaGetImageBufferDRM(
    VADisplay           dpy,
    VAImageID           image,
    VABufferInfoDRM    *out_buffer_info
)
{
    VADriverContextP ctx;
    VAStatus status;

    INIT_CONTEXT(ctx, dpy);

    INVOKE(ctx, GetImageBuffer, (ctx, image, out_buffer_info));
    return status;
}
