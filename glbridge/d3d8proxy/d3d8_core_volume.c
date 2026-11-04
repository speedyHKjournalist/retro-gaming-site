#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include "d3d8_core_volume.h"
#include "d3d8_core_view.h"

typedef struct D8CoreVolume {
    IDirect3DVolume8 iface;
    struct IDirect3DVolume9 *backend;
    const D9WGVolumeMethods8 *methods;
    IDirect3DDevice8 *device;
    IUnknown *container;
} D8CoreVolume;
static IDirect3DVolume8Vtbl g_core_volume8_vtbl;

static BOOL core_volume8_guid(REFIID a, REFIID b)
{
    return d8wg_core_guid8(a, b);
}
static ULONG WINAPI core_volume8_add_ref(IDirect3DVolume8 *iface)
{
    return IUnknown_AddRef(((D8CoreVolume *)iface)->container);
}
static ULONG WINAPI core_volume8_release(IDirect3DVolume8 *iface)
{
    return IUnknown_Release(((D8CoreVolume *)iface)->container);
}
static HRESULT WINAPI core_volume8_query(IDirect3DVolume8 *iface,
        REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!core_volume8_guid(iid, &IID_IUnknown)
            && !core_volume8_guid(iid, &IID_IDirect3DVolume8))
        return E_NOINTERFACE;
    *out = iface;
    core_volume8_add_ref(iface);
    return S_OK;
}
static HRESULT WINAPI core_volume8_get_device(IDirect3DVolume8 *iface,
        IDirect3DDevice8 **out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = ((D8CoreVolume *)iface)->device;
    IDirect3DDevice8_AddRef(*out);
    return D3D_OK;
}
static HRESULT WINAPI core_volume8_set_private(IDirect3DVolume8 *iface,
        REFGUID guid, const void *data, DWORD size, DWORD flags)
{
    D8CoreVolume *surface = (D8CoreVolume *)iface;
    return surface->methods->set_private_data(surface->backend, guid, data, size, flags);
}
static HRESULT WINAPI core_volume8_get_private(IDirect3DVolume8 *iface,
        REFGUID guid, void *data, DWORD *size)
{
    D8CoreVolume *surface = (D8CoreVolume *)iface;
    return surface->methods->get_private_data(surface->backend, guid, data, size);
}
static HRESULT WINAPI core_volume8_free_private(IDirect3DVolume8 *iface,
        REFGUID guid)
{
    D8CoreVolume *surface = (D8CoreVolume *)iface;
    return surface->methods->free_private_data(surface->backend, guid);
}
static HRESULT WINAPI core_volume8_get_container(IDirect3DVolume8 *iface,
        REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid) return E_NOINTERFACE;
    return IUnknown_QueryInterface(((D8CoreVolume *)iface)->container, iid, out);
}
static HRESULT WINAPI core_volume8_get_desc(IDirect3DVolume8 *iface,
        D3DVOLUME_DESC *out)
{
    D8CoreVolume *surface = (D8CoreVolume *)iface;
    return surface->methods->get_desc(surface->backend, out, sizeof(*out));
}
static HRESULT WINAPI core_volume8_lock(IDirect3DVolume8 *iface,
        D3DLOCKED_BOX *locked, const D3DBOX *rect, DWORD flags)
{
    D8CoreVolume *surface = (D8CoreVolume *)iface;
    return surface->methods->lock_box(surface->backend, locked, rect, flags);
}
static HRESULT WINAPI core_volume8_unlock(IDirect3DVolume8 *iface)
{
    D8CoreVolume *surface = (D8CoreVolume *)iface;
    return surface->methods->unlock_box(surface->backend);
}
static IDirect3DVolume8Vtbl g_core_volume8_vtbl = {
    .QueryInterface = core_volume8_query,
    .AddRef = core_volume8_add_ref,
    .Release = core_volume8_release,
    .GetDevice = core_volume8_get_device,
    .SetPrivateData = core_volume8_set_private,
    .GetPrivateData = core_volume8_get_private,
    .FreePrivateData = core_volume8_free_private,
    .GetContainer = core_volume8_get_container,
    .GetDesc = core_volume8_get_desc,
    .LockBox = core_volume8_lock,
    .UnlockBox = core_volume8_unlock
};
static void WINAPI core_volume8_retired(void *view)
{
    HeapFree(GetProcessHeap(), 0, view);
}
HRESULT WINAPI d8wg_core_wrap_volume8(struct IDirect3DVolume9 *owned,
        IDirect3DDevice8 *device, IUnknown *container,
        IDirect3DVolume8 **out)
{
    HRESULT hr = D3DERR_INVALIDCALL;
    D8CoreVolume *view = NULL;
    if (out) *out = NULL;
    if (!out || !owned || !device || !container) goto done;
    const D9WGVolumeMethods8 *methods = d9wg_d3d9_core_volume_methods8(owned);
    if (!methods) goto done;
    void *cached = NULL;
    hr = d9wg_d3d9_core_get_view8((IUnknown *)owned, &cached);
    if (FAILED(hr)) goto done;
    view = (D8CoreVolume *)cached;
    if (!view) {
        view = (D8CoreVolume *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                sizeof(*view));
        if (!view) { hr = E_OUTOFMEMORY; goto done; }
        view->iface.lpVtbl = &g_core_volume8_vtbl;
        view->backend = owned;
        view->methods = methods;
        view->device = device;
        view->container = container;
        hr = d9wg_d3d9_core_attach_view8((IUnknown *)owned, view, core_volume8_retired);
        if (FAILED(hr)) {
            HeapFree(GetProcessHeap(), 0, view);
            view = NULL;
            /* A concurrent constructor may have published the same view. */
            if (hr != D3DERR_INVALIDCALL) goto done;
            hr = d9wg_d3d9_core_get_view8((IUnknown *)owned, &cached);
            if (FAILED(hr)) goto done;
            if (!cached) { hr = D3DERR_INVALIDCALL; goto done; }
            view = (D8CoreVolume *)cached;
        }
    }
    if (view->iface.lpVtbl != &g_core_volume8_vtbl || view->device != device
            || view->container != container) {
        hr = D3DERR_INVALIDCALL;
        goto done;
    }
    core_volume8_add_ref(&view->iface);
    *out = &view->iface;
    hr = D3D_OK;
done:
    /* Fold the getter's extra reference into the wrapper's single storage
     * reference (or its coupled frontend container),on success and failure. */
    if (owned) IUnknown_Release((IUnknown *)owned);
    return hr;
}
