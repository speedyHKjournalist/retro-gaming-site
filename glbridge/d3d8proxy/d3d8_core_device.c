#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include "d3d8_core_device.h"
#include "d3d8_core_view.h"
#include "d3d8_core_surface.h"
#include "d3d8_core_texture.h"
#include "d3d8_core_cube_texture.h"
#include "d3d8_core_volume_texture.h"
#include "d3d8_core_buffer.h"
#include "d3d8_core_swap_chain.h"
#include "../runtime/d9wg_state_defaults.h"
#include "../runtime/d9wg_state_bridge8.h"
#include "../runtime/d9wg_declaration8.h"
#include "../runtime/d9wg_present_parameters.h"
#define D9WG_STATE_BLOCK_API 8
#include "../runtime/d9wg_state_block_scope.h"

typedef struct D8CoreDevice D8CoreDevice;
typedef struct D8CoreShader {
    struct D8CoreShader *next;
    D8CoreDevice *device;
    DWORD handle;
    UINT refs, declaration_bytes, function_bytes;
    BOOL pixel, deleted;
    struct IDirect3DVertexDeclaration9 *declaration;
    IUnknown *program;
    BYTE bytes[];
} D8CoreShader;
typedef struct D8CoreRawState {
    BYTE render_mask[256], texture_mask[8][D9WG_TEXTURE_STAGE_STATES8];
    DWORD render[256], texture[8][D9WG_TEXTURE_STAGE_STATES8];
    BOOL vertex_mask, pixel_mask, indices_mask;
    DWORD vertex, pixel;
    UINT base;
    D8CoreShader *vs, *ps;
} D8CoreRawState;
typedef struct D8CoreBlock {
    struct D8CoreBlock *next;
    DWORD token;
    UINT type;
    D8CoreRawState raw;
} D8CoreBlock;
/* Reusable frontend allocations preserve the COM header after retirement.
 * Only idle wrappers are cached; backend resources are released immediately.
 * Reuse bounds retained memory by peak simultaneous wrappers, and final device
 * teardown frees the cache. This matches retail's immediate repeated Release. */
typedef struct D8CoreViewStorage {
    struct D8CoreViewStorage *next;
    UINT bytes;
} D8CoreViewStorage;
struct D8CoreDevice {
    IDirect3DDevice8 iface;
    LONG refs, children, metadata_refs;
    BOOL cleaning, retired;
    LONG view_storage_lock;
    D8CoreViewStorage *retired_views;
    IDirect3D8 *parent;
    struct IDirect3DDevice9 *backend;
    const D9WGDeviceMethods8 *methods;
    D8CoreRawState live;
    D8CoreShader *shaders;
    D8CoreBlock *blocks, *recording;
};
static IDirect3DDevice8Vtbl g_core_device8_vtbl;
static LONG g_core_shader8_token;
static D8CoreDevice *core_device8(IDirect3DDevice8 *iface) { return (D8CoreDevice *)iface; }
void *WINAPI d8wg_core_device_allocate_view8(IDirect3DDevice8 *iface, UINT bytes)
{
    if (iface->lpVtbl != &g_core_device8_vtbl)
        return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
    D8CoreDevice *device = core_device8(iface);
    D8CoreViewStorage *storage = NULL;
    while (InterlockedCompareExchange(&device->view_storage_lock, 1, 0)) Sleep(0);
    D8CoreViewStorage **link = &device->retired_views;
    while (*link && (*link)->bytes != bytes) link = &(*link)->next;
    if (*link) { storage = *link; *link = storage->next; }
    InterlockedExchange(&device->view_storage_lock, 0);
    if (!storage) {
        if (bytes > 0xffffffffu - sizeof(*storage)) return NULL;
        storage = HeapAlloc(GetProcessHeap(), 0, sizeof(*storage) + bytes);
        if (!storage) return NULL;
        storage->bytes = bytes;
    }
    storage->next = NULL;
    ZeroMemory(storage + 1, bytes);
    return storage + 1;
}

void WINAPI d8wg_core_device_retire_view8(IDirect3DDevice8 *iface, void *view)
{
    if (iface->lpVtbl != &g_core_device8_vtbl) {
        HeapFree(GetProcessHeap(), 0, view);
        return;
    }
    D8CoreDevice *device = core_device8(iface);
    D8CoreViewStorage *storage = (D8CoreViewStorage *)view - 1;
    while (InterlockedCompareExchange(&device->view_storage_lock, 1, 0)) Sleep(0);
    storage->next = device->retired_views;
    device->retired_views = storage;
    InterlockedExchange(&device->view_storage_lock, 0);
}

static void core_shader8_release(D8CoreShader *shader)
{
    if (!shader || --shader->refs) return;
    D8CoreDevice *device = shader->device;
    D8CoreShader **link = &device->shaders;
    while (*link != shader) link = &(*link)->next;
    *link = shader->next;
    if (shader->program)
        d9wg_d3d9_core_shader_storage_release8(device->backend, shader->program);
    if (shader->declaration)
        d9wg_d3d9_core_shader_storage_release8(device->backend, (IUnknown *)shader->declaration);
    HeapFree(GetProcessHeap(), 0, shader);
}
static void core_shader8_assign(D8CoreShader **slot, D8CoreShader *shader)
{
    D8CoreShader *old = *slot;
    if (shader) ++shader->refs;
    *slot = shader;
    core_shader8_release(old);
}
static void core_raw8_release(D8CoreRawState *raw)
{
    core_shader8_assign(&raw->vs, NULL);
    core_shader8_assign(&raw->ps, NULL);
}
static void core_block8_free(D8CoreBlock *block)
{
    if (!block) return;
    core_raw8_release(&block->raw);
    HeapFree(GetProcessHeap(), 0, block);
}
static void core_device8_metadata_release(D8CoreDevice *device)
{
    if (InterlockedDecrement(&device->metadata_refs)) return;
    IDirect3D8 *parent = device->parent;
    while (device->retired_views) {
        D8CoreViewStorage *storage = device->retired_views;
        device->retired_views = storage->next;
        HeapFree(GetProcessHeap(), 0, storage);
    }
    HeapFree(GetProcessHeap(), 0, device);
    IDirect3D8_Release(parent);
}
static void WINAPI core_device8_cleanup(void *view)
{
    D8CoreDevice *device = view;
    D8CoreBlock *block = device->blocks, *recording = device->recording;
    device->blocks = device->recording = NULL;
    core_raw8_release(&device->live);
    device->live.vertex = device->live.pixel = device->live.base = 0;
    core_block8_free(recording);
    while (block) { D8CoreBlock *next = block->next; core_block8_free(block); block = next; }
}
static void WINAPI core_device8_retire(void *view)
{
    D8CoreDevice *device = view;
    core_device8_cleanup(device);
    /* Core storage remains valid until this callback returns. All private
     * shader holds must be released here, never by the eventual CPU free. */
    while (device->shaders) core_shader8_release(device->shaders);
    device->retired = TRUE;
    core_device8_metadata_release(device);
}
static ULONG WINAPI core_device8_AddRef(IDirect3DDevice8 *iface)
{
    D8CoreDevice *device = core_device8(iface);
    ULONG refs = InterlockedIncrement(&device->refs);
    if (refs == 1) IUnknown_AddRef((IUnknown *)device->backend);
    return refs;
}
static ULONG WINAPI core_device8_Release(IDirect3DDevice8 *iface)
{
    D8CoreDevice *device = core_device8(iface);
    InterlockedIncrement(&device->metadata_refs);
    ULONG refs = InterlockedDecrement(&device->refs);
    if (!refs) IUnknown_Release((IUnknown *)device->backend);
    else if (refs == (ULONG)device->children && !device->cleaning) {
        device->cleaning = TRUE;
        d9wg_d3d9_core_release_owned8(device->backend);
        device->cleaning = FALSE;
    }
    core_device8_metadata_release(device);
    return refs;
}
ULONG WINAPI d8wg_core_device_child_add8(IDirect3DDevice8 *iface)
{
    if (iface->lpVtbl != &g_core_device8_vtbl) return IDirect3DDevice8_AddRef(iface);
    InterlockedIncrement(&core_device8(iface)->children);
    return core_device8_AddRef(iface);
}
ULONG WINAPI d8wg_core_device_child_release8(IDirect3DDevice8 *iface)
{
    if (iface->lpVtbl != &g_core_device8_vtbl) return IDirect3DDevice8_Release(iface);
    InterlockedDecrement(&core_device8(iface)->children);
    return core_device8_Release(iface);
}
static HRESULT WINAPI core_device8_QueryInterface(IDirect3DDevice8 *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!d8wg_core_guid8(iid, &IID_IUnknown) && !d8wg_core_guid8(iid, &IID_IDirect3DDevice8))
        return E_NOINTERFACE;
    *out = iface; core_device8_AddRef(iface); return S_OK;
}
static HRESULT WINAPI core_device8_GetDirect3D(IDirect3DDevice8 *iface, IDirect3D8 **out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = core_device8(iface)->parent;
    IDirect3D8_AddRef(*out); return D3D_OK;
}
static HRESULT WINAPI core_device8_GetDeviceCaps(IDirect3DDevice8 *iface, D3DCAPS8 *out)
{
    D8CoreDevice *device = core_device8(iface);
    if (!out) return D3DERR_INVALIDCALL;
    D3DDEVICE_CREATION_PARAMETERS creation;
    HRESULT hr = device->methods->get_creation_parameters(device->backend, &creation);
    if (FAILED(hr)) return hr;
    return IDirect3D8_GetDeviceCaps(device->parent, creation.AdapterOrdinal, creation.DeviceType, out);
}
static void core_device8_defaults(D8CoreDevice *device, BOOL auto_depth)
{
    for (UINT i = 0; i < 256; ++i)
        device->live.render[i] = d9wg_render_default(8, i, auto_depth, D9WG_DEFAULT_POINT_MAX_BITS);
    for (UINT stage = 0; stage < 8; ++stage)
        for (UINT i = 0; i < D9WG_TEXTURE_STAGE_STATES8; ++i)
            device->live.texture[stage][i] = d9wg_texture_default(8, stage, i);
}
static void core_device8_return_parameters(D3DPRESENT_PARAMETERS *parameters)
{
    D3DPRESENT_PARAMETERS effective;
    d9wg_present_parameters_effective(&effective, parameters,
            parameters->BackBufferWidth, parameters->BackBufferHeight,
            parameters->BackBufferFormat, parameters->hDeviceWindow);
    d9wg_present_parameters_return(8, parameters, &effective);
}
/* Getter-owned backend resources are consumed by their cached wrappers. A
 * surface's real texture/cube container must be restored before wrapping it. */
