#ifndef D8WG_CORE_VIEW_H
#define D8WG_CORE_VIEW_H
#include <d3d8.h>
void *WINAPI d8wg_core_device_allocate_view8(IDirect3DDevice8 *device, UINT bytes);
void WINAPI d8wg_core_device_retire_view8(IDirect3DDevice8 *device, void *view);
ULONG WINAPI d8wg_core_device_child_add8(IDirect3DDevice8 *device);
ULONG WINAPI d8wg_core_device_child_release8(IDirect3DDevice8 *device);
static inline BOOL d8wg_core_guid8(REFIID a, REFIID b)
{
    UINT i;
    if (!a || !b || a->Data1 != b->Data1 || a->Data2 != b->Data2 || a->Data3 != b->Data3)
        return FALSE;
    for (i = 0; i < 8; ++i) if (a->Data4[i] != b->Data4[i]) return FALSE;
    return TRUE;
}
/* Frontend public refs coalesce into one core public reference and one frontend
 * device pin. Arguments are copied before either Release can retire metadata. */
static inline ULONG d8wg_core_view_add8(LONG *count, IUnknown *backend,
        IDirect3DDevice8 *device)
{
    ULONG refs = (ULONG)InterlockedIncrement(count);
    if (refs == 1) {
        IUnknown_AddRef(backend);
        d8wg_core_device_child_add8(device);
    }
    return refs;
}
static inline ULONG d8wg_core_view_release8(LONG *count, IUnknown *backend,
        IDirect3DDevice8 *device)
{
    if (!InterlockedCompareExchange(count, 0, 0)) return 0;
    ULONG refs = (ULONG)InterlockedDecrement(count);
    if (!refs) {
        IUnknown_Release(backend);
        d8wg_core_device_child_release8(device);
    }
    return refs;
}
#endif
