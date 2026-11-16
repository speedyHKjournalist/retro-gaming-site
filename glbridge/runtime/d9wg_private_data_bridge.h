#ifndef D9WG_PRIVATE_DATA_BRIDGE_H
#define D9WG_PRIVATE_DATA_BRIDGE_H
#include "d9wg_private_data.h"

static void* d9wg_private_allocate(void* context, uint32_t size)
{
    (void)context;
    return HeapAlloc(GetProcessHeap(), 0, size);
}
static void d9wg_private_release_memory(void* context, void* pointer)
{
    (void)context;
    HeapFree(GetProcessHeap(), 0, pointer);
}
static void d9wg_private_retain(void* context, void* pointer)
{
    (void)context;
    IUnknown_AddRef((IUnknown*)pointer);
}
static void d9wg_private_release(void* context, void* pointer)
{
    (void)context;
    IUnknown_Release((IUnknown*)pointer);
}
static void d9wg_private_lock(void* context)
{
    (void)context;
    EnterCriticalSection(&g_transport_lock);
}
static void d9wg_private_unlock(void* context)
{
    (void)context;
    LeaveCriticalSection(&g_transport_lock);
}
static const D9WGPrivateOps d9wg_private_ops = {
    NULL, d9wg_private_allocate, d9wg_private_release_memory,
    d9wg_private_retain, d9wg_private_release, d9wg_private_lock, d9wg_private_unlock
};
static HRESULT d9wg_private_hresult(int result)
{
    switch (result) {
    case D9WG_PRIVATE_OK:
        return D3D_OK;
    case D9WG_PRIVATE_NOT_FOUND:
        return D3DERR_NOTFOUND;
    case D9WG_PRIVATE_MORE_DATA:
        return D3DERR_MOREDATA;
    case D9WG_PRIVATE_OUT_OF_MEMORY:
        return E_OUTOFMEMORY;
    default:
        return D3DERR_INVALIDCALL;
    }
}
static HRESULT d9wg_get_private_data(D9WGPrivateStore* store, REFGUID tag,
    void* data, DWORD* size)
{
    return d9wg_private_hresult(d9wg_private_get(store, tag, data, size, &d9wg_private_ops));
}
#endif
