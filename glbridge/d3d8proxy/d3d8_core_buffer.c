#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include "d3d8_core_buffer.h"
#include "d3d8_core_view.h"

typedef struct D8CoreBuffer {
    IUnknown iface;
    LONG refcount;
    IUnknown *backend;
    const D9WGBufferMethods8 *methods;
    IDirect3DDevice8 *device;
    UINT kind;
} D8CoreBuffer;
static IDirect3DVertexBuffer8Vtbl g_core_vertex_buffer8_vtbl;
static IDirect3DIndexBuffer8Vtbl g_core_index_buffer8_vtbl;
static IUnknownVtbl *core_buffer8_vtable(UINT kind)
{
    return kind == 6u ? (IUnknownVtbl *)&g_core_vertex_buffer8_vtbl
            : (IUnknownVtbl *)&g_core_index_buffer8_vtbl;
}
static HRESULT core_buffer8_query(D8CoreBuffer *buffer, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    const GUID *class_id = buffer->kind == 6u
            ? &IID_IDirect3DVertexBuffer8 : &IID_IDirect3DIndexBuffer8;
    if (!d8wg_core_guid8(iid, &IID_IUnknown)
            && !d8wg_core_guid8(iid, &IID_IDirect3DResource8)
            && !d8wg_core_guid8(iid, class_id)) return E_NOINTERFACE;
    d8wg_core_view_add8(&buffer->refcount, buffer->backend, buffer->device);
    *out = &buffer->iface;
    return S_OK;
}
/* Only typed COM signatures differ. Both resource classes share identity,
 * reference, cache and method forwarding policy in this implementation. */
#define D8WG_BUFFER_METHODS8(P, Interface, Descriptor) \
static HRESULT WINAPI P##_query(Interface *iface, REFIID iid, void **out) \
{ return core_buffer8_query((D8CoreBuffer *)iface, iid, out); } \
static ULONG WINAPI P##_add_ref(Interface *iface) \
{ D8CoreBuffer *b = (D8CoreBuffer *)iface; return d8wg_core_view_add8(&b->refcount, b->backend, b->device); } \
static ULONG WINAPI P##_release(Interface *iface) \
{ D8CoreBuffer *b = (D8CoreBuffer *)iface; return d8wg_core_view_release8(&b->refcount, b->backend, b->device); } \
static HRESULT WINAPI P##_get_device(Interface *iface, IDirect3DDevice8 **out) \
{ if (!out) return D3DERR_INVALIDCALL; *out = ((D8CoreBuffer *)iface)->device; IDirect3DDevice8_AddRef(*out); return D3D_OK; } \
static HRESULT WINAPI P##_set_private(Interface *iface, REFGUID id, const void *data, DWORD size, DWORD flags) \
{ D8CoreBuffer *b = (D8CoreBuffer *)iface; return b->methods->set_private_data(b->backend, id, data, size, flags); } \
static HRESULT WINAPI P##_get_private(Interface *iface, REFGUID id, void *data, DWORD *size) \
{ D8CoreBuffer *b = (D8CoreBuffer *)iface; return b->methods->get_private_data(b->backend, id, data, size); } \
static HRESULT WINAPI P##_free_private(Interface *iface, REFGUID id) \
{ D8CoreBuffer *b = (D8CoreBuffer *)iface; return b->methods->free_private_data(b->backend, id); } \
static DWORD WINAPI P##_set_priority(Interface *iface, DWORD value) \
{ D8CoreBuffer *b = (D8CoreBuffer *)iface; return b->methods->set_priority(b->backend, value); } \
static DWORD WINAPI P##_get_priority(Interface *iface) \
{ D8CoreBuffer *b = (D8CoreBuffer *)iface; return b->methods->get_priority(b->backend); } \
static void WINAPI P##_preload(Interface *iface) \
{ D8CoreBuffer *b = (D8CoreBuffer *)iface; b->methods->preload(b->backend); } \
static D3DRESOURCETYPE WINAPI P##_get_type(Interface *iface) \
{ D8CoreBuffer *b = (D8CoreBuffer *)iface; return (D3DRESOURCETYPE)b->methods->get_type(b->backend); } \
static HRESULT WINAPI P##_lock(Interface *iface, UINT offset, UINT size, BYTE **out, DWORD flags) \
{ D8CoreBuffer *b = (D8CoreBuffer *)iface; return b->methods->lock(b->backend, offset, size, out, flags); } \
static HRESULT WINAPI P##_unlock(Interface *iface) \
{ D8CoreBuffer *b = (D8CoreBuffer *)iface; return b->methods->unlock(b->backend); } \
static HRESULT WINAPI P##_get_desc(Interface *iface, Descriptor *out) \
{ D8CoreBuffer *b = (D8CoreBuffer *)iface; return b->methods->get_desc(b->backend, out, sizeof(*out)); }
D8WG_BUFFER_METHODS8(core_vertex_buffer8, IDirect3DVertexBuffer8, D3DVERTEXBUFFER_DESC)
D8WG_BUFFER_METHODS8(core_index_buffer8, IDirect3DIndexBuffer8, D3DINDEXBUFFER_DESC)
#undef D8WG_BUFFER_METHODS8
#define D8WG_BUFFER_VTABLE8(P) \
    .QueryInterface=P##_query, .AddRef=P##_add_ref, .Release=P##_release, \
    .GetDevice=P##_get_device, .SetPrivateData=P##_set_private, \
    .GetPrivateData=P##_get_private, .FreePrivateData=P##_free_private, \
    .SetPriority=P##_set_priority, .GetPriority=P##_get_priority, \
    .PreLoad=P##_preload, .GetType=P##_get_type, .Lock=P##_lock, \
    .Unlock=P##_unlock, .GetDesc=P##_get_desc