static HRESULT core_device8_surface(D8CoreDevice *device, struct IDirect3DSurface9 *surface,
        IDirect3DSurface8 **out)
{
    IUnknown *owner = NULL, *container = NULL;
    UINT kind = 0;
    HRESULT hr = d9wg_d3d9_core_surface_owner8(surface, &owner, &kind);
    if (SUCCEEDED(hr) && (kind == 3 || kind == 4)) {
        IUnknown_AddRef(owner);
        if (kind == 3) hr = d8wg_core_wrap_texture8((struct IDirect3DTexture9 *)owner,
                &device->iface, (IDirect3DTexture8 **)&container);
        else hr = d8wg_core_wrap_cube_texture8((struct IDirect3DCubeTexture9 *)owner,
                &device->iface, (IDirect3DCubeTexture8 **)&container);
    }
    if (FAILED(hr)) { IUnknown_Release((IUnknown *)surface); return hr; }
    hr = d8wg_core_wrap_surface8(surface, &device->iface,
            container ? container : (IUnknown *)&device->iface, container != NULL, out);
    if (container) IUnknown_Release(container);
    return hr;
}
static struct IDirect3DBaseTexture9 *core_device8_texture_backend(IDirect3DBaseTexture8 *texture)
{
    struct IDirect3DBaseTexture9 *backend;
    if (!texture) return NULL;
    backend = (struct IDirect3DBaseTexture9 *)d8wg_core_texture_backend8((IDirect3DTexture8 *)texture);
    if (!backend) backend = (struct IDirect3DBaseTexture9 *)d8wg_core_cube_texture_backend8((IDirect3DCubeTexture8 *)texture);
    if (!backend) backend = (struct IDirect3DBaseTexture9 *)d8wg_core_volume_texture_backend8((IDirect3DVolumeTexture8 *)texture);
    return backend;
}
static HRESULT core_device8_texture(D8CoreDevice *device, struct IDirect3DBaseTexture9 *owned,
        IDirect3DBaseTexture8 **out)
{
    if (!owned) { *out = NULL; return D3D_OK; }
    if (d9wg_d3d9_core_texture_methods8((struct IDirect3DTexture9 *)owned))
        return d8wg_core_wrap_texture8((struct IDirect3DTexture9 *)owned, &device->iface, (IDirect3DTexture8 **)out);
    if (d9wg_d3d9_core_cube_texture_methods8((struct IDirect3DCubeTexture9 *)owned))
        return d8wg_core_wrap_cube_texture8((struct IDirect3DCubeTexture9 *)owned, &device->iface, (IDirect3DCubeTexture8 **)out);
    return d8wg_core_wrap_volume_texture8((struct IDirect3DVolumeTexture9 *)owned, &device->iface, (IDirect3DVolumeTexture8 **)out);
}

static HRESULT WINAPI core_device8_TestCooperativeLevel(IDirect3DDevice8 *iface)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->test_cooperative_level(device->backend);
}

static UINT WINAPI core_device8_GetAvailableTextureMem(IDirect3DDevice8 *iface)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->get_available_texture_mem(device->backend);
}

static HRESULT WINAPI core_device8_ResourceManagerDiscardBytes(IDirect3DDevice8 *iface, DWORD Bytes)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->resource_manager_discard_bytes(device->backend, Bytes);
}

static HRESULT WINAPI core_device8_GetDisplayMode(IDirect3DDevice8 *iface, D3DDISPLAYMODE * pMode)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->get_display_mode(device->backend, pMode);
}

static HRESULT WINAPI core_device8_GetCreationParameters(IDirect3DDevice8 *iface, D3DDEVICE_CREATION_PARAMETERS * pParameters)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->get_creation_parameters(device->backend, pParameters);
}

static void WINAPI core_device8_SetCursorPosition(IDirect3DDevice8 *iface, UINT XScreenSpace, UINT YScreenSpace,DWORD Flags)
{
    D8CoreDevice *device = core_device8(iface);
    device->methods->set_cursor_position(device->backend, XScreenSpace, YScreenSpace, Flags);
}

static WINBOOL WINAPI core_device8_ShowCursor(IDirect3DDevice8 *iface, WINBOOL bShow)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->show_cursor(device->backend, bShow);
}

static HRESULT WINAPI core_device8_Present(IDirect3DDevice8 *iface, const RECT *src_rect, const RECT *dst_rect, HWND dst_window_override, const RGNDATA *dirty_region)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->present(device->backend, src_rect, dst_rect, dst_window_override, dirty_region);
}

static HRESULT WINAPI core_device8_GetRasterStatus(IDirect3DDevice8 *iface, D3DRASTER_STATUS * pRasterStatus)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->get_raster_status(device->backend, pRasterStatus);
}

static void WINAPI core_device8_SetGammaRamp(IDirect3DDevice8 *iface, DWORD flags, const D3DGAMMARAMP *ramp)
{
    D8CoreDevice *device = core_device8(iface);
    device->methods->set_gamma_ramp(device->backend, flags, ramp);
}

static void WINAPI core_device8_GetGammaRamp(IDirect3DDevice8 *iface, D3DGAMMARAMP * pRamp)
{
    D8CoreDevice *device = core_device8(iface);
    device->methods->get_gamma_ramp(device->backend, pRamp);
}

static HRESULT WINAPI core_device8_BeginScene(IDirect3DDevice8 *iface)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->begin_scene(device->backend);
}

static HRESULT WINAPI core_device8_EndScene(IDirect3DDevice8 *iface)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->end_scene(device->backend);
}

static HRESULT WINAPI core_device8_Clear(IDirect3DDevice8 *iface, DWORD rect_count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->clear(device->backend, rect_count, rects, flags, color, z, stencil);
}

static HRESULT WINAPI core_device8_SetTransform(IDirect3DDevice8 *iface, D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->set_transform(device->backend, state, matrix);
}

static HRESULT WINAPI core_device8_GetTransform(IDirect3DDevice8 *iface, D3DTRANSFORMSTATETYPE State,D3DMATRIX * pMatrix)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->get_transform(device->backend, State, pMatrix);
}

static HRESULT WINAPI core_device8_MultiplyTransform(IDirect3DDevice8 *iface, D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->multiply_transform(device->backend, state, matrix);
}

static HRESULT WINAPI core_device8_SetViewport(IDirect3DDevice8 *iface, const D3DVIEWPORT8 *viewport)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->set_viewport(device->backend, viewport);
}

static HRESULT WINAPI core_device8_GetViewport(IDirect3DDevice8 *iface, D3DVIEWPORT8 * pViewport)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->get_viewport(device->backend, pViewport);
}

static HRESULT WINAPI core_device8_SetMaterial(IDirect3DDevice8 *iface, const D3DMATERIAL8 *material)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->set_material(device->backend, material);
}

