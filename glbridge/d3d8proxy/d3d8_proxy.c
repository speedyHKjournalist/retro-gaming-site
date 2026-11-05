/* SDK8 API translation frontend. Device/resource storage, rendering, window
 * tracking and transport are implemented once by the shared D3D9 core.
 * The previous backend is frozen under tests/fixtures for regression comparison;
 * it is never compiled or linked into the shipping DLL. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d8.h>
#include <stdint.h>
#include "d3d8_core_device.h"
#include "../runtime/d9wg_process_aliases.h"
/* These legacy D3D shade-cap bits are omitted by some d3d8.h variants. */
#ifndef D3DPSHADECAPS_COLORFLATRGB
#define D3DPSHADECAPS_COLORFLATRGB 0x00000002u
#endif
#ifndef D3DPSHADECAPS_ALPHAFLATBLEND
#define D3DPSHADECAPS_ALPHAFLATBLEND 0x00001000u
#endif

#define D8WG_LOG_PREFIX "[d3d8-webgpu] "
#ifdef D8WG_DIAGNOSTIC_TRACE
#define V86WG_DIAGNOSTIC_COMPONENT "d3d8-webgpu"
#define V86WG_DIAGNOSTIC_FILE_STEM "d3d8_trace"
#include "../diagnostic_trace.h"
#define D8WG_TRACE_ERROR(result) \
    v86wg_diagnostic_hresult((HRESULT)(result), __func__, __LINE__)
#define D8WG_TRACE(...) v86wg_diagnostic_write(__VA_ARGS__)
#else
#define D8WG_TRACE_ERROR(result) (result)
#define D8WG_TRACE(...) ((void)0)
#endif

#define D8WG_MAX_STREAMS 16u
#define D8WG_MAX_TEXTURE_STAGES 8u
#define D8WG_MAX_LIGHTS 8u
#define D8WG_MAX_CLIP_PLANES 6u
#define D8WG_MAX_VS_CONSTANTS 96u
#define D3D_SDK_VERSION_DX80 120u
#define D3D_SDK_VERSION_DX81 220u
typedef struct D8Direct3D {
    IDirect3D8 iface;
    LONG refcount;
    struct IDirect3D9 *backend;
} D8Direct3D;
static IDirect3D8Vtbl g_d3d_vtbl;

static D8Direct3D *d3d_from_iface(IDirect3D8 *iface)
{
    return (D8Direct3D *)iface;
}

static BOOL guid_equal(REFIID left, REFIID right)
{
    UINT index;
    if (!left || !right || left->Data1 != right->Data1
            || left->Data2 != right->Data2 || left->Data3 != right->Data3)
        return FALSE;
    for (index = 0; index < 8; ++index) {
        if (left->Data4[index] != right->Data4[index]) return FALSE;
    }
    return TRUE;
}

