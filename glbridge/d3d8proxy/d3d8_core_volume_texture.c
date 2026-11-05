#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include "d3d8_core_volume_texture.h"
#include "d3d8_core_volume.h"
#include "d3d8_core_view.h"

typedef struct D8CoreVolumeTexture {
    IDirect3DVolumeTexture8 iface;
    LONG refcount;
    struct IDirect3DVolumeTexture9 *backend;
    const D9WGVolumeTextureMethods8 *methods;
    IDirect3DDevice8 *device;
    const void *identity;
} D8CoreVolumeTexture;
static IDirect3DVolumeTexture8Vtbl g_core_volume_texture8_vtbl;
static BOOL core_volume_texture8_guid(REFIID a, REFIID b)
{
    return d8wg_core_guid8(a, b);
}
static ULONG WINAPI core_volume_texture8_add_ref(IDirect3DVolumeTexture8 *iface)
{
    D8CoreVolumeTexture *texture = (D8CoreVolumeTexture *)iface;
    return d8wg_core_view_add8(&texture->refcount, (IUnknown *)texture->backend, texture->device);
}
static ULONG WINAPI core_volume_texture8_release(IDirect3DVolumeTexture8 *iface)
{
    D8CoreVolumeTexture *texture = (D8CoreVolumeTexture *)iface;
    return d8wg_core_view_release8(&texture->refcount, (IUnknown *)texture->backend, texture->device);
}
static HRESULT WINAPI core_volume_texture8_query(IDirect3DVolumeTexture8 *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!core_volume_texture8_guid(iid, &IID_IUnknown)
            && !core_volume_texture8_guid(iid, &IID_IDirect3DResource8)
            && !core_volume_texture8_guid(iid, &IID_IDirect3DBaseTexture8)
            && !core_volume_texture8_guid(iid, &IID_IDirect3DVolumeTexture8)) return E_NOINTERFACE;
    *out = iface;
    core_volume_texture8_add_ref(iface);
    return S_OK;
}
static HRESULT WINAPI core_volume_texture8_get_device(IDirect3DVolumeTexture8 *iface, IDirect3DDevice8 **out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = ((D8CoreVolumeTexture *)iface)->device;
    IDirect3DDevice8_AddRef(*out);
    return D3D_OK;
}
static HRESULT WINAPI core_volume_texture8_set_private(IDirect3DVolumeTexture8 *iface, REFGUID guid,
        const void *data, DWORD size, DWORD flags)
{
    D8CoreVolumeTexture *texture = (D8CoreVolumeTexture *)iface;
    return texture->methods->set_private_data(texture->backend, guid, data, size, flags);
}
static HRESULT WINAPI core_volume_texture8_get_private(IDirect3DVolumeTexture8 *iface, REFGUID guid,
        void *data, DWORD *size)
{
    D8CoreVolumeTexture *texture = (D8CoreVolumeTexture *)iface;
    return texture->methods->get_private_data(texture->backend, guid, data, size);
}
static HRESULT WINAPI core_volume_texture8_free_private(IDirect3DVolumeTexture8 *iface, REFGUID guid)
{
    D8CoreVolumeTexture *texture = (D8CoreVolumeTexture *)iface;
    return texture->methods->free_private_data(texture->backend, guid);
}
static DWORD WINAPI core_volume_texture8_set_priority(IDirect3DVolumeTexture8 *iface, DWORD priority)
{
    D8CoreVolumeTexture *texture = (D8CoreVolumeTexture *)iface;
    return texture->methods->set_priority(texture->backend, priority);
}
static DWORD WINAPI core_volume_texture8_get_priority(IDirect3DVolumeTexture8 *iface)
{
    D8CoreVolumeTexture *texture = (D8CoreVolumeTexture *)iface;
    return texture->methods->get_priority(texture->backend);
}
static void WINAPI core_volume_texture8_preload(IDirect3DVolumeTexture8 *iface)
{
    D8CoreVolumeTexture *texture = (D8CoreVolumeTexture *)iface;
    texture->methods->preload(texture->backend);
}
static D3DRESOURCETYPE WINAPI core_volume_texture8_get_type(IDirect3DVolumeTexture8 *iface)
{
    D8CoreVolumeTexture *texture = (D8CoreVolumeTexture *)iface;
    return (D3DRESOURCETYPE)texture->methods->get_type(texture->backend);
}
static DWORD WINAPI core_volume_texture8_set_lod(IDirect3DVolumeTexture8 *iface, DWORD lod)
{
    D8CoreVolumeTexture *texture = (D8CoreVolumeTexture *)iface;
    return texture->methods->set_lod(texture->backend, lod);
}
static DWORD WINAPI core_volume_texture8_get_lod(IDirect3DVolumeTexture8 *iface)
{
    D8CoreVolumeTexture *texture = (D8CoreVolumeTexture *)iface;
    return texture->methods->get_lod(texture->backend);
}
static DWORD WINAPI core_volume_texture8_get_level_count(IDirect3DVolumeTexture8 *iface)
{
    D8CoreVolumeTexture *texture = (D8CoreVolumeTexture *)iface;
    return texture->methods->get_level_count(texture->backend);
}
static HRESULT WINAPI core_volume_texture8_get_level_desc(IDirect3DVolumeTexture8 *iface, UINT level,
        D3DVOLUME_DESC *out)
{
    D8CoreVolumeTexture *texture = (D8CoreVolumeTexture *)iface;
    return texture->methods->get_level_desc(texture->backend, level, out, sizeof(*out));
}
static HRESULT WINAPI core_volume_texture8_get_volume_level(IDirect3DVolumeTexture8 *iface, UINT level,
        IDirect3DVolume8 **out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = NULL;
    D8CoreVolumeTexture *texture = (D8CoreVolumeTexture *)iface;
    struct IDirect3DVolume9 *surface = NULL;
    HRESULT hr = texture->methods->get_volume_level(texture->backend, level, &surface);
    if (FAILED(hr)) return hr;
    return d8wg_core_wrap_volume8(surface, texture->device, (IUnknown *)iface, out);
}
static HRESULT WINAPI core_volume_texture8_lock(IDirect3DVolumeTexture8 *iface, UINT level,
        D3DLOCKED_BOX *locked, const D3DBOX *rect, DWORD flags)
{
    D8CoreVolumeTexture *texture = (D8CoreVolumeTexture *)iface;
    return texture->methods->lock_box(texture->backend, level, locked, rect, flags);
}
static HRESULT WINAPI core_volume_texture8_unlock(IDirect3DVolumeTexture8 *iface, UINT level)
{
    D8CoreVolumeTexture *texture = (D8CoreVolumeTexture *)iface;
    return texture->methods->unlock_box(texture->backend, level);
}
static HRESULT WINAPI core_volume_texture8_add_dirty(IDirect3DVolumeTexture8 *iface, const D3DBOX *rect)
{
    D8CoreVolumeTexture *texture = (D8CoreVolumeTexture *)iface;
    return texture->methods->add_dirty_box(texture->backend, rect);
}
static IDirect3DVolumeTexture8Vtbl g_core_volume_texture8_vtbl = {
    .QueryInterface = core_volume_texture8_query, .AddRef = core_volume_texture8_add_ref, .Release = core_volume_texture8_release,
    .GetDevice = core_volume_texture8_get_device, .SetPrivateData = core_volume_texture8_set_private,
    .GetPrivateData = core_volume_texture8_get_private, .FreePrivateData = core_volume_texture8_free_private,
    .SetPriority = core_volume_texture8_set_priority, .GetPriority = core_volume_texture8_get_priority,
    .PreLoad = core_volume_texture8_preload, .GetType = core_volume_texture8_get_type, .SetLOD = core_volume_texture8_set_lod,
    .GetLOD = core_volume_texture8_get_lod, .GetLevelCount = core_volume_texture8_get_level_count,
    .GetLevelDesc = core_volume_texture8_get_level_desc, .GetVolumeLevel = core_volume_texture8_get_volume_level,
    .LockBox = core_volume_texture8_lock, .UnlockBox = core_volume_texture8_unlock, .AddDirtyBox = core_volume_texture8_add_dirty
};
static void WINAPI core_volume_texture8_retired(void *view)
{
    HeapFree(GetProcessHeap(), 0, view);
}
HRESULT WINAPI d8wg_core_wrap_volume_texture8(struct IDirect3DVolumeTexture9 *owned,
        IDirect3DDevice8 *device, IDirect3DVolumeTexture8 **out)
{
    HRESULT hr = D3DERR_INVALIDCALL;
    D8CoreVolumeTexture *view = NULL;
    if (out) *out = NULL;
    if (!out || !owned || !device) goto done;
    const D9WGVolumeTextureMethods8 *methods = d9wg_d3d9_core_volume_texture_methods8(owned);
    if (!methods) goto done;
    void *cached = NULL;
    hr = d9wg_d3d9_core_get_view8((IUnknown *)owned, &cached);
    if (FAILED(hr)) goto done;
    view = (D8CoreVolumeTexture *)cached;
    if (!view) {
        view = (D8CoreVolumeTexture *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*view));
        if (!view) { hr = E_OUTOFMEMORY; goto done; }
        view->iface.lpVtbl = &g_core_volume_texture8_vtbl;
        view->identity = &g_core_volume_texture8_vtbl;
        view->backend = owned;
        view->methods = methods;
        view->device = device;
        hr = d9wg_d3d9_core_attach_view8((IUnknown *)owned, view, core_volume_texture8_retired);
        if (FAILED(hr)) {
            HeapFree(GetProcessHeap(), 0, view);
            if (hr != D3DERR_INVALIDCALL) goto done;
            hr = d9wg_d3d9_core_get_view8((IUnknown *)owned, &cached);
            if (FAILED(hr)) goto done;
            if (!cached) { hr = D3DERR_INVALIDCALL; goto done; }
            view = (D8CoreVolumeTexture *)cached;
        }
    }
    if (view->identity != &g_core_volume_texture8_vtbl || view->device != device) {
        hr = D3DERR_INVALIDCALL;
        goto done;
    }
    core_volume_texture8_add_ref(&view->iface);
    *out = &view->iface;
    hr = D3D_OK;
done:
    if (owned) IUnknown_Release((IUnknown *)owned);
    return hr;
}

struct IDirect3DVolumeTexture9 *WINAPI d8wg_core_volume_texture_backend8(IDirect3DVolumeTexture8 *texture)
{
    if (!texture || ((D8CoreVolumeTexture *)texture)->identity != &g_core_volume_texture8_vtbl) return NULL;
    return ((D8CoreVolumeTexture *)texture)->backend;
}