static HRESULT WINAPI core_device8_GetMaterial(IDirect3DDevice8 *iface, D3DMATERIAL8 *pMaterial)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->get_material(device->backend, pMaterial);
}

static HRESULT WINAPI core_device8_SetLight(IDirect3DDevice8 *iface, DWORD index, const D3DLIGHT8 *light)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->set_light(device->backend, index, light);
}

static HRESULT WINAPI core_device8_GetLight(IDirect3DDevice8 *iface, DWORD Index,D3DLIGHT8 * pLight)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->get_light(device->backend, Index, pLight);
}

static HRESULT WINAPI core_device8_LightEnable(IDirect3DDevice8 *iface, DWORD Index,WINBOOL Enable)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->light_enable(device->backend, Index, Enable);
}

static HRESULT WINAPI core_device8_GetLightEnable(IDirect3DDevice8 *iface, DWORD Index,WINBOOL * pEnable)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->get_light_enable(device->backend, Index, pEnable);
}

static HRESULT WINAPI core_device8_SetClipPlane(IDirect3DDevice8 *iface, DWORD index, const float *plane)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->set_clip_plane(device->backend, index, plane);
}

static HRESULT WINAPI core_device8_GetClipPlane(IDirect3DDevice8 *iface, DWORD Index,float * pPlane)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->get_clip_plane(device->backend, Index, pPlane);
}

static HRESULT WINAPI core_device8_SetClipStatus(IDirect3DDevice8 *iface, const D3DCLIPSTATUS8 *clip_status)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->set_clip_status(device->backend, clip_status);
}

static HRESULT WINAPI core_device8_GetClipStatus(IDirect3DDevice8 *iface, D3DCLIPSTATUS8 * pClipStatus)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->get_clip_status(device->backend, pClipStatus);
}

static HRESULT WINAPI core_device8_ValidateDevice(IDirect3DDevice8 *iface, DWORD * pNumPasses)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->validate_device(device->backend, pNumPasses);
}

static HRESULT WINAPI core_device8_SetPaletteEntries(IDirect3DDevice8 *iface, UINT palette_idx, const PALETTEENTRY *entries)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->set_palette_entries(device->backend, palette_idx, entries);
}

static HRESULT WINAPI core_device8_GetPaletteEntries(IDirect3DDevice8 *iface, UINT PaletteNumber,PALETTEENTRY * pEntries)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->get_palette_entries(device->backend, PaletteNumber, pEntries);
}

static HRESULT WINAPI core_device8_SetCurrentTexturePalette(IDirect3DDevice8 *iface, UINT PaletteNumber)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->set_current_texture_palette(device->backend, PaletteNumber);
}

static HRESULT WINAPI core_device8_GetCurrentTexturePalette(IDirect3DDevice8 *iface, UINT * PaletteNumber)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->get_current_texture_palette(device->backend, PaletteNumber);
}

static HRESULT WINAPI core_device8_DrawPrimitive(IDirect3DDevice8 *iface, D3DPRIMITIVETYPE PrimitiveType,UINT StartVertex,UINT PrimitiveCount)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->draw_primitive(device->backend, PrimitiveType, StartVertex, PrimitiveCount);
}

static HRESULT WINAPI core_device8_DrawPrimitiveUP(IDirect3DDevice8 *iface, D3DPRIMITIVETYPE primitive_type, UINT primitive_count, const void *data, UINT stride)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->draw_primitive_up(device->backend, primitive_type, primitive_count, data, stride);
}

static HRESULT WINAPI core_device8_DrawIndexedPrimitiveUP(IDirect3DDevice8 *iface, D3DPRIMITIVETYPE primitive_type, UINT min_vertex_idx, UINT vertex_count, UINT primitive_count, const void *index_data, D3DFORMAT index_format, const void *data, UINT stride)
{
    D8CoreDevice *device = core_device8(iface);
    HRESULT hr = device->methods->draw_indexed_primitive_up(device->backend, primitive_type, min_vertex_idx, vertex_count, primitive_count, index_data, index_format, data, stride);
    if (SUCCEEDED(hr) && primitive_count) device->live.base = 0;
    return hr;
}

static HRESULT WINAPI core_device8_SetVertexShaderConstant(IDirect3DDevice8 *iface, DWORD reg_idx, const void *data, DWORD count)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->set_vertex_shader_constant(device->backend, reg_idx, data, count);
}

static HRESULT WINAPI core_device8_GetVertexShaderConstant(IDirect3DDevice8 *iface, DWORD Register,void * pConstantData,DWORD ConstantCount)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->get_vertex_shader_constant(device->backend, Register, pConstantData, ConstantCount);
}

static HRESULT WINAPI core_device8_SetPixelShaderConstant(IDirect3DDevice8 *iface, DWORD reg_idx, const void *data, DWORD count)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->set_pixel_shader_constant(device->backend, reg_idx, data, count);
}

static HRESULT WINAPI core_device8_GetPixelShaderConstant(IDirect3DDevice8 *iface, DWORD Register,void * pConstantData,DWORD ConstantCount)
{
    D8CoreDevice *device = core_device8(iface);
    return device->methods->get_pixel_shader_constant(device->backend, Register, pConstantData, ConstantCount);
}

static HRESULT WINAPI core_device8_DrawRectPatch(IDirect3DDevice8 *iface, UINT handle, const float *segment_count, const D3DRECTPATCH_INFO *patch_info)
{
    D8CoreDevice *device = core_device8(iface);
    if (!patch_info) return D3DERR_INVALIDCALL;
    return device->methods->draw_rect_patch(device->backend, handle, segment_count, patch_info);
}

static HRESULT WINAPI core_device8_DrawTriPatch(IDirect3DDevice8 *iface, UINT handle, const float *segment_count, const D3DTRIPATCH_INFO *patch_info)
{
    D8CoreDevice *device = core_device8(iface);
    if (!patch_info) return D3DERR_INVALIDCALL;
    return device->methods->draw_tri_patch(device->backend, handle, segment_count, patch_info);
}

static HRESULT WINAPI core_device8_GetInfo(IDirect3DDevice8 *iface, DWORD DevInfoID,void * pDevInfoStruct,DWORD DevInfoStructSize)
{
    (void)iface; (void)pDevInfoStruct; (void)DevInfoStructSize;
    /* Retail runtime exposes no debug statistics. */
    return DevInfoID < 4 ? E_FAIL : S_FALSE;
}

static HRESULT WINAPI core_device8_DeletePatch(IDirect3DDevice8 *iface, UINT Handle)
{
    (void)iface; (void)Handle; return D3D_OK;
}

static HRESULT WINAPI core_device8_CreateTexture(IDirect3DDevice8 *iface, UINT Width,UINT Height,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DTexture8 ** ppTexture)
{
    D8CoreDevice *device = core_device8(iface);
    if (!ppTexture) return D3DERR_INVALIDCALL;
    struct IDirect3DTexture9 *owned = (struct IDirect3DTexture9 *)(UINT_PTR)1;
    HRESULT hr = d9wg_d3d9_core_texture8(device->backend, Width, Height, Levels, Usage, Format, Pool, &owned);
    if (FAILED(hr)) { if (!owned) *ppTexture = NULL; return hr; }
    *ppTexture = NULL;
    return d8wg_core_wrap_texture8(owned, iface, ppTexture);
}

static HRESULT WINAPI core_device8_CreateCubeTexture(IDirect3DDevice8 *iface, UINT EdgeLength,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DCubeTexture8 ** ppCubeTexture)
{
    D8CoreDevice *device = core_device8(iface);
    if (!ppCubeTexture) return D3DERR_INVALIDCALL;
    struct IDirect3DCubeTexture9 *owned = (struct IDirect3DCubeTexture9 *)(UINT_PTR)1;
    HRESULT hr = d9wg_d3d9_core_cube_texture8(device->backend, EdgeLength, Levels, Usage, Format, Pool, &owned);
    if (FAILED(hr)) { if (!owned) *ppCubeTexture = NULL; return hr; }
    *ppCubeTexture = NULL;
    return d8wg_core_wrap_cube_texture8(owned, iface, ppCubeTexture);
}

static HRESULT WINAPI core_device8_CreateVolumeTexture(IDirect3DDevice8 *iface, UINT Width,UINT Height,UINT Depth,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DVolumeTexture8 ** ppVolumeTexture)
{
    D8CoreDevice *device = core_device8(iface);
    if (!ppVolumeTexture) return D3DERR_INVALIDCALL;
    struct IDirect3DVolumeTexture9 *owned = (struct IDirect3DVolumeTexture9 *)(UINT_PTR)1;
    HRESULT hr = d9wg_d3d9_core_volume_texture8(device->backend, Width, Height, Depth, Levels, Usage, Format, Pool, &owned);
    if (FAILED(hr)) { if (!owned) *ppVolumeTexture = NULL; return hr; }
    *ppVolumeTexture = NULL;
    return d8wg_core_wrap_volume_texture8(owned, iface, ppVolumeTexture);
}