static BOOL iid_is_unknown(REFIID iid)
{
    static const IID unknown = { 0x00000000, 0x0000, 0x0000,
        { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
    return guid_equal(iid, &unknown);
}

static void fill_caps(D3DCAPS8 *caps)
{
    d9wg_d3d9_core_caps8(caps);
    caps->Caps = 0;
    caps->Caps3 = 0;
    caps->Caps2 &= (D3DCAPS2_CANRENDERWINDOWED
            | D3DCAPS2_CANMANAGERESOURCE
            | D3DCAPS2_DYNAMICTEXTURES
            | D3DCAPS2_FULLSCREENGAMMA);
    caps->DevCaps &= (D3DDEVCAPS_HWRASTERIZATION
            | D3DDEVCAPS_HWTRANSFORMANDLIGHT
            | D3DDEVCAPS_DRAWPRIMTLVERTEX
            | D3DDEVCAPS_EXECUTESYSTEMMEMORY
            | D3DDEVCAPS_EXECUTEVIDEOMEMORY
            | D3DDEVCAPS_TEXTURESYSTEMMEMORY
            | D3DDEVCAPS_TEXTUREVIDEOMEMORY
            | D3DDEVCAPS_TLVERTEXSYSTEMMEMORY
            | D3DDEVCAPS_TLVERTEXVIDEOMEMORY
            | D3DDEVCAPS_CANRENDERAFTERFLIP
            | D3DDEVCAPS_DRAWPRIMITIVES2
            | D3DDEVCAPS_DRAWPRIMITIVES2EX);
    caps->PrimitiveMiscCaps &= (D3DPMISCCAPS_CULLNONE
            | D3DPMISCCAPS_CULLCW
            | D3DPMISCCAPS_CULLCCW
            | D3DPMISCCAPS_COLORWRITEENABLE
            | D3DPMISCCAPS_BLENDOP
            | D3DPMISCCAPS_MASKZ
            | D3DPMISCCAPS_CLIPTLVERTS
            | D3DPMISCCAPS_TSSARGTEMP);
    caps->RasterCaps &= (D3DPRASTERCAPS_ZTEST
            | D3DPRASTERCAPS_ZBIAS
            | D3DPRASTERCAPS_FOGVERTEX
            | D3DPRASTERCAPS_FOGTABLE
            | D3DPRASTERCAPS_WFOG
            | D3DPRASTERCAPS_FOGRANGE
            | D3DPRASTERCAPS_COLORPERSPECTIVE
            | D3DPRASTERCAPS_ANISOTROPY);
    caps->SrcBlendCaps &= (0x1FFFu);
    caps->DestBlendCaps &= (0x1FFFu);
    caps->StencilCaps &= (D3DSTENCILCAPS_KEEP
            | D3DSTENCILCAPS_ZERO
            | D3DSTENCILCAPS_REPLACE
            | D3DSTENCILCAPS_INCRSAT
            | D3DSTENCILCAPS_DECRSAT
            | D3DSTENCILCAPS_INVERT
            | D3DSTENCILCAPS_INCR
            | D3DSTENCILCAPS_DECR);
    caps->ShadeCaps &= (D3DPSHADECAPS_COLORFLATRGB
            | D3DPSHADECAPS_COLORGOURAUDRGB
            | D3DPSHADECAPS_SPECULARGOURAUDRGB
            | D3DPSHADECAPS_FOGGOURAUD
            | D3DPSHADECAPS_ALPHAFLATBLEND
            | D3DPSHADECAPS_ALPHAGOURAUDBLEND);
    caps->TextureCaps &= (D3DPTEXTURECAPS_ALPHA
            | D3DPTEXTURECAPS_MIPMAP
            | D3DPTEXTURECAPS_PERSPECTIVE
            | D3DPTEXTURECAPS_ALPHAPALETTE
            | D3DPTEXTURECAPS_PROJECTED
            | D3DPTEXTURECAPS_CUBEMAP
            | D3DPTEXTURECAPS_MIPCUBEMAP
            | D3DPTEXTURECAPS_VOLUMEMAP
            | D3DPTEXTURECAPS_MIPVOLUMEMAP);
    caps->TextureAddressCaps &= (D3DPTADDRESSCAPS_WRAP
            | D3DPTADDRESSCAPS_MIRROR
            | D3DPTADDRESSCAPS_CLAMP
            | D3DPTADDRESSCAPS_BORDER
            | D3DPTADDRESSCAPS_INDEPENDENTUV);
    /* This legacy path applies one addressing set to 2D and volume textures.
     * Preserve that relation even though D3D9's base volume field is zero. */
    caps->VolumeTextureAddressCaps = caps->TextureAddressCaps;
    caps->LineCaps &= (D3DLINECAPS_TEXTURE
            | D3DLINECAPS_ZTEST
            | D3DLINECAPS_BLEND
            | D3DLINECAPS_ALPHACMP
            | D3DLINECAPS_FOG);
    caps->TextureOpCaps &= (D3DTEXOPCAPS_DISABLE
            | D3DTEXOPCAPS_SELECTARG1
            | D3DTEXOPCAPS_SELECTARG2
            | D3DTEXOPCAPS_MODULATE
            | D3DTEXOPCAPS_MODULATE2X
            | D3DTEXOPCAPS_MODULATE4X
            | D3DTEXOPCAPS_ADD
            | D3DTEXOPCAPS_ADDSIGNED
            | D3DTEXOPCAPS_ADDSIGNED2X
            | D3DTEXOPCAPS_SUBTRACT
            | D3DTEXOPCAPS_ADDSMOOTH
            | D3DTEXOPCAPS_BLENDDIFFUSEALPHA
            | D3DTEXOPCAPS_BLENDTEXTUREALPHA
            | D3DTEXOPCAPS_BLENDFACTORALPHA
            | D3DTEXOPCAPS_BLENDTEXTUREALPHAPM
            | D3DTEXOPCAPS_BLENDCURRENTALPHA
            | D3DTEXOPCAPS_DOTPRODUCT3
            | D3DTEXOPCAPS_MULTIPLYADD
            | D3DTEXOPCAPS_LERP
            | D3DTEXOPCAPS_BUMPENVMAP
            | D3DTEXOPCAPS_BUMPENVMAPLUMINANCE);
    caps->VertexProcessingCaps &= (D3DVTXPCAPS_TEXGEN
            | D3DVTXPCAPS_MATERIALSOURCE7
            | D3DVTXPCAPS_DIRECTIONALLIGHTS
            | D3DVTXPCAPS_POSITIONALLIGHTS
            | D3DVTXPCAPS_LOCALVIEWER
            | D3DVTXPCAPS_TWEENING);
    caps->Caps2 |= D3DCAPS2_CANRENDERWINDOWED;
    if (caps->MaxAnisotropy > 1) caps->RasterCaps |= D3DPRASTERCAPS_ANISOTROPY;
    caps->ShadeCaps |= D3DPSHADECAPS_COLORFLATRGB | D3DPSHADECAPS_ALPHAFLATBLEND;
    caps->TextureCaps |= D3DPTEXTURECAPS_ALPHAPALETTE;
    /* Guard-band claims and D3D9 diagnostic/permissive profiles are outside
     * this frontend's legacy contract. Shader1.x is the D3D8 implementation. */
    caps->GuardBandLeft = caps->GuardBandTop = 0.0f;
    caps->GuardBandRight = caps->GuardBandBottom = caps->ExtentsAdjust = 0.0f;
    DWORD coords = caps->FVFCaps & D3DFVFCAPS_TEXCOORDCOUNTMASK;
    if (coords > 8) coords = 8;
    caps->FVFCaps = coords | (caps->FVFCaps & D3DFVFCAPS_PSIZE);
    if (caps->MaxStreams > D8WG_MAX_STREAMS) caps->MaxStreams = D8WG_MAX_STREAMS;
    if (caps->MaxTextureBlendStages > D8WG_MAX_TEXTURE_STAGES)
        caps->MaxTextureBlendStages = D8WG_MAX_TEXTURE_STAGES;
    if (caps->MaxSimultaneousTextures > D8WG_MAX_TEXTURE_STAGES)
        caps->MaxSimultaneousTextures = D8WG_MAX_TEXTURE_STAGES;
    if (caps->MaxActiveLights > D8WG_MAX_LIGHTS) caps->MaxActiveLights = D8WG_MAX_LIGHTS;
    if (caps->MaxUserClipPlanes > D8WG_MAX_CLIP_PLANES) caps->MaxUserClipPlanes = D8WG_MAX_CLIP_PLANES;
    if (caps->MaxVertexBlendMatrices > 4) caps->MaxVertexBlendMatrices = 4;
    if (caps->MaxVertexBlendMatrixIndex > 255) caps->MaxVertexBlendMatrixIndex = 255;
    caps->VertexShaderVersion = (DWORD)D3DVS_VERSION(1, 1);
    caps->MaxVertexShaderConst = D8WG_MAX_VS_CONSTANTS;
    caps->PixelShaderVersion = (DWORD)D3DPS_VERSION(1, 4);
    caps->MaxPixelShaderValue = 8.0f;
}

static HRESULT WINAPI d3d_query_interface(IDirect3D8 *iface, REFIID iid,
        void **object)
{
    if (!object)
        return D8WG_TRACE_ERROR(E_POINTER);
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid) && !guid_equal(iid, &IID_IDirect3D8)))
        return D8WG_TRACE_ERROR(E_NOINTERFACE);
    *object = iface;
    IDirect3D8_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI d3d_add_ref(IDirect3D8 *iface)
{
    return (ULONG)InterlockedIncrement(&d3d_from_iface(iface)->refcount);
}

