// Direct3D 8 conservative capability-profile and CheckDeviceFormat audit.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>

#define TEST_WIDTH  680
#define TEST_HEIGHT 380

typedef struct FormatProbe
{
    const char *name;
    DWORD usage;
    D3DRESOURCETYPE type;
    D3DFORMAT format;
    BOOL expected_supported;
    UINT category;
} FormatProbe;

static const FormatProbe g_probes[] =
{
    {"X8R8G8B8 texture", 0, D3DRTYPE_TEXTURE, D3DFMT_X8R8G8B8, TRUE, 0},
    {"A8R8G8B8 texture", 0, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8, TRUE, 0},
    {"R5G6B5 texture", 0, D3DRTYPE_TEXTURE, D3DFMT_R5G6B5, TRUE, 0},
    {"A1R5G5B5 texture", 0, D3DRTYPE_TEXTURE, D3DFMT_A1R5G5B5, TRUE, 0},
    {"A4R4G4B4 texture", 0, D3DRTYPE_TEXTURE, D3DFMT_A4R4G4B4, TRUE, 0},
    {"L8 texture", 0, D3DRTYPE_TEXTURE, D3DFMT_L8, TRUE, 0},
    {"A8 texture", 0, D3DRTYPE_TEXTURE, D3DFMT_A8, TRUE, 0},
    {"D24S8 depth/stencil surface", D3DUSAGE_DEPTHSTENCIL,
            D3DRTYPE_SURFACE, D3DFMT_D24S8, TRUE, 0},

    {"DXT1 must be hidden", 0, D3DRTYPE_TEXTURE, D3DFMT_DXT1, FALSE, 1},
    {"DXT3 must be hidden", 0, D3DRTYPE_TEXTURE, D3DFMT_DXT3, FALSE, 1},
    {"DXT5 must be hidden", 0, D3DRTYPE_TEXTURE, D3DFMT_DXT5, FALSE, 1},
    {"cube texture must be hidden", 0, D3DRTYPE_CUBETEXTURE,
            D3DFMT_A8R8G8B8, FALSE, 1},
    {"volume texture must be hidden", 0, D3DRTYPE_VOLUMETEXTURE,
            D3DFMT_A8R8G8B8, FALSE, 1},
    {"render-target texture must be hidden", D3DUSAGE_RENDERTARGET,
            D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8, FALSE, 1}
};

static const char g_class_name[] = "V86GLD3D8CapsAuditTest";
static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_device;
static HWND g_window;
static D3DCAPS8 g_caps;
static D3DFORMAT g_adapter_format;
static UINT g_mismatch_count;
static UINT g_category_mismatches[4];
static char g_first_mismatch[240];

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d8-caps-audit] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void trace_dword(const char *name, DWORD value)
{
    char line[180];
    wsprintfA(line, "%s=0x%08lX (%lu)", name,
            (unsigned long)value, (unsigned long)value);
    trace_text(line);
}

static void trace_float_bits(const char *name, FLOAT value)
{
    union { FLOAT f; DWORD d; } bits;
    char line[180];
    bits.f = value;
    wsprintfA(line, "%s float-bits=0x%08lX", name,
            (unsigned long)bits.d);
    trace_text(line);
}

static void begin_stage(const char *stage)
{
    char title[250];
    trace_text(stage);
    wsprintfA(title, "D3D8 capability audit: calling %s", stage);
    if (g_window) SetWindowTextA(g_window, title);
}