static HRESULT WINAPI core_device8_CreateVertexBuffer(IDirect3DDevice8 *iface, UINT Length,DWORD Usage,DWORD FVF,D3DPOOL Pool,IDirect3DVertexBuffer8 ** ppVertexBuffer)
{
    D8CoreDevice *device = core_device8(iface);
    if (!ppVertexBuffer) return D3DERR_INVALIDCALL;
    struct IDirect3DVertexBuffer9 *owned = (struct IDirect3DVertexBuffer9 *)(UINT_PTR)1;
    HRESULT hr = d9wg_d3d9_core_vertex_buffer8(device->backend, Length, Usage, FVF, Pool, &owned);
    if (FAILED(hr)) { if (!owned) *ppVertexBuffer = NULL; return hr; }
    *ppVertexBuffer = NULL;
    return d8wg_core_wrap_vertex_buffer8(owned, iface, ppVertexBuffer);
}

static HRESULT WINAPI core_device8_CreateIndexBuffer(IDirect3DDevice8 *iface, UINT Length,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DIndexBuffer8 ** ppIndexBuffer)
{
    D8CoreDevice *device = core_device8(iface);
    if (!ppIndexBuffer) return D3DERR_INVALIDCALL;
    struct IDirect3DIndexBuffer9 *owned = (struct IDirect3DIndexBuffer9 *)(UINT_PTR)1;
    HRESULT hr = d9wg_d3d9_core_index_buffer8(device->backend, Length, Usage, Format, Pool, &owned);
    if (FAILED(hr)) { if (!owned) *ppIndexBuffer = NULL; return hr; }
    *ppIndexBuffer = NULL;
    return d8wg_core_wrap_index_buffer8(owned, iface, ppIndexBuffer);
}

static HRESULT WINAPI core_device8_CreateRenderTarget(IDirect3DDevice8 *iface, UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,WINBOOL Lockable,IDirect3DSurface8 ** ppSurface)
{
    D8CoreDevice *device = core_device8(iface);
    if (!ppSurface) return D3DERR_INVALIDCALL;
    struct IDirect3DSurface9 *owned = (struct IDirect3DSurface9 *)(UINT_PTR)1;
    HRESULT hr = d9wg_d3d9_core_render_target8(device->backend, Width, Height, Format, MultiSample, Lockable, &owned);
    if (FAILED(hr)) { if (!owned) *ppSurface = NULL; return hr; }
    *ppSurface = NULL;
    return core_device8_surface(device, owned, ppSurface);
}

static HRESULT WINAPI core_device8_CreateDepthStencilSurface(IDirect3DDevice8 *iface, UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,IDirect3DSurface8 ** ppSurface)
{
    D8CoreDevice *device = core_device8(iface);
    if (!ppSurface) return D3DERR_INVALIDCALL;
    struct IDirect3DSurface9 *owned = (struct IDirect3DSurface9 *)(UINT_PTR)1;
    HRESULT hr = d9wg_d3d9_core_depth_surface8(device->backend, Width, Height, Format, MultiSample, &owned);
    if (FAILED(hr)) { if (!owned) *ppSurface = NULL; return hr; }
    *ppSurface = NULL;
    return core_device8_surface(device, owned, ppSurface);
}

static HRESULT WINAPI core_device8_CreateImageSurface(IDirect3DDevice8 *iface, UINT Width,UINT Height,D3DFORMAT Format,IDirect3DSurface8 ** ppSurface)
{
    D8CoreDevice *device = core_device8(iface);
    if (!ppSurface) return D3DERR_INVALIDCALL;
    struct IDirect3DSurface9 *owned = (struct IDirect3DSurface9 *)(UINT_PTR)1;
    HRESULT hr = d9wg_d3d9_core_image_surface8(device->backend, Width, Height, Format, &owned);
    if (FAILED(hr)) { if (!owned) *ppSurface = NULL; return hr; }
    *ppSurface = NULL;
    return core_device8_surface(device, owned, ppSurface);
}

static HRESULT WINAPI core_device8_CreateAdditionalSwapChain(IDirect3DDevice8 *iface, D3DPRESENT_PARAMETERS * pPresentationParameters, IDirect3DSwapChain8 ** pSwapChain)
{
    D8CoreDevice *device = core_device8(iface);
    if (!pSwapChain) return D3DERR_INVALIDCALL;
    struct IDirect3DSwapChain9 *owned = (struct IDirect3DSwapChain9 *)(UINT_PTR)1;
    HRESULT hr = d9wg_d3d9_core_additional8(device->backend, pPresentationParameters, &owned);
    if (FAILED(hr)) { if (!owned) *pSwapChain = NULL; return hr; }
    *pSwapChain = NULL;
    hr = d8wg_core_wrap_swap_chain8(owned, iface, pSwapChain);
    if (SUCCEEDED(hr)) core_device8_return_parameters(pPresentationParameters);
    return hr;
}

static HRESULT WINAPI core_device8_GetBackBuffer(IDirect3DDevice8 *iface, UINT BackBuffer,D3DBACKBUFFER_TYPE Type,IDirect3DSurface8 ** ppBackBuffer)
{
    D8CoreDevice *device = core_device8(iface);
    if (!ppBackBuffer) return D3DERR_INVALIDCALL;
    *ppBackBuffer = NULL;
    struct IDirect3DSurface9 *owned = NULL;
    HRESULT hr = device->methods->get_back_buffer(device->backend, BackBuffer, Type, &owned);
    return FAILED(hr) ? hr : core_device8_surface(device, owned, ppBackBuffer);
}

static HRESULT WINAPI core_device8_GetRenderTarget(IDirect3DDevice8 *iface, IDirect3DSurface8 ** ppRenderTarget)
{
    D8CoreDevice *device = core_device8(iface);
    if (!ppRenderTarget) return D3DERR_INVALIDCALL;
    *ppRenderTarget = NULL;
    struct IDirect3DSurface9 *owned = NULL;
    HRESULT hr = device->methods->get_render_target(device->backend, &owned);
    return FAILED(hr) ? hr : core_device8_surface(device, owned, ppRenderTarget);
}

static HRESULT WINAPI core_device8_GetDepthStencilSurface(IDirect3DDevice8 *iface, IDirect3DSurface8 ** ppZStencilSurface)
{
    D8CoreDevice *device = core_device8(iface);
    if (!ppZStencilSurface) return D3DERR_INVALIDCALL;
    *ppZStencilSurface = NULL;
    struct IDirect3DSurface9 *owned = NULL;
    HRESULT hr = device->methods->get_depth_stencil_surface(device->backend, &owned);
    return FAILED(hr) ? hr : core_device8_surface(device, owned, ppZStencilSurface);
}