static ULONG WINAPI d3d_release(IDirect3D8 *iface)
{
    D8Direct3D *d3d = d3d_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&d3d->refcount);
    if (!refs) {
        d9wg_d3d9_core_root_release(d3d->backend);
        HeapFree(GetProcessHeap(), 0, d3d);
    }
    return refs;
}

static HRESULT WINAPI d3d_register_software_device(IDirect3D8 *iface, void *initialize)
{
    return d9wg_d3d9_core_register_software(d3d_from_iface(iface)->backend, initialize);
}

static UINT WINAPI d3d_get_adapter_count(IDirect3D8 *iface)
{
    return d9wg_d3d9_core_adapter_count(d3d_from_iface(iface)->backend);
}

static HRESULT WINAPI d3d_get_adapter_identifier(IDirect3D8 *iface,
        UINT adapter, DWORD flags, D3DADAPTER_IDENTIFIER8 *identifier)
{
    return d9wg_d3d9_core_adapter_identifier8(d3d_from_iface(iface)->backend,
            adapter, flags, identifier, sizeof(*identifier));
}

static UINT WINAPI d3d_get_adapter_mode_count(IDirect3D8 *iface, UINT adapter)
{
    return d9wg_d3d9_core_adapter_mode_count8(d3d_from_iface(iface)->backend, adapter);
}

