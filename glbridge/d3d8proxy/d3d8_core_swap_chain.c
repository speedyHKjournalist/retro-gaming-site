#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include "d3d8_core_swap_chain.h"
#include "d3d8_core_surface.h"
#include "d3d8_core_view.h"

typedef struct D8CoreSwapChain {
    IDirect3DSwapChain8 iface;
    LONG refcount;
    struct IDirect3DSwapChain9 *backend;
    const D9WGSwapChainMethods8 *methods;
    IDirect3DDevice8 *device;
} D8CoreSwapChain;
static IDirect3DSwapChain8Vtbl g_core_swap_chain8_vtbl;
static BOOL core_swap_chain8_guid(REFIID a, REFIID b)
{
    return d8wg_core_guid8(a, b);
}
static ULONG WINAPI core_swap_chain8_add_ref(IDirect3DSwapChain8 *iface)
{
    D8CoreSwapChain *texture = (D8CoreSwapChain *)iface;
    return d8wg_core_view_add8(&texture->refcount, (IUnknown *)texture->backend, texture->device);
}
static ULONG WINAPI core_swap_chain8_release(IDirect3DSwapChain8 *iface)
{
    D8CoreSwapChain *texture = (D8CoreSwapChain *)iface;
    return d8wg_core_view_release8(&texture->refcount, (IUnknown *)texture->backend, texture->device);
}
static HRESULT WINAPI core_swap_chain8_query(IDirect3DSwapChain8 *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!core_swap_chain8_guid(iid, &IID_IUnknown)
            && !core_swap_chain8_guid(iid, &IID_IDirect3DSwapChain8)) return E_NOINTERFACE;
    *out = iface;
    core_swap_chain8_add_ref(iface);
    return S_OK;
}
static HRESULT WINAPI core_swap_chain8_present(IDirect3DSwapChain8 *iface,
        const RECT *source, const RECT *destination, HWND window,
        const RGNDATA *dirty)
{
    D8CoreSwapChain *chain = (D8CoreSwapChain *)iface;
    return chain->methods->present(chain->backend, source, destination, window, dirty);
}
static HRESULT WINAPI core_swap_chain8_get_back_buffer(IDirect3DSwapChain8 *iface,
        UINT index, D3DBACKBUFFER_TYPE type, IDirect3DSurface8 **out)
{
    if (!out) return D3DERR_INVALIDCALL;
    D8CoreSwapChain *chain = (D8CoreSwapChain *)iface;
    struct IDirect3DSurface9 *surface = NULL;
    HRESULT hr = chain->methods->get_back_buffer(chain->backend, index, (UINT)type, &surface);
    /* Invalid chain indices preserve the caller's output in the existing8
     * contract. Only a successful core getter enters the consuming wrapper. */
    if (FAILED(hr)) return hr;
    return d8wg_core_wrap_surface8(surface, chain->device,
            (IUnknown *)chain->device, FALSE, out);
}
static IDirect3DSwapChain8Vtbl g_core_swap_chain8_vtbl = {
    .QueryInterface = core_swap_chain8_query,
    .AddRef = core_swap_chain8_add_ref,
    .Release = core_swap_chain8_release,
    .Present = core_swap_chain8_present,
    .GetBackBuffer = core_swap_chain8_get_back_buffer
};
static void WINAPI core_swap_chain8_retired(void *view)
{
    HeapFree(GetProcessHeap(), 0, view);
}
HRESULT WINAPI d8wg_core_wrap_swap_chain8(struct IDirect3DSwapChain9 *owned,
        IDirect3DDevice8 *device, IDirect3DSwapChain8 **out)
{
    HRESULT hr = D3DERR_INVALIDCALL;
    D8CoreSwapChain *view = NULL;
    if (out) *out = NULL;
    if (!out || !owned || !device) goto done;
    const D9WGSwapChainMethods8 *methods = d9wg_d3d9_core_swap_chain_methods8(owned);
    if (!methods) goto done;
    void *cached = NULL;
    hr = d9wg_d3d9_core_get_view8((IUnknown *)owned, &cached);
    if (FAILED(hr)) goto done;
    view = (D8CoreSwapChain *)cached;
    if (!view) {
        view = (D8CoreSwapChain *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*view));
        if (!view) { hr = E_OUTOFMEMORY; goto done; }
        view->iface.lpVtbl = &g_core_swap_chain8_vtbl;
        view->backend = owned;
        view->methods = methods;
        view->device = device;
        hr = d9wg_d3d9_core_attach_view8((IUnknown *)owned, view, core_swap_chain8_retired);
        if (FAILED(hr)) {
            HeapFree(GetProcessHeap(), 0, view);
            if (hr != D3DERR_INVALIDCALL) goto done;
            hr = d9wg_d3d9_core_get_view8((IUnknown *)owned, &cached);
            if (FAILED(hr)) goto done;
            if (!cached) { hr = D3DERR_INVALIDCALL; goto done; }
            view = (D8CoreSwapChain *)cached;
        }
    }
    if (view->iface.lpVtbl != &g_core_swap_chain8_vtbl || view->device != device) {
        hr = D3DERR_INVALIDCALL;
        goto done;
    }
    core_swap_chain8_add_ref(&view->iface);
    *out = &view->iface;
    hr = D3D_OK;
done:
    if (owned) IUnknown_Release((IUnknown *)owned);
    return hr;
}

struct IDirect3DSwapChain9 *WINAPI d8wg_core_swap_chain_backend8(IDirect3DSwapChain8 *texture)
{
    if (!texture || texture->lpVtbl != &g_core_swap_chain8_vtbl) return NULL;
    return ((D8CoreSwapChain *)texture)->backend;
}