static HRESULT WINAPI core_device8_SetCursorProperties(IDirect3DDevice8 *iface,
        UINT x, UINT y, IDirect3DSurface8 *surface)
{
    D8CoreDevice *device = core_device8(iface);
    struct IDirect3DSurface9 *backend = d8wg_core_surface_backend8(surface);
    if (!backend) return D3DERR_INVALIDCALL;
    return device->methods->set_cursor_properties(device->backend, x, y, backend);
}
static HRESULT WINAPI core_device8_SetRenderTarget(IDirect3DDevice8 *iface,
        IDirect3DSurface8 *color, IDirect3DSurface8 *depth)
{
    struct IDirect3DSurface9 *c = d8wg_core_surface_backend8(color);
    struct IDirect3DSurface9 *d = d8wg_core_surface_backend8(depth);
    if ((color && !c) || (depth && !d)) return D3DERR_INVALIDCALL;
    return d9wg_d3d9_core_set_render_target8(core_device8(iface)->backend, c, d);
}
static HRESULT WINAPI core_device8_CopyRects(IDirect3DDevice8 *iface,
        IDirect3DSurface8 *source, const RECT *rects, UINT count,
        IDirect3DSurface8 *destination, const POINT *points)
{
    struct IDirect3DSurface9 *src = d8wg_core_surface_backend8(source);
    struct IDirect3DSurface9 *dst = d8wg_core_surface_backend8(destination);
    if (!src || !dst) return D3DERR_INVALIDCALL;
    return d9wg_d3d9_core_copy_rects8(core_device8(iface)->backend, src, rects, count, dst, points);
}
static HRESULT WINAPI core_device8_GetFrontBuffer(IDirect3DDevice8 *iface, IDirect3DSurface8 *surface)
{
    struct IDirect3DSurface9 *dst = d8wg_core_surface_backend8(surface);
    if (!dst) return D3DERR_INVALIDCALL;
    return d9wg_d3d9_core_get_front_buffer8(core_device8(iface)->backend, dst);
}
static HRESULT WINAPI core_device8_UpdateTexture(IDirect3DDevice8 *iface,
        IDirect3DBaseTexture8 *source, IDirect3DBaseTexture8 *destination)
{
    struct IDirect3DBaseTexture9 *src = core_device8_texture_backend(source);
    struct IDirect3DBaseTexture9 *dst = core_device8_texture_backend(destination);
    if (!src || !dst) return D3DERR_INVALIDCALL;
    return d9wg_d3d9_core_update_texture8(core_device8(iface)->backend, src, dst);
}
static HRESULT WINAPI core_device8_SetTexture(IDirect3DDevice8 *iface,
        DWORD stage, IDirect3DBaseTexture8 *texture)
{
    D8CoreDevice *device = core_device8(iface);
    struct IDirect3DBaseTexture9 *backend = core_device8_texture_backend(texture);
    if (texture && !backend) return D3DERR_INVALIDCALL;
    return device->methods->set_texture(device->backend, stage, backend);
}
static HRESULT WINAPI core_device8_GetTexture(IDirect3DDevice8 *iface,
        DWORD stage, IDirect3DBaseTexture8 **out)
{
    D8CoreDevice *device = core_device8(iface);
    if (!out) return D3DERR_INVALIDCALL;
    *out = NULL;
    struct IDirect3DBaseTexture9 *owned = NULL;
    HRESULT hr = device->methods->get_texture(device->backend, stage, &owned);
    return FAILED(hr) ? hr : core_device8_texture(device, owned, out);
}
static HRESULT WINAPI core_device8_SetStreamSource(IDirect3DDevice8 *iface,
        UINT stream, IDirect3DVertexBuffer8 *buffer, UINT stride)
{
    D8CoreDevice *device = core_device8(iface);
    struct IDirect3DVertexBuffer9 *backend = d8wg_core_vertex_buffer_backend8(buffer);
    if (buffer && !backend) return D3DERR_INVALIDCALL;
    return device->methods->set_stream_source(device->backend, stream, backend, stride);
}
static HRESULT WINAPI core_device8_GetStreamSource(IDirect3DDevice8 *iface,
        UINT stream, IDirect3DVertexBuffer8 **out, UINT *stride)
{
    D8CoreDevice *device = core_device8(iface);
    if (!out || !stride) return D3DERR_INVALIDCALL;
    *out = NULL;
    struct IDirect3DVertexBuffer9 *owned = NULL;
    HRESULT hr = device->methods->get_stream_source(device->backend, stream, &owned, stride);
    if (FAILED(hr) || !owned) return hr;
    return d8wg_core_wrap_vertex_buffer8(owned, iface, out);
}
static HRESULT WINAPI core_device8_SetIndices(IDirect3DDevice8 *iface,
        IDirect3DIndexBuffer8 *buffer, UINT base)
{
    D8CoreDevice *device = core_device8(iface);
    struct IDirect3DIndexBuffer9 *backend = d8wg_core_index_buffer_backend8(buffer);
    if (buffer && !backend) return D3DERR_INVALIDCALL;
    HRESULT hr = device->methods->set_indices(device->backend, backend);
    if (SUCCEEDED(hr) || hr == D3DERR_DRIVERINTERNALERROR) {
        D8CoreRawState *state = device->recording ? &device->recording->raw : &device->live;
        state->indices_mask = TRUE; state->base = base;
    }
    return hr;
}
static HRESULT WINAPI core_device8_GetIndices(IDirect3DDevice8 *iface,
        IDirect3DIndexBuffer8 **out, UINT *base)
{
    D8CoreDevice *device = core_device8(iface);
    if (!out || !base) return D3DERR_INVALIDCALL;
    *out = NULL;
    struct IDirect3DIndexBuffer9 *owned = NULL;
    HRESULT hr = device->methods->get_indices(device->backend, &owned);
    if (FAILED(hr)) return hr;
    *base = device->live.base;
    return owned ? d8wg_core_wrap_index_buffer8(owned, iface, out) : D3D_OK;
}
static HRESULT WINAPI core_device8_DrawIndexedPrimitive(IDirect3DDevice8 *iface,
        D3DPRIMITIVETYPE type, UINT min, UINT count, UINT start, UINT primitives)
{
    D8CoreDevice *device = core_device8(iface);
    return d9wg_d3d9_core_draw_indexed8(device->backend, type, device->live.base,
            min, count, start, primitives);
}
static HRESULT WINAPI core_device8_ProcessVertices(IDirect3DDevice8 *iface,
        UINT src, UINT dst, UINT count, IDirect3DVertexBuffer8 *buffer, DWORD flags)
{
    struct IDirect3DVertexBuffer9 *backend = d8wg_core_vertex_buffer_backend8(buffer);
    if (!backend) return D3DERR_INVALIDCALL;
    return d9wg_d3d9_core_process_vertices8(core_device8(iface)->backend, src, dst, count, backend, flags);
}
static HRESULT WINAPI core_device8_SetRenderState(IDirect3DDevice8 *iface,
        D3DRENDERSTATETYPE state, DWORD value)
{
    D8CoreDevice *device = core_device8(iface);
    if ((UINT)state >= 256) return D3DERR_INVALIDCALL;
    D8CoreRawState *raw = device->recording ? &device->recording->raw : &device->live;
    if (!device->recording && raw->render[state] == value) return D3D_OK;
    HRESULT hr = d9wg_d3d9_core_set_render_state8(device->backend, state, value);
    if (SUCCEEDED(hr) || hr == D3DERR_DRIVERINTERNALERROR) {
        raw->render_mask[state] = TRUE; raw->render[state] = value;
    }
    return hr;
}
static HRESULT WINAPI core_device8_GetRenderState(IDirect3DDevice8 *iface,
        D3DRENDERSTATETYPE state, DWORD *out)
{
    if (!out || (UINT)state >= 256) return D3DERR_INVALIDCALL;
    *out = core_device8(iface)->live.render[state]; return D3D_OK;
}
static HRESULT WINAPI core_device8_SetTextureStageState(IDirect3DDevice8 *iface,
        DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value)
{
    D8CoreDevice *device = core_device8(iface);
    if (stage >= 8 || (UINT)state >= D9WG_TEXTURE_STAGE_STATES8) return D3DERR_INVALIDCALL;
    D8CoreRawState *raw = device->recording ? &device->recording->raw : &device->live;
    if (!device->recording && raw->texture[stage][state] == value) return D3D_OK;
    HRESULT hr = d9wg_d3d9_core_set_texture_stage_state8(device->backend, stage, state, value);
    if (SUCCEEDED(hr) || hr == D3DERR_DRIVERINTERNALERROR) {
        raw->texture_mask[stage][state] = TRUE; raw->texture[stage][state] = value;
    }
    return hr;
}
static HRESULT WINAPI core_device8_GetTextureStageState(IDirect3DDevice8 *iface,
        DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD *out)
{
    if (!out || stage >= 8 || (UINT)state >= D9WG_TEXTURE_STAGE_STATES8) return D3DERR_INVALIDCALL;
    *out = core_device8(iface)->live.texture[stage][state]; return D3D_OK;
}