static HRESULT WINAPI d3d_enum_adapter_modes(IDirect3D8 *iface, UINT adapter,
        UINT index, D3DDISPLAYMODE *mode)
{
    return d9wg_d3d9_core_enum_adapter_modes8(d3d_from_iface(iface)->backend, adapter, index, mode);
}

static HRESULT WINAPI d3d_get_adapter_display_mode(IDirect3D8 *iface,
        UINT adapter, D3DDISPLAYMODE *mode)
{
    return d9wg_d3d9_core_adapter_display_mode(d3d_from_iface(iface)->backend, adapter, mode);
}

static HRESULT WINAPI d3d_check_device_type(IDirect3D8 *iface, UINT adapter,
        D3DDEVTYPE type, D3DFORMAT display_format,
        D3DFORMAT backbuffer_format, WINBOOL windowed)
{
    return d9wg_d3d9_core_check_type(d3d_from_iface(iface)->backend, adapter,
            type, display_format, backbuffer_format, windowed);
}

static HRESULT WINAPI d3d_check_device_format(IDirect3D8 *iface,
        UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format,
        DWORD usage, D3DRESOURCETYPE resource_type, D3DFORMAT format)
{
    return d9wg_d3d9_core_check_format8(d3d_from_iface(iface)->backend, adapter,
            type, adapter_format, usage, resource_type, format);
}

static HRESULT WINAPI d3d_check_multisample(IDirect3D8 *iface, UINT adapter,
        D3DDEVTYPE type, D3DFORMAT format, WINBOOL windowed,
        D3DMULTISAMPLE_TYPE multisample)
{
    return d9wg_d3d9_core_check_multisample8(d3d_from_iface(iface)->backend,
            adapter, type, format, windowed, multisample);
}

static HRESULT WINAPI d3d_check_depth_stencil(IDirect3D8 *iface,
        UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format,
        D3DFORMAT render_format, D3DFORMAT depth_format)
{
    return d9wg_d3d9_core_check_depth(d3d_from_iface(iface)->backend,
            adapter, type, adapter_format, render_format, depth_format);
}

static HRESULT WINAPI d3d_get_device_caps(IDirect3D8 *iface, UINT adapter,
        D3DDEVTYPE type, D3DCAPS8 *caps)
{
    (void)iface;
    if (adapter || type != D3DDEVTYPE_HAL || !caps)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    fill_caps(caps);
    return D3D_OK;
}

static HMONITOR WINAPI d3d_get_adapter_monitor(IDirect3D8 *iface, UINT adapter)
{
    return d9wg_d3d9_core_adapter_monitor(d3d_from_iface(iface)->backend, adapter);
}

static HRESULT WINAPI d3d_create_device(IDirect3D8 *iface, UINT adapter,
        D3DDEVTYPE type, HWND focus, DWORD behavior,
        D3DPRESENT_PARAMETERS *parameters, IDirect3DDevice8 **out)
{
    return d8wg_core_create_device8(iface, d3d_from_iface(iface)->backend,
            adapter, type, focus, behavior, parameters, out);
}
static void emit_hello_once(void) { d9wg_d3d9_core_hello8(); }
static IDirect3D8Vtbl g_d3d_vtbl = {
    .QueryInterface = d3d_query_interface,
    .AddRef = d3d_add_ref,
    .Release = d3d_release,
    .RegisterSoftwareDevice = d3d_register_software_device,
    .GetAdapterCount = d3d_get_adapter_count,
    .GetAdapterIdentifier = d3d_get_adapter_identifier,
    .GetAdapterModeCount = d3d_get_adapter_mode_count,
    .EnumAdapterModes = d3d_enum_adapter_modes,
    .GetAdapterDisplayMode = d3d_get_adapter_display_mode,
    .CheckDeviceType = d3d_check_device_type,
    .CheckDeviceFormat = d3d_check_device_format,
    .CheckDeviceMultiSampleType = d3d_check_multisample,
    .CheckDepthStencilMatch = d3d_check_depth_stencil,
    .GetDeviceCaps = d3d_get_device_caps,
    .GetAdapterMonitor = d3d_get_adapter_monitor,
    .CreateDevice = d3d_create_device
};