static IDirect3DVertexBuffer8Vtbl g_core_vertex_buffer8_vtbl = {
    D8WG_BUFFER_VTABLE8(core_vertex_buffer8)
};
static IDirect3DIndexBuffer8Vtbl g_core_index_buffer8_vtbl = {
    D8WG_BUFFER_VTABLE8(core_index_buffer8)
};
#undef D8WG_BUFFER_VTABLE8
static void WINAPI core_buffer8_retired(void *view)
{
    HeapFree(GetProcessHeap(), 0, view);
}
static HRESULT core_buffer8_wrap(IUnknown *owned, IDirect3DDevice8 *device,
        UINT kind, void **out)
{
    HRESULT hr = D3DERR_INVALIDCALL;
    D8CoreBuffer *view = NULL;
    if (out) *out = NULL;
    if (!out || !owned || !device) goto done;
    const D9WGBufferMethods8 *methods = d9wg_d3d9_core_buffer_methods8(owned, kind);
    if (!methods) goto done;
    void *cached = NULL;
    hr = d9wg_d3d9_core_get_view8(owned, &cached);
    if (FAILED(hr)) goto done;
    view = (D8CoreBuffer *)cached;
    if (!view) {
        view = (D8CoreBuffer *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*view));
        if (!view) { hr = E_OUTOFMEMORY; goto done; }
        view->iface.lpVtbl = core_buffer8_vtable(kind);
        view->backend = owned;
        view->methods = methods;
        view->device = device;
        view->kind = kind;
        hr = d9wg_d3d9_core_attach_view8(owned, view, core_buffer8_retired);
        if (FAILED(hr)) {
            HeapFree(GetProcessHeap(), 0, view);
            if (hr != D3DERR_INVALIDCALL) goto done;
            hr = d9wg_d3d9_core_get_view8(owned, &cached);
            if (FAILED(hr)) goto done;
            if (!cached) { hr = D3DERR_INVALIDCALL; goto done; }
            view = (D8CoreBuffer *)cached;
        }
    }
    if (view->iface.lpVtbl != core_buffer8_vtable(kind)
            || view->kind != kind || view->device != device) {
        hr = D3DERR_INVALIDCALL;
        goto done;
    }
    d8wg_core_view_add8(&view->refcount, view->backend, view->device);
    *out = &view->iface;
    hr = D3D_OK;
done:
    if (owned) IUnknown_Release(owned);
    return hr;
}
HRESULT WINAPI d8wg_core_wrap_vertex_buffer8(struct IDirect3DVertexBuffer9 *owned,
        IDirect3DDevice8 *device, IDirect3DVertexBuffer8 **out)
{
    void *view = NULL;
    if (out) *out = NULL;
    HRESULT hr = core_buffer8_wrap((IUnknown *)owned, device, 6u, out ? &view : NULL);
    if (out) *out = (IDirect3DVertexBuffer8 *)view;
    return hr;
}
HRESULT WINAPI d8wg_core_wrap_index_buffer8(struct IDirect3DIndexBuffer9 *owned,
        IDirect3DDevice8 *device, IDirect3DIndexBuffer8 **out)
{
    void *view = NULL;
    if (out) *out = NULL;
    HRESULT hr = core_buffer8_wrap((IUnknown *)owned, device, 7u, out ? &view : NULL);
    if (out) *out = (IDirect3DIndexBuffer8 *)view;
    return hr;
}
static IUnknown *core_buffer8_backend(IUnknown *iface, UINT kind)
{
    if (!iface || iface->lpVtbl != core_buffer8_vtable(kind)) return NULL;
    return ((D8CoreBuffer *)iface)->backend;
}
struct IDirect3DVertexBuffer9 *WINAPI d8wg_core_vertex_buffer_backend8(IDirect3DVertexBuffer8 *iface)
{
    return (struct IDirect3DVertexBuffer9 *)core_buffer8_backend((IUnknown *)iface, 6u);
}
struct IDirect3DIndexBuffer9 *WINAPI d8wg_core_index_buffer_backend8(IDirect3DIndexBuffer8 *iface)
{
    return (struct IDirect3DIndexBuffer9 *)core_buffer8_backend((IUnknown *)iface, 7u);
}