static D8CoreShader *core_shader8_find(D8CoreDevice *device, DWORD handle, BOOL pixel)
{
    for (D8CoreShader *shader = device->shaders; shader; shader = shader->next)
        if (shader->handle == handle && shader->pixel == pixel && !shader->deleted) return shader;
    return NULL;
}
static HRESULT core_shader8_create(D8CoreDevice *device, const DWORD *declaration,
        const DWORD *function, BOOL pixel, DWORD *out)
{
    if (out) *out = 0;
    if (!out || (pixel ? !function : !declaration)) return D3DERR_INVALIDCALL;
    uint32_t decl_count = 0;
    UINT body_count = 0;
    if (declaration && !d9wg_declaration8_validate(declaration, D9WG_D3D8_SHADER_TOKENS, &decl_count))
        return D3DERR_INVALIDCALL;
    if (function) {
        UINT minor = D3DSHADER_VERSION_MINOR(function[0]);
        if ((function[0] & 0xffff0000u) != (pixel ? 0xffff0000u : 0xfffe0000u)
                || D3DSHADER_VERSION_MAJOR(function[0]) != 1 || minor > (pixel ? 4u : 1u)
                || !d9wg_d3d9_core_validate_shader_body8(function + 1,
                    D9WG_D3D8_SHADER_TOKENS, pixel, minor, &body_count)) return D3DERR_INVALIDCALL;
    }
    UINT decl_bytes = declaration ? (decl_count + 1u) * sizeof(DWORD) : 0;
    UINT code_bytes = function ? (body_count + 2u) * sizeof(DWORD) : 0;
    D8CoreShader *shader = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*shader) + decl_bytes + code_bytes);
    if (!shader) return E_OUTOFMEMORY;
    HRESULT hr;
    if (pixel) hr = d9wg_d3d9_core_pixel_shader8(device->backend, function,
            (struct IDirect3DPixelShader9 **)&shader->program);
    else hr = d9wg_d3d9_core_vertex_shader8(device->backend, declaration, function,
            &shader->declaration, (struct IDirect3DVertexShader9 **)&shader->program);
    if (FAILED(hr)) { HeapFree(GetProcessHeap(), 0, shader); return hr; }
    if (shader->declaration) {
        hr = d9wg_d3d9_core_shader_storage_add_ref8(device->backend, (IUnknown *)shader->declaration);
        if (FAILED(hr)) goto fail_public;
    }
    if (shader->program) {
        hr = d9wg_d3d9_core_shader_storage_add_ref8(device->backend, shader->program);
        if (FAILED(hr)) {
            if (shader->declaration) d9wg_d3d9_core_shader_storage_release8(device->backend, (IUnknown *)shader->declaration);
            goto fail_public;
        }
    }
    if (shader->declaration) IUnknown_Release((IUnknown *)shader->declaration);
    if (shader->program) IUnknown_Release(shader->program);
    shader->device = device; shader->pixel = pixel; shader->refs = 1;
    shader->declaration_bytes = decl_bytes; shader->function_bytes = code_bytes;
    if (decl_bytes) CopyMemory(shader->bytes, declaration, decl_bytes);
    if (code_bytes) CopyMemory(shader->bytes + decl_bytes, function, code_bytes);
    do {
        shader->handle = ((DWORD)InterlockedIncrement(&g_core_shader8_token) << 1) | 1u;
    } while (core_shader8_find(device, shader->handle, FALSE) || core_shader8_find(device, shader->handle, TRUE));
    shader->next = device->shaders; device->shaders = shader;
    *out = shader->handle; return D3D_OK;
fail_public:
    if (shader->declaration) IUnknown_Release((IUnknown *)shader->declaration);
    if (shader->program) IUnknown_Release(shader->program);
    HeapFree(GetProcessHeap(), 0, shader); return hr;
}
static HRESULT WINAPI core_device8_CreateVertexShader(IDirect3DDevice8 *iface,
        const DWORD *declaration, const DWORD *function, DWORD *out, DWORD usage)
{
    (void)usage;
    return core_shader8_create(core_device8(iface), declaration, function, FALSE, out);
}
static HRESULT WINAPI core_device8_CreatePixelShader(IDirect3DDevice8 *iface,
        const DWORD *function, DWORD *out)
{
    return core_shader8_create(core_device8(iface), NULL, function, TRUE, out);
}
static HRESULT core_shader8_bind(D8CoreDevice *device, BOOL pixel, DWORD handle, D8CoreShader *shader)
{
    HRESULT hr;
    if (pixel) hr = device->methods->set_pixel_shader(device->backend,
            shader ? (struct IDirect3DPixelShader9 *)shader->program : NULL);
    else if (shader) hr = d9wg_d3d9_core_set_vertex_shader8(device->backend,
            shader->declaration, (struct IDirect3DVertexShader9 *)shader->program);
    else hr = d9wg_d3d9_core_set_fvf8(device->backend, handle);
    if (FAILED(hr)) return hr;
    D8CoreRawState *raw = device->recording ? &device->recording->raw : &device->live;
    if (pixel) {
        core_shader8_assign(&raw->ps, shader); raw->pixel = handle; raw->pixel_mask = TRUE;
    } else {
        core_shader8_assign(&raw->vs, shader); raw->vertex = handle; raw->vertex_mask = TRUE;
    }
    return hr;
}
static HRESULT WINAPI core_device8_SetVertexShader(IDirect3DDevice8 *iface, DWORD handle)
{
    D8CoreDevice *device = core_device8(iface);
    D8CoreShader *shader = (handle & 1u) ? core_shader8_find(device, handle, FALSE) : NULL;
    if ((handle & 1u) && !shader) return D3DERR_INVALIDCALL;
    return core_shader8_bind(device, FALSE, handle, shader);
}
static HRESULT WINAPI core_device8_SetPixelShader(IDirect3DDevice8 *iface, DWORD handle)
{
    D8CoreDevice *device = core_device8(iface);
    D8CoreShader *shader = handle ? core_shader8_find(device, handle, TRUE) : NULL;
    if (handle && !shader) return D3DERR_INVALIDCALL;
    return core_shader8_bind(device, TRUE, handle, shader);
}
static HRESULT WINAPI core_device8_GetVertexShader(IDirect3DDevice8 *iface, DWORD *out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = core_device8(iface)->live.vertex; return D3D_OK;
}
static HRESULT WINAPI core_device8_GetPixelShader(IDirect3DDevice8 *iface, DWORD *out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = core_device8(iface)->live.pixel; return D3D_OK;
}
static HRESULT core_shader8_delete(D8CoreDevice *device, DWORD handle, BOOL pixel)
{
    D8CoreShader *shader = core_shader8_find(device, handle, pixel);
    if (!shader) return D3DERR_INVALIDCALL;
    shader->deleted = TRUE;
    if ((pixel ? device->live.pixel : device->live.vertex) == handle)
        core_shader8_bind(device, pixel, 0, NULL);
    core_shader8_release(shader); return D3D_OK;
}
static HRESULT WINAPI core_device8_DeleteVertexShader(IDirect3DDevice8 *iface, DWORD handle)
{ return core_shader8_delete(core_device8(iface), handle, FALSE); }
static HRESULT WINAPI core_device8_DeletePixelShader(IDirect3DDevice8 *iface, DWORD handle)
{ return core_shader8_delete(core_device8(iface), handle, TRUE); }
static HRESULT core_shader8_bytes(D8CoreDevice *device, DWORD handle, BOOL pixel,
        BOOL declaration, void *out, DWORD *size)
{
    D8CoreShader *shader = core_shader8_find(device, handle, pixel);
    if (!shader || !size) return D3DERR_INVALIDCALL;
    DWORD bytes = declaration ? shader->declaration_bytes : shader->function_bytes;
    if (!bytes || !out) { *size = bytes; return D3D_OK; }
    if (*size < bytes) return D3DERR_INVALIDCALL;
    CopyMemory(out, shader->bytes + (declaration ? 0 : shader->declaration_bytes), bytes);
    *size = bytes; return D3D_OK;
}
static HRESULT WINAPI core_device8_GetVertexShaderDeclaration(IDirect3DDevice8 *iface,
        DWORD handle, void *out, DWORD *size)
{ return core_shader8_bytes(core_device8(iface), handle, FALSE, TRUE, out, size); }
static HRESULT WINAPI core_device8_GetVertexShaderFunction(IDirect3DDevice8 *iface,
        DWORD handle, void *out, DWORD *size)
{ return core_shader8_bytes(core_device8(iface), handle, FALSE, FALSE, out, size); }
static HRESULT WINAPI core_device8_GetPixelShaderFunction(IDirect3DDevice8 *iface,
        DWORD handle, void *out, DWORD *size)
{ return core_shader8_bytes(core_device8(iface), handle, TRUE, FALSE, out, size); }