IDirect3D8 *WINAPI Direct3DCreate8(UINT sdk_version)
{
    D8Direct3D *d3d;

    D8WG_TRACE("CALL Direct3DCreate8 sdk=%lu", sdk_version);
    if (sdk_version != D3D_SDK_VERSION_DX80
            && sdk_version != D3D_SDK_VERSION_DX81) {
        D8WG_TRACE("FAIL Direct3DCreate8 sdk=%lu expected=%lu|%lu",
                sdk_version, D3D_SDK_VERSION_DX80, D3D_SDK_VERSION_DX81);
        return NULL;
    }
    d3d = (D8Direct3D *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*d3d));
    if (!d3d) {
        D8WG_TRACE("FAIL Direct3DCreate8 allocation win32_last=%lu",
                GetLastError());
        return NULL;
    }
    d3d->backend = d9wg_d3d9_core_create8();
    if (!d3d->backend) {
        D8WG_TRACE("FAIL Direct3DCreate8 backend factory unavailable");
        HeapFree(GetProcessHeap(), 0, d3d);
        return NULL;
    }
    d3d->iface.lpVtbl = &g_d3d_vtbl;
    d3d->refcount = 1;
    emit_hello_once();
    D8WG_TRACE("OK Direct3DCreate8 object=%08lX",
            (DWORD)(uintptr_t)&d3d->iface);
    return &d3d->iface;
}

/* These exports are callable before a device exists. Error strings belong to
 * the process heap, including the empty string on success / muted failure.
 * Unlike the vertex export, a NULL pixel program leaves errors untouched. */
static HRESULT shader_validation_result(HRESULT result, WINBOOL return_error,
        char **errors)
{
    if (errors) {
        const char *message = FAILED(result) && return_error
                ? "Invalid shader program.\n" : "";
        SIZE_T size = (SIZE_T)lstrlenA(message) + 1u;
        *errors = HeapAlloc(GetProcessHeap(), 0, size);
        if (!*errors) return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
        CopyMemory(*errors, message, size);
    }
    return D8WG_TRACE_ERROR(result);
}

HRESULT WINAPI ValidateVertexShader(const DWORD *shader, const DWORD *declaration,
        const D3DCAPS8 *caps, WINBOOL return_error, char **errors)
{
    HRESULT result = E_FAIL;
    if (shader && (shader[0] == D3DVS_VERSION(1, 0)
                || shader[0] == D3DVS_VERSION(1, 1))
            && (!caps || shader[0] <= caps->VertexShaderVersion)
            && d9wg_d3d9_core_validate_shader_export8(shader, declaration,
                FALSE, caps ? caps->MaxVertexShaderConst : 96u))
        result = S_OK;
    return shader_validation_result(result, return_error, errors);
}

HRESULT WINAPI ValidatePixelShader(const DWORD *shader, const D3DCAPS8 *caps,
        WINBOOL return_error, char **errors)
{
    if (!shader) return D8WG_TRACE_ERROR(E_FAIL);
    HRESULT result = E_FAIL;
    if (shader[0] >= D3DPS_VERSION(1, 0)
            && shader[0] <= D3DPS_VERSION(1, 4)
            && (!caps || shader[0] <= caps->PixelShaderVersion)
            && d9wg_d3d9_core_validate_shader_export8(shader, NULL, TRUE, 8u))
        result = S_OK;
    return shader_validation_result(result, return_error, errors);
}

void WINAPI DebugSetMute(void)
{
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_DETACH && (reserved || !g_process))
        return d9wg_d3d9_core_module(instance, reason, reserved);

    if (reason == DLL_PROCESS_ATTACH) {
        if (!d9wg_d3d9_core_module(instance, reason, reserved)) return FALSE;
        /* Borrow the core's attachment: this DLL remains one frontend owner. */
        g_process = d9wg_d3d9_core_process();
#ifdef D8WG_DIAGNOSTIC_TRACE
        v86wg_diagnostic_process_attach(instance);
#endif
    } else if (reason == DLL_PROCESS_DETACH) {
#ifdef D8WG_DIAGNOSTIC_TRACE
        DWORD pending_commands = g_command_count;
#endif
        BOOL result = d9wg_d3d9_core_module(instance, reason, reserved);
        g_process = NULL;
#ifdef D8WG_DIAGNOSTIC_TRACE
        v86wg_diagnostic_process_detach(reserved, pending_commands);
#endif
        return result;
    }
    return TRUE;
}
