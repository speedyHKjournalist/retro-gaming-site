/* App-local output reference plus bounded/aliased XYZ tests. The original
 * Microsoft reference program remains unchanged and never runs these cases. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define DIRECTDRAW_VERSION 0x0700
#define DIRECT3D_VERSION 0x0700
#include <d3d.h>
#include <ddraw.h>
#include <windows.h>
static HRESULT layout_process(IDirect3DVertexBuffer7*, DWORD, DWORD, DWORD,
    IDirect3DVertexBuffer7*, DWORD, IDirect3DDevice7*, DWORD);
#undef IDirect3DVertexBuffer7_ProcessVertices
#define IDirect3DVertexBuffer7_ProcessVertices(a, b, c, d, e, f, g, h) layout_process(a, b, c, d, e, f, g, h)
#define D9WG_PROXY_CAPTURE
#include "d3d7_native_layout_reference.c"
#define CHECK_LAYOUT(x)                                     \
    do {                                                    \
        if (!(x)) {                                         \
            line("FAIL proxy XYZ alias/bounds cases: " #x); \
            ExitProcess(1);                                 \
        }                                                   \
    } while (0)
static void proxy_layout_cases(IDirect3DDevice7* device)
{
    IDirect3D7* parent;
    IDirect3DVertexBuffer7* buffer;
    CHECK_LAYOUT(SUCCEEDED(IDirect3DDevice7_GetDirect3D(device, &parent)));
    D3DVERTEXBUFFERDESC desc = { sizeof(desc), D3DVBCAPS_SYSTEMMEMORY, D3DFVF_XYZ, 7 };
    CHECK_LAYOUT(SUCCEEDED(IDirect3D7_CreateVertexBuffer(parent, &desc, &buffer, 0)));
    float (*data)[3];
    CHECK_LAYOUT(SUCCEEDED(IDirect3DVertexBuffer7_Lock(buffer, 0, (void**)&data, 0)));
    for (unsigned i = 0; i < 7; ++i) {
        data[i][0] = .25f * i;
        data[i][1] = 0;
        data[i][2] = .5f;
    }
    CHECK_LAYOUT(SUCCEEDED(IDirect3DVertexBuffer7_Unlock(buffer)));
    CHECK_LAYOUT(SUCCEEDED(buffer->lpVtbl->ProcessVertices(buffer, D3DVOP_TRANSFORM, 2, 3, buffer, 0, device, 0)));
    CHECK_LAYOUT(SUCCEEDED(IDirect3DVertexBuffer7_Lock(buffer, 0, (void**)&data, 0)));
    for (unsigned i = 0; i < 3; ++i) {
        CHECK_LAYOUT(data[i + 2][0] == 57 + 6.25f * i);
        CHECK_LAYOUT(data[i + 2][1] == 51 && data[i + 2][2] == .375f);
    }
    CHECK_LAYOUT(data[5][0] == .5f && data[5][1] == 0 && data[5][2] == .5f);
    CHECK_LAYOUT(SUCCEEDED(IDirect3DVertexBuffer7_Unlock(buffer)));
    /* Final nominal XYZ vertex, in-place, with temporary clipping records.
     * The original RGB runtime hangs on this combination. */
    CHECK_LAYOUT(SUCCEEDED(buffer->lpVtbl->ProcessVertices(buffer, D3DVOP_TRANSFORM | D3DVOP_CLIP | D3DVOP_EXTENTS, 6, 1, buffer, 0, device, 0)));
    CHECK_LAYOUT(SUCCEEDED(IDirect3DVertexBuffer7_Lock(buffer, 0, (void**)&data, 0)));
    CHECK_LAYOUT(data[6][0] == 57 && data[6][1] == 51 && data[6][2] == .375f);
    CHECK_LAYOUT(SUCCEEDED(IDirect3DVertexBuffer7_Unlock(buffer)));
    IDirect3DVertexBuffer7_Release(buffer);
    IDirect3D7_Release(parent);
    line("PASS proxy XYZ aliases, final buffer boundary and temporary clip/extents records");
}
static HRESULT layout_process(IDirect3DVertexBuffer7* destination, DWORD op, DWORD first,
    DWORD count, IDirect3DVertexBuffer7* source, DWORD source_first,
    IDirect3DDevice7* device, DWORD flags)
{
    static BOOL checked;
    if (!checked) {
        checked = TRUE;
        proxy_layout_cases(device);
    }
    return destination->lpVtbl->ProcessVertices(destination, op, first, count, source, source_first, device, flags);
}