static D8CoreBlock **core_block8_link(D8CoreDevice *device, DWORD token)
{
    D8CoreBlock **link = &device->blocks;
    while (*link && (*link)->token != token) link = &(*link)->next;
    return link;
}
static void core_raw8_capture(D8CoreRawState *dst, const D8CoreRawState *src)
{
    for (UINT i = 0; i < 256; ++i) if (dst->render_mask[i]) dst->render[i] = src->render[i];
    for (UINT s = 0; s < 8; ++s)
        for (UINT i = 0; i < D9WG_TEXTURE_STAGE_STATES8; ++i) if (dst->texture_mask[s][i]) dst->texture[s][i] = src->texture[s][i];
    if (dst->vertex_mask) { dst->vertex = src->vertex; core_shader8_assign(&dst->vs, src->vs); }
    if (dst->pixel_mask) { dst->pixel = src->pixel; core_shader8_assign(&dst->ps, src->ps); }
    if (dst->indices_mask) dst->base = src->base;
}
static void core_block8_scope(D8CoreBlock *block, UINT type)
{
    D8CoreRawState *raw = &block->raw;
    block->type = type;
    if (type == D3DSBT_ALL || type == D3DSBT_PIXELSTATE) {
        for (UINT i = 0; i < sizeof(g_pixel_state_render_states) / sizeof(g_pixel_state_render_states[0]); ++i)
            raw->render_mask[g_pixel_state_render_states[i]] = TRUE;
        raw->pixel_mask = TRUE;
    }
    if (type == D3DSBT_ALL || type == D3DSBT_VERTEXSTATE) {
        for (UINT i = 0; i < sizeof(g_vertex_state_render_states) / sizeof(g_vertex_state_render_states[0]); ++i)
            raw->render_mask[g_vertex_state_render_states[i]] = TRUE;
        raw->vertex_mask = TRUE;
    }
    for (UINT s = 0; s < 8; ++s)
        for (UINT i = 0; i < D9WG_TEXTURE_STAGE_STATES8; ++i) raw->texture_mask[s][i] = d9wg_state_block_texture_state(type, i);
    if (type == D3DSBT_ALL) {
        FillMemory(raw->render_mask, sizeof(raw->render_mask), 1); raw->indices_mask = TRUE;
    }
}
static HRESULT WINAPI core_block8_record(struct IDirect3DDevice9 *backend, const void *context)
{
    const D8CoreBlock *block = context;
    const D8CoreRawState *raw = &block->raw;
    HRESULT hr = d9wg_d3d9_core_record_common_state8(backend, block->type, block->token);
    if (FAILED(hr)) return hr;
    for (UINT i = 0; i < 256; ++i) {
        if (!raw->render_mask[i]) continue;
        hr = d9wg_d3d9_core_set_render_state8(backend, i, raw->render[i]);
        if (FAILED(hr)) return hr;
    }
    for (UINT s = 0; s < 8; ++s) for (UINT i = 0; i < D9WG_TEXTURE_STAGE_STATES8; ++i) {
        if (!raw->texture_mask[s][i]) continue;
        hr = d9wg_d3d9_core_set_texture_stage_state8(backend, s, i, raw->texture[s][i]);
        if (FAILED(hr)) return hr;
    }
    if (raw->vertex_mask) {
        if (raw->vs) hr = d9wg_d3d9_core_set_vertex_shader8(backend, raw->vs->declaration,
                (struct IDirect3DVertexShader9 *)raw->vs->program);
        else hr = d9wg_d3d9_core_set_fvf8(backend, raw->vertex);
        if (FAILED(hr)) return hr;
    }
    if (raw->pixel_mask) {
        const D9WGDeviceMethods8 *methods = d9wg_d3d9_core_device_methods8(backend);
        hr = methods->set_pixel_shader(backend, raw->ps ? (struct IDirect3DPixelShader9 *)raw->ps->program : NULL);
    }
    return hr;
}
static HRESULT WINAPI core_device8_BeginStateBlock(IDirect3DDevice8 *iface)
{
    D8CoreDevice *device = core_device8(iface);
    if (device->recording) return D3DERR_INVALIDCALL;
    D8CoreBlock *block = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*block));
    if (!block) return E_OUTOFMEMORY;
    HRESULT hr = d9wg_d3d9_core_begin_state_block8(device->backend);
    if (FAILED(hr)) core_block8_free(block);
    else device->recording = block;
    return hr;
}
static HRESULT WINAPI core_device8_EndStateBlock(IDirect3DDevice8 *iface, DWORD *out)
{
    D8CoreDevice *device = core_device8(iface);
    D8CoreBlock *block = device->recording;
    if (!block || !out) return D3DERR_INVALIDCALL;
    HRESULT hr = d9wg_d3d9_core_end_state_block_handle8(device->backend, &block->token);
    if (FAILED(hr)) return hr;
    device->recording = NULL;
    block->next = device->blocks; device->blocks = block;
    *out = block->token; return D3D_OK;
}
static HRESULT WINAPI core_device8_CreateStateBlock(IDirect3DDevice8 *iface,
        D3DSTATEBLOCKTYPE type, DWORD *out)
{
    D8CoreDevice *device = core_device8(iface);
    if (!out || device->recording || type < D3DSBT_ALL || type > D3DSBT_VERTEXSTATE)
        return D3DERR_INVALIDCALL;
    D8CoreBlock *block = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*block));
    if (!block) return E_OUTOFMEMORY;
    core_block8_scope(block, type); core_raw8_capture(&block->raw, &device->live);
    DWORD token = 0;
    HRESULT hr = d9wg_d3d9_core_create_state_block_handle8(device->backend, core_block8_record, block, &token);
    if (FAILED(hr)) { core_block8_free(block); return hr; }
    block->token = token; block->next = device->blocks; device->blocks = block;
    *out = token; return D3D_OK;
}
static HRESULT WINAPI core_device8_CaptureStateBlock(IDirect3DDevice8 *iface, DWORD token)
{
    D8CoreDevice *device = core_device8(iface);
    D8CoreBlock *block = *core_block8_link(device, token);
    if (device->recording || !block) return D3DERR_INVALIDCALL;
    D8CoreBlock *replacement = HeapAlloc(GetProcessHeap(), 0, sizeof(*replacement));
    if (!replacement) return E_OUTOFMEMORY;
    CopyMemory(replacement, block, sizeof(*replacement));
    replacement->raw.vs = replacement->raw.ps = NULL;
    core_raw8_capture(&replacement->raw, &device->live);
    HRESULT hr = d9wg_d3d9_core_capture_state_block_handle8(device->backend, token,
            core_block8_record, replacement);
    if (FAILED(hr)) { core_block8_free(replacement); return hr; }
    *core_block8_link(device, token) = replacement;
    core_block8_free(block); return D3D_OK;
}
typedef struct D8CoreApplyContext {
    D8CoreDevice *device;
    const D8CoreRawState *raw;
} D8CoreApplyContext;
static HRESULT WINAPI core_block8_apply_raw(void *context, UINT group,
        UINT index, HRESULT result)
{
    D8CoreApplyContext *apply = context;
    D8CoreDevice *device = apply->device;
    const D8CoreRawState *raw = apply->raw;
    HRESULT hr;
    switch (group) {
    case D9WG_APPLY8_RENDER:
        for (UINT i = 0; i < 256; ++i) if (raw->render_mask[i]) {
            hr = core_device8_SetRenderState(&device->iface, (D3DRENDERSTATETYPE)i, raw->render[i]);
            if (FAILED(hr)) return hr;
        }
        break;
    case D9WG_APPLY8_TEXTURE_STAGE:
        for (UINT i = 0; i < D9WG_TEXTURE_STAGE_STATES8; ++i) if (raw->texture_mask[index][i]) {
            hr = core_device8_SetTextureStageState(&device->iface, index,
                    (D3DTEXTURESTAGESTATETYPE)i, raw->texture[index][i]);
            if (FAILED(hr)) return hr;
        }
        break;
    case D9WG_APPLY8_VERTEX:
        if (raw->vertex_mask) return core_shader8_bind(device, FALSE, raw->vertex, raw->vs);
        break;
    case D9WG_APPLY8_PIXEL:
        if (raw->pixel_mask) return core_shader8_bind(device, TRUE, raw->pixel, raw->ps);
        break;
    case D9WG_APPLY8_INDICES:
        if (raw->indices_mask && (SUCCEEDED(result) || result == D3DERR_DRIVERINTERNALERROR))
            device->live.base = raw->base;
        break;
    default:
        return D3DERR_INVALIDCALL;
    }
    return result;
}
static HRESULT WINAPI core_device8_ApplyStateBlock(IDirect3DDevice8 *iface, DWORD token)
{
    D8CoreDevice *device = core_device8(iface);
    if (!token && !device->recording) return D3D_OK;
    D8CoreBlock *block = *core_block8_link(device, token);
    if (device->recording || !block) return D3DERR_INVALIDCALL;
    /* Resource setters may release private-data objects and reenter the
     * frontend. Retain a detached raw snapshot and the public device while
     * the common core independently pins its actual resource snapshot. */
    D8CoreRawState raw = block->raw;
    raw.vs = raw.ps = NULL;
    core_raw8_capture(&raw, &block->raw);
    D8CoreApplyContext context = {device, &raw};
    core_device8_AddRef(iface);
    HRESULT hr = d9wg_d3d9_core_apply_state_block_frontend8(device->backend,
            token, core_block8_apply_raw, &context);
    core_raw8_release(&raw);
    core_device8_Release(iface);
    return hr;
}
static HRESULT WINAPI core_device8_DeleteStateBlock(IDirect3DDevice8 *iface, DWORD token)
{
    D8CoreDevice *device = core_device8(iface);
    D8CoreBlock **link = core_block8_link(device, token), *block = *link;
    if (device->recording || !block) return D3DERR_INVALIDCALL;
    HRESULT hr = d9wg_d3d9_core_delete_state_block_handle8(device->backend, token);
    if (FAILED(hr)) return hr;
    *link = block->next; core_block8_free(block); return D3D_OK;
}
static HRESULT WINAPI core_device8_Reset(IDirect3DDevice8 *iface, D3DPRESENT_PARAMETERS *parameters)
{
    D8CoreDevice *device = core_device8(iface);
    UINT old_epoch = 0, epoch = 0;
    BOOL recording = FALSE, auto_depth = FALSE;
    d9wg_d3d9_core_device_state8(device->backend, &old_epoch, &recording, &auto_depth);
    HRESULT hr = d9wg_d3d9_core_reset8(device->backend, parameters);
    d9wg_d3d9_core_device_state8(device->backend, &epoch, &recording, &auto_depth);
    if (!recording) { D8CoreBlock *block = device->recording; device->recording = NULL; core_block8_free(block); }
    if (epoch != old_epoch) {
        core_raw8_release(&device->live);
        device->live.vertex = device->live.pixel = device->live.base = 0;
        core_device8_defaults(device, auto_depth);
    }
    if (SUCCEEDED(hr)) core_device8_return_parameters(parameters);
    return hr;
}
HRESULT WINAPI d8wg_core_create_device8(IDirect3D8 *parent, struct IDirect3D9 *backend_parent,
        UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior,
        D3DPRESENT_PARAMETERS *parameters, IDirect3DDevice8 **out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = NULL;
    if (!parent || !backend_parent || !parameters) return D3DERR_INVALIDCALL;
    D8CoreDevice *device = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*device));
    if (!device) return E_OUTOFMEMORY;
    HRESULT hr = d9wg_d3d9_core_create_device8(backend_parent, adapter, type, focus,
            behavior, parameters, &device->backend);
    if (FAILED(hr)) { HeapFree(GetProcessHeap(), 0, device); return hr; }
    device->iface.lpVtbl = &g_core_device8_vtbl;
    device->refs = device->metadata_refs = 1;
    device->parent = parent;
    device->methods = d9wg_d3d9_core_device_methods8(device->backend);
    IDirect3D8_AddRef(parent);
    core_device8_defaults(device, parameters->EnableAutoDepthStencil);
    hr = d9wg_d3d9_core_attach_device_view8(device->backend, device, core_device8_cleanup, core_device8_retire);
    if (FAILED(hr)) {
        IUnknown_Release((IUnknown *)device->backend);
        core_device8_metadata_release(device); return hr;
    }
    core_device8_return_parameters(parameters);
    *out = &device->iface; return D3D_OK;
}