static HRESULT fail_stage(const char *stage, HRESULT hr)
{
    char title[250];
    char line[250];
    wsprintfA(line, "[d3d8-caps-audit] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
    wsprintfA(title, "D3D8 capability audit: %s (0x%08lX)",
            stage, (unsigned long)hr);
    if (g_window) SetWindowTextA(g_window, title);
    return hr;
}

static void mismatch(const char *name, UINT category)
{
    char line[250];
    if (!g_first_mismatch[0]) lstrcpynA(g_first_mismatch, name,
            sizeof(g_first_mismatch));
    ++g_mismatch_count;
    if (category < 4) ++g_category_mismatches[category];
    wsprintfA(line, "MISMATCH #%u: %s", g_mismatch_count, name);
    trace_text(line);
}

static void require_bits(const char *name, DWORD actual, DWORD required,
        UINT category)
{
    char line[240];
    if ((actual & required) == required) return;
    wsprintfA(line, "%s is missing required tested bits", name);
    mismatch(line, category);
}

static void forbid_bits(const char *name, DWORD actual, DWORD forbidden,
        UINT category)
{
    char line[240];
    if (!(actual & forbidden)) return;
    wsprintfA(line, "%s exposes an unverified/forbidden bit", name);
    mismatch(line, category);
}

static void dump_caps(void)
{
    trace_text("---- GetDeviceCaps complete dump ----");
    trace_dword("Caps", g_caps.Caps);
    trace_dword("Caps2", g_caps.Caps2);
    trace_dword("Caps3", g_caps.Caps3);
    trace_dword("PresentationIntervals", g_caps.PresentationIntervals);
    trace_dword("CursorCaps", g_caps.CursorCaps);
    trace_dword("DevCaps", g_caps.DevCaps);
    trace_dword("PrimitiveMiscCaps", g_caps.PrimitiveMiscCaps);
    trace_dword("RasterCaps", g_caps.RasterCaps);
    trace_dword("ZCmpCaps", g_caps.ZCmpCaps);
    trace_dword("SrcBlendCaps", g_caps.SrcBlendCaps);
    trace_dword("DestBlendCaps", g_caps.DestBlendCaps);
    trace_dword("AlphaCmpCaps", g_caps.AlphaCmpCaps);
    trace_dword("ShadeCaps", g_caps.ShadeCaps);
    trace_dword("TextureCaps", g_caps.TextureCaps);
    trace_dword("TextureFilterCaps", g_caps.TextureFilterCaps);
    trace_dword("CubeTextureFilterCaps", g_caps.CubeTextureFilterCaps);
    trace_dword("VolumeTextureFilterCaps", g_caps.VolumeTextureFilterCaps);
    trace_dword("TextureAddressCaps", g_caps.TextureAddressCaps);
    trace_dword("VolumeTextureAddressCaps", g_caps.VolumeTextureAddressCaps);
    trace_dword("LineCaps", g_caps.LineCaps);
    trace_dword("MaxTextureWidth", g_caps.MaxTextureWidth);
    trace_dword("MaxTextureHeight", g_caps.MaxTextureHeight);
    trace_dword("MaxVolumeExtent", g_caps.MaxVolumeExtent);
    trace_dword("MaxTextureRepeat", g_caps.MaxTextureRepeat);
    trace_dword("MaxTextureAspectRatio", g_caps.MaxTextureAspectRatio);
    trace_dword("MaxAnisotropy", g_caps.MaxAnisotropy);
    trace_float_bits("MaxVertexW", g_caps.MaxVertexW);
    trace_float_bits("GuardBandLeft", g_caps.GuardBandLeft);
    trace_float_bits("GuardBandTop", g_caps.GuardBandTop);
    trace_float_bits("GuardBandRight", g_caps.GuardBandRight);
    trace_float_bits("GuardBandBottom", g_caps.GuardBandBottom);
    trace_float_bits("ExtentsAdjust", g_caps.ExtentsAdjust);
    trace_dword("StencilCaps", g_caps.StencilCaps);
    trace_dword("FVFCaps", g_caps.FVFCaps);
    trace_dword("TextureOpCaps", g_caps.TextureOpCaps);
    trace_dword("MaxTextureBlendStages", g_caps.MaxTextureBlendStages);
    trace_dword("MaxSimultaneousTextures", g_caps.MaxSimultaneousTextures);
    trace_dword("VertexProcessingCaps", g_caps.VertexProcessingCaps);
    trace_dword("MaxActiveLights", g_caps.MaxActiveLights);
    trace_dword("MaxUserClipPlanes", g_caps.MaxUserClipPlanes);
    trace_dword("MaxVertexBlendMatrices", g_caps.MaxVertexBlendMatrices);
    trace_dword("MaxVertexBlendMatrixIndex", g_caps.MaxVertexBlendMatrixIndex);
    trace_float_bits("MaxPointSize", g_caps.MaxPointSize);
    trace_dword("MaxPrimitiveCount", g_caps.MaxPrimitiveCount);
    trace_dword("MaxVertexIndex", g_caps.MaxVertexIndex);
    trace_dword("MaxStreams", g_caps.MaxStreams);
    trace_dword("MaxStreamStride", g_caps.MaxStreamStride);
    trace_dword("VertexShaderVersion", g_caps.VertexShaderVersion);
    trace_dword("MaxVertexShaderConst", g_caps.MaxVertexShaderConst);
    trace_dword("PixelShaderVersion", g_caps.PixelShaderVersion);
    trace_float_bits("MaxPixelShaderValue", g_caps.MaxPixelShaderValue);
}

static void audit_caps(void)
{
    DWORD tested_texture_ops = D3DTEXOPCAPS_SELECTARG1
            | D3DTEXOPCAPS_SELECTARG2 | D3DTEXOPCAPS_MODULATE
            | D3DTEXOPCAPS_ADD;
    begin_stage("audit tested fixed-pipeline caps");
    require_bits("PrimitiveMiscCaps", g_caps.PrimitiveMiscCaps,
            D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW
            | D3DPMISCCAPS_CULLCCW | D3DPMISCCAPS_COLORWRITEENABLE, 2);
    require_bits("RasterCaps", g_caps.RasterCaps,
            D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_ZBIAS
            | D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_FOGTABLE, 2);
    require_bits("SrcBlendCaps", g_caps.SrcBlendCaps,
            D3DPBLENDCAPS_SRCALPHA, 2);
    require_bits("DestBlendCaps", g_caps.DestBlendCaps,
            D3DPBLENDCAPS_INVSRCALPHA, 2);
    require_bits("StencilCaps", g_caps.StencilCaps,
            D3DSTENCILCAPS_KEEP | D3DSTENCILCAPS_REPLACE, 2);
    require_bits("TextureOpCaps", g_caps.TextureOpCaps,
            tested_texture_ops, 2);
    if (g_caps.MaxTextureBlendStages < 2)
        mismatch("MaxTextureBlendStages is below tested value 2", 2);
    if (g_caps.MaxSimultaneousTextures < 2)
        mismatch("MaxSimultaneousTextures is below tested value 2", 2);

    begin_stage("audit forbidden high-risk caps");
    forbid_bits("TextureCaps", g_caps.TextureCaps,
            D3DPTEXTURECAPS_CUBEMAP | D3DPTEXTURECAPS_VOLUMEMAP
            | D3DPTEXTURECAPS_MIPCUBEMAP | D3DPTEXTURECAPS_MIPVOLUMEMAP
            | D3DPTEXTURECAPS_CUBEMAP_POW2 | D3DPTEXTURECAPS_VOLUMEMAP_POW2,
            3);
    if (g_caps.CubeTextureFilterCaps)
        mismatch("CubeTextureFilterCaps must be zero", 3);
    if (g_caps.VolumeTextureFilterCaps)
        mismatch("VolumeTextureFilterCaps must be zero", 3);
    if (g_caps.VolumeTextureAddressCaps)
        mismatch("VolumeTextureAddressCaps must be zero", 3);
    if (g_caps.MaxVolumeExtent)
        mismatch("MaxVolumeExtent must be zero", 3);
    if (g_caps.VertexShaderVersion)
        mismatch("VertexShaderVersion must be zero", 3);
    if (g_caps.MaxVertexShaderConst)
        mismatch("MaxVertexShaderConst must be zero", 3);
    if (g_caps.PixelShaderVersion)
        mismatch("PixelShaderVersion must be zero", 3);
    if (g_caps.MaxSimultaneousTextures > 2)
        mismatch("MaxSimultaneousTextures exceeds tested value 2", 3);
    if (g_caps.MaxTextureBlendStages > 2)
        mismatch("MaxTextureBlendStages exceeds tested value 2", 3);
}

static void audit_formats(void)
{
    UINT i;
    trace_text("---- CheckDeviceFormat matrix ----");
    for (i = 0; i < sizeof(g_probes) / sizeof(g_probes[0]); ++i)
    {
        const FormatProbe *probe = &g_probes[i];
        HRESULT hr = IDirect3D8_CheckDeviceFormat(g_d3d, 0,
                D3DDEVTYPE_HAL, g_adapter_format, probe->usage,
                probe->type, probe->format);
        BOOL supported = SUCCEEDED(hr);
        char line[250];
        wsprintfA(line, "%s: hr=0x%08lX actual=%s expected=%s",
                probe->name, (unsigned long)hr,
                supported ? "SUPPORTED" : "HIDDEN",
                probe->expected_supported ? "SUPPORTED" : "HIDDEN");
        trace_text(line);
        if (supported != probe->expected_supported)
            mismatch(probe->name, probe->category);
    }
}

static HRESULT create_dashboard_device(void)
{
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = TEST_WIDTH;
    pp.BackBufferHeight = TEST_HEIGHT;
    pp.BackBufferFormat = g_adapter_format;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = g_window;
    pp.Windowed = TRUE;
    begin_stage("CreateDevice for audit dashboard");
    return IDirect3D8_CreateDevice(g_d3d, 0, D3DDEVTYPE_HAL, g_window,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &g_device);
}

static HRESULT render_dashboard(void)
{
    D3DRECT panels[4] =
    {
        {35, 65, 155, 315}, {195, 65, 315, 315},
        {355, 65, 475, 315}, {515, 65, 635, 315}
    };
    DWORD colors[4];
    UINT i;
    HRESULT hr;
    char title[250];
    hr = IDirect3DDevice8_Clear(g_device, 0, NULL, D3DCLEAR_TARGET,
            D3DCOLOR_XRGB(3, 6, 18), 1.0f, 0);
    if (FAILED(hr)) return fail_stage("dashboard Clear background", hr);
    for (i = 0; i < 4; ++i)
    {
        colors[i] = g_category_mismatches[i]
                ? D3DCOLOR_XRGB(230, 35, 35) : D3DCOLOR_XRGB(35, 220, 90);
        hr = IDirect3DDevice8_Clear(g_device, 1, &panels[i],
                D3DCLEAR_TARGET, colors[i], 1.0f, 0);
        if (FAILED(hr)) return fail_stage("dashboard panel Clear", hr);
    }
    begin_stage("Present audit dashboard");
    hr = IDirect3DDevice8_Present(g_device, NULL, NULL, NULL, NULL);
    if (FAILED(hr)) return fail_stage("Present", hr);
    if (!g_mismatch_count)
        SetWindowTextA(g_window,
                "D3D8 caps audit: PASS - formats | hidden formats | fixed caps | forbidden caps");
    else
    {
        wsprintfA(title, "D3D8 caps audit: %u MISMATCHES - first: %s",
                g_mismatch_count, g_first_mismatch);
        SetWindowTextA(g_window, title);
    }
    return D3D_OK;
}

static void release_all(void)
{
    if (g_device) IDirect3DDevice8_Release(g_device);
    if (g_d3d) IDirect3D8_Release(g_d3d);
    g_device = NULL;
    g_d3d = NULL;
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message,
        WPARAM wparam, LPARAM lparam)
{
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous,
        LPSTR command_line, int show_command)
{
    WNDCLASSA wc;
    D3DDISPLAYMODE mode;
    MSG message;
    HRESULT hr = D3D_OK;
    RECT rect = {0, 0, TEST_WIDTH, TEST_HEIGHT};
    (void)previous; (void)command_line; (void)show_command;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = g_class_name;
    if (!RegisterClassA(&wc)) return 1;
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    g_window = CreateWindowA(g_class_name, "D3D8 capability audit: starting",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left, rect.bottom - rect.top,
            NULL, NULL, instance, NULL);
    if (!g_window) return 1;

    begin_stage("Direct3DCreate8");
    g_d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!g_d3d) hr = fail_stage("Direct3DCreate8 returned NULL", E_FAIL);
    if (SUCCEEDED(hr))
    {
        hr = IDirect3D8_GetAdapterDisplayMode(g_d3d, 0, &mode);
        if (FAILED(hr)) fail_stage("GetAdapterDisplayMode", hr);
        else g_adapter_format = mode.Format;
    }
    if (SUCCEEDED(hr))
    {
        ZeroMemory(&g_caps, sizeof(g_caps));
        begin_stage("GetDeviceCaps");
        hr = IDirect3D8_GetDeviceCaps(g_d3d, 0, D3DDEVTYPE_HAL, &g_caps);
        if (FAILED(hr)) fail_stage("GetDeviceCaps", hr);
    }
    if (SUCCEEDED(hr))
    {
        dump_caps();
        audit_caps();
        audit_formats();
        hr = create_dashboard_device();
        if (FAILED(hr)) fail_stage("CreateDevice dashboard", hr);
    }
    if (SUCCEEDED(hr)) hr = render_dashboard();
    if (SUCCEEDED(hr) && g_mismatch_count)
    {
        char message_text[300];
        wsprintfA(message_text,
                "Capability audit found %u mismatches. The first is:\r\n%s\r\n\r\nSee guest debug output for the complete caps/format matrix.",
                g_mismatch_count, g_first_mismatch);
        MessageBoxA(g_window, message_text, "D3D8 capability audit",
                MB_OK | MB_ICONWARNING);
        hr = E_FAIL;
    }
    else if (FAILED(hr))
        MessageBoxA(g_window,
                "The D3D8 capability audit could not complete. Check the window title and guest debug output.",
                "D3D8 capability audit", MB_OK | MB_ICONERROR);

    while (GetMessageA(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    release_all();
    return FAILED(hr) ? 1 : 0;
}

void WINAPI WinMainCRTStartup(void)
{
    ExitProcess((UINT)WinMain(GetModuleHandleA(NULL), NULL,
            GetCommandLineA(), SW_SHOWDEFAULT));
}
