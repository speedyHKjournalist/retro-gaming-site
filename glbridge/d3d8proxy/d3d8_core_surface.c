#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include "d3d8_core_surface.h"
#include "d3d8_core_view.h"

typedef struct D8CoreSurface {
    IDirect3DSurface8 iface;
    LONG refcount;
    struct IDirect3DSurface9 *backend;
    const D9WGSurfaceMethods8 *methods;
    IDirect3DDevice8 *device;
    IUnknown *container;
    BOOL container_refcount;
} D8CoreSurface;
static IDirect3DSurface8Vtbl g_core_surface8_vtbl;

static BOOL core_surface8_guid(REFIID a, REFIID b)
{
    return d8wg_core_guid8(a, b);
}
static ULONG WINAPI core_surface8_add_ref(IDirect3DSurface8 *iface)
{
    D8CoreSurface *surface = (D8CoreSurface *)iface;
    if (surface->container_refcount) return IUnknown_AddRef(surface->container);
    return d8wg_core_view_add8(&surface->refcount,
            (IUnknown *)surface->backend, surface->device);
}
static ULONG WINAPI core_surface8_release(IDirect3DSurface8 *iface)
{
    D8CoreSurface *surface = (D8CoreSurface *)iface;
    if (surface->container_refcount) return IUnknown_Release(surface->container);
    return d8wg_core_view_release8(&surface->refcount,
            (IUnknown *)surface->backend, surface->device);
}
static HRESULT WINAPI core_surface8_query(IDirect3DSurface8 *iface,
        REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    /* Preserve the current frontend8 query policy,including Resource8. */
    if (!core_surface8_guid(iid, &IID_IUnknown)
            && !core_surface8_guid(iid, &IID_IDirect3DResource8)
            && !core_surface8_guid(iid, &IID_IDirect3DSurface8))
        return E_NOINTERFACE;
    *out = iface;
    core_surface8_add_ref(iface);
    return S_OK;
}
static HRESULT WINAPI core_surface8_get_device(IDirect3DSurface8 *iface,
        IDirect3DDevice8 **out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = ((D8CoreSurface *)iface)->device;
    IDirect3DDevice8_AddRef(*out);
    return D3D_OK;
}
static HRESULT WINAPI core_surface8_set_private(IDirect3DSurface8 *iface,
        REFGUID guid, const void *data, DWORD size, DWORD flags)
{
    D8CoreSurface *surface = (D8CoreSurface *)iface;
    return surface->methods->set_private_data(surface->backend, guid, data, size, flags);
}
static HRESULT WINAPI core_surface8_get_private(IDirect3DSurface8 *iface,
        REFGUID guid, void *data, DWORD *size)
{
    D8CoreSurface *surface = (D8CoreSurface *)iface;
    return surface->methods->get_private_data(surface->backend, guid, data, size);
}
static HRESULT WINAPI core_surface8_free_private(IDirect3DSurface8 *iface,
        REFGUID guid)
{
    D8CoreSurface *surface = (D8CoreSurface *)iface;
    return surface->methods->free_private_data(surface->backend, guid);
}
static HRESULT WINAPI core_surface8_get_container(IDirect3DSurface8 *iface,
        REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid) return E_NOINTERFACE;
    return IUnknown_QueryInterface(((D8CoreSurface *)iface)->container, iid, out);
}
static HRESULT WINAPI core_surface8_get_desc(IDirect3DSurface8 *iface,
        D3DSURFACE_DESC *out)
{
    D8CoreSurface *surface = (D8CoreSurface *)iface;
    return surface->methods->get_desc(surface->backend, out, sizeof(*out));
}
static HRESULT WINAPI core_surface8_lock(IDirect3DSurface8 *iface,
        D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags)
{
    D8CoreSurface *surface = (D8CoreSurface *)iface;
    return surface->methods->lock_rect(surface->backend, locked, rect, flags);
}
static HRESULT WINAPI core_surface8_unlock(IDirect3DSurface8 *iface)
{
    D8CoreSurface *surface = (D8CoreSurface *)iface;
    return surface->methods->unlock_rect(surface->backend);
}
static IDirect3DSurface8Vtbl g_core_surface8_vtbl = {
    .QueryInterface = core_surface8_query,
    .AddRef = core_surface8_add_ref,
    .Release = core_surface8_release,
    .GetDevice = core_surface8_get_device,
    .SetPrivateData = core_surface8_set_private,
    .GetPrivateData = core_surface8_get_private,
    .FreePrivateData = core_surface8_free_private,
    .GetContainer = core_surface8_get_container,
    .GetDesc = core_surface8_get_desc,
    .LockRect = core_surface8_lock,
    .UnlockRect = core_surface8_unlock
};
static void WINAPI core_surface8_retired(void *view)
{
    HeapFree(GetProcessHeap(), 0, view);
}
HRESULT WINAPI d8wg_core_wrap_surface8(struct IDirect3DSurface9 *owned,
        IDirect3DDevice8 *device, IUnknown *container, BOOL container_refcount,
        IDirect3DSurface8 **out)
{
    HRESULT hr = D3DERR_INVALIDCALL;
    D8CoreSurface *view = NULL;
    if (out) *out = NULL;
    if (!out || !owned || !device || !container) goto done;
    const D9WGSurfaceMethods8 *methods = d9wg_d3d9_core_surface_methods8(owned);
    if (!methods) goto done;
    void *cached = NULL;
    hr = d9wg_d3d9_core_get_view8((IUnknown *)owned, &cached);
    if (FAILED(hr)) goto done;
    view = (D8CoreSurface *)cached;
    if (!view) {
        view = (D8CoreSurface *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                sizeof(*view));
        if (!view) { hr = E_OUTOFMEMORY; goto done; }
        view->iface.lpVtbl = &g_core_surface8_vtbl;
        view->backend = owned;
        view->methods = methods;
        view->device = device;
        view->container = container;
        view->container_refcount = !!container_refcount;
        hr = d9wg_d3d9_core_attach_view8((IUnknown *)owned, view, core_surface8_retired);
        if (FAILED(hr)) {
            HeapFree(GetProcessHeap(), 0, view);
            view = NULL;
            /* A concurrent constructor may have published the same view. */
            if (hr != D3DERR_INVALIDCALL) goto done;
            hr = d9wg_d3d9_core_get_view8((IUnknown *)owned, &cached);
            if (FAILED(hr)) goto done;
            if (!cached) { hr = D3DERR_INVALIDCALL; goto done; }
            view = (D8CoreSurface *)cached;
        }
    }
    if (view->iface.lpVtbl != &g_core_surface8_vtbl || view->device != device
            || view->container != container
            || view->container_refcount != !!container_refcount) {
        hr = D3DERR_INVALIDCALL;
        goto done;
    }
    core_surface8_add_ref(&view->iface);
    *out = &view->iface;
    hr = D3D_OK;
done:
    /* Fold the getter's extra reference into the wrapper's single storage
     * reference (or its coupled frontend container),on success and failure. */
    if (owned) IUnknown_Release((IUnknown *)owned);
    return hr;
}

/* Borrowed while the public view or its coupled container is retained. */
struct IDirect3DSurface9 *WINAPI d8wg_core_surface_backend8(IDirect3DSurface8 *iface)
{
    return iface && iface->lpVtbl == &g_core_surface8_vtbl
            ? ((D8CoreSurface *)iface)->backend : NULL;
}