static IDirect3DDevice8Vtbl g_core_device8_vtbl = {
    .QueryInterface = core_device8_QueryInterface,
    .AddRef = core_device8_AddRef,
    .Release = core_device8_Release,
    .TestCooperativeLevel = core_device8_TestCooperativeLevel,
    .GetAvailableTextureMem = core_device8_GetAvailableTextureMem,
    .ResourceManagerDiscardBytes = core_device8_ResourceManagerDiscardBytes,
    .GetDirect3D = core_device8_GetDirect3D,
    .GetDeviceCaps = core_device8_GetDeviceCaps,
    .GetDisplayMode = core_device8_GetDisplayMode,
    .GetCreationParameters = core_device8_GetCreationParameters,
    .SetCursorProperties = core_device8_SetCursorProperties,
    .SetCursorPosition = core_device8_SetCursorPosition,
    .ShowCursor = core_device8_ShowCursor,
    .CreateAdditionalSwapChain = core_device8_CreateAdditionalSwapChain,
    .Reset = core_device8_Reset,
    .Present = core_device8_Present,
    .GetBackBuffer = core_device8_GetBackBuffer,
    .GetRasterStatus = core_device8_GetRasterStatus,
    .SetGammaRamp = core_device8_SetGammaRamp,
    .GetGammaRamp = core_device8_GetGammaRamp,
    .CreateTexture = core_device8_CreateTexture,
    .CreateVolumeTexture = core_device8_CreateVolumeTexture,
    .CreateCubeTexture = core_device8_CreateCubeTexture,
    .CreateVertexBuffer = core_device8_CreateVertexBuffer,
    .CreateIndexBuffer = core_device8_CreateIndexBuffer,
    .CreateRenderTarget = core_device8_CreateRenderTarget,
    .CreateDepthStencilSurface = core_device8_CreateDepthStencilSurface,
    .CreateImageSurface = core_device8_CreateImageSurface,
    .CopyRects = core_device8_CopyRects,
    .UpdateTexture = core_device8_UpdateTexture,
    .GetFrontBuffer = core_device8_GetFrontBuffer,
    .SetRenderTarget = core_device8_SetRenderTarget,
    .GetRenderTarget = core_device8_GetRenderTarget,
    .GetDepthStencilSurface = core_device8_GetDepthStencilSurface,
    .BeginScene = core_device8_BeginScene,
    .EndScene = core_device8_EndScene,
    .Clear = core_device8_Clear,
    .SetTransform = core_device8_SetTransform,
    .GetTransform = core_device8_GetTransform,
    .MultiplyTransform = core_device8_MultiplyTransform,
    .SetViewport = core_device8_SetViewport,
    .GetViewport = core_device8_GetViewport,
    .SetMaterial = core_device8_SetMaterial,
    .GetMaterial = core_device8_GetMaterial,
    .SetLight = core_device8_SetLight,
    .GetLight = core_device8_GetLight,
    .LightEnable = core_device8_LightEnable,
    .GetLightEnable = core_device8_GetLightEnable,
    .SetClipPlane = core_device8_SetClipPlane,
    .GetClipPlane = core_device8_GetClipPlane,
    .SetRenderState = core_device8_SetRenderState,
    .GetRenderState = core_device8_GetRenderState,
    .BeginStateBlock = core_device8_BeginStateBlock,
    .EndStateBlock = core_device8_EndStateBlock,
    .ApplyStateBlock = core_device8_ApplyStateBlock,
    .CaptureStateBlock = core_device8_CaptureStateBlock,
    .DeleteStateBlock = core_device8_DeleteStateBlock,
    .CreateStateBlock = core_device8_CreateStateBlock,
    .SetClipStatus = core_device8_SetClipStatus,
    .GetClipStatus = core_device8_GetClipStatus,
    .GetTexture = core_device8_GetTexture,
    .SetTexture = core_device8_SetTexture,
    .GetTextureStageState = core_device8_GetTextureStageState,
    .SetTextureStageState = core_device8_SetTextureStageState,
    .ValidateDevice = core_device8_ValidateDevice,
    .GetInfo = core_device8_GetInfo,
    .SetPaletteEntries = core_device8_SetPaletteEntries,
    .GetPaletteEntries = core_device8_GetPaletteEntries,
    .SetCurrentTexturePalette = core_device8_SetCurrentTexturePalette,
    .GetCurrentTexturePalette = core_device8_GetCurrentTexturePalette,
    .DrawPrimitive = core_device8_DrawPrimitive,
    .DrawIndexedPrimitive = core_device8_DrawIndexedPrimitive,
    .DrawPrimitiveUP = core_device8_DrawPrimitiveUP,
    .DrawIndexedPrimitiveUP = core_device8_DrawIndexedPrimitiveUP,
    .ProcessVertices = core_device8_ProcessVertices,
    .CreateVertexShader = core_device8_CreateVertexShader,
    .SetVertexShader = core_device8_SetVertexShader,
    .GetVertexShader = core_device8_GetVertexShader,
    .DeleteVertexShader = core_device8_DeleteVertexShader,
    .SetVertexShaderConstant = core_device8_SetVertexShaderConstant,
    .GetVertexShaderConstant = core_device8_GetVertexShaderConstant,
    .GetVertexShaderDeclaration = core_device8_GetVertexShaderDeclaration,
    .GetVertexShaderFunction = core_device8_GetVertexShaderFunction,
    .SetStreamSource = core_device8_SetStreamSource,
    .GetStreamSource = core_device8_GetStreamSource,
    .SetIndices = core_device8_SetIndices,
    .GetIndices = core_device8_GetIndices,
    .CreatePixelShader = core_device8_CreatePixelShader,
    .SetPixelShader = core_device8_SetPixelShader,
    .GetPixelShader = core_device8_GetPixelShader,
    .DeletePixelShader = core_device8_DeletePixelShader,
    .SetPixelShaderConstant = core_device8_SetPixelShaderConstant,
    .GetPixelShaderConstant = core_device8_GetPixelShaderConstant,
    .GetPixelShaderFunction = core_device8_GetPixelShaderFunction,
    .DrawRectPatch = core_device8_DrawRectPatch,
    .DrawTriPatch = core_device8_DrawTriPatch,
    .DeletePatch = core_device8_DeletePatch,
};
