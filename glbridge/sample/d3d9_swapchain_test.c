// Direct3D 9 primary swap-chain contract smoke test for the v86 WebGPU bridge.
//
// The test intentionally stays within the implicit swap chain created by
// CreateDevice().  It verifies the object/lifetime-facing methods used by old
// games before they render their first frame, including rejection of an
// out-of-range swap-chain index.
//
// Build for Windows XP with ../d3d9proxy/build.sh --tests.  The build uses
// a 32-bit MinGW compiler and deliberately avoids the MinGW C runtime.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d9.h>

#define TEST_CLIENT_WIDTH  640
#define TEST_CLIENT_HEIGHT 480

static const char g_window_class[] = "V86GLD3D9SwapChainTest";
static IDirect3D9 *g_d3d;
static IDirect3DDevice9 *g_device;
static IDirect3DSwapChain9 *g_swap_chain;
static IDirect3DSurface9 *g_back_buffer;
static const char *g_failed_stage = "unknown stage";

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d9-swapchain] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void trace_hresult(const char *stage, HRESULT hr)
{
    char line[192];

    wsprintfA(line, "[d3d9-swapchain] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
}

static HRESULT fail_stage(const char *stage, HRESULT hr)
{
    g_failed_stage = stage;
    trace_hresult(stage, hr);
    return FAILED(hr) ? hr : E_FAIL;
}

static void release_d3d9(void)
{
    if (g_back_buffer)
    {
        IDirect3DSurface9_Release(g_back_buffer);
        g_back_buffer = NULL;
    }
    if (g_swap_chain)
    {
        IDirect3DSwapChain9_Release(g_swap_chain);
        g_swap_chain = NULL;
    }
    if (g_device)
    {
        IDirect3DDevice9_Release(g_device);
        g_device = NULL;
    }
    if (g_d3d)
    {
        IDirect3D9_Release(g_d3d);
        g_d3d = NULL;
    }
}

static HRESULT create_device(HWND hwnd, D3DPRESENT_PARAMETERS *created)
{
    D3DDISPLAYMODE mode;
    HRESULT hr;

    trace_text("Direct3DCreate9");
    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d)
        return fail_stage("Direct3DCreate9 returned NULL", E_FAIL);

    ZeroMemory(&mode, sizeof(mode));
    hr = IDirect3D9_GetAdapterDisplayMode(g_d3d, D3DADAPTER_DEFAULT, &mode);
    if (FAILED(hr))
        return fail_stage("IDirect3D9::GetAdapterDisplayMode", hr);

    ZeroMemory(created, sizeof(*created));
    created->BackBufferWidth = TEST_CLIENT_WIDTH;
    created->BackBufferHeight = TEST_CLIENT_HEIGHT;
    created->BackBufferFormat = mode.Format;
    created->BackBufferCount = 1;
    created->MultiSampleType = D3DMULTISAMPLE_NONE;
    created->SwapEffect = D3DSWAPEFFECT_DISCARD;
    created->hDeviceWindow = hwnd;
    created->Windowed = TRUE;
    created->EnableAutoDepthStencil = FALSE;
    created->PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

    hr = IDirect3D9_CreateDevice(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            created, &g_device);
    trace_hresult("IDirect3D9::CreateDevice", hr);
    if (FAILED(hr) || !g_device)
        return fail_stage("IDirect3D9::CreateDevice",
                FAILED(hr) ? hr : E_FAIL);
    return D3D_OK;
}

static HRESULT verify_invalid_index(void)
{
    IDirect3DSwapChain9 *invalid_chain =
            (IDirect3DSwapChain9 *)(ULONG_PTR)1;
    HRESULT hr;

    hr = IDirect3DDevice9_GetSwapChain(g_device, 1, &invalid_chain);
    trace_hresult("IDirect3DDevice9::GetSwapChain(1)", hr);
    if (SUCCEEDED(hr))
    {
        if (invalid_chain
                && invalid_chain != (IDirect3DSwapChain9 *)(ULONG_PTR)1)
            IDirect3DSwapChain9_Release(invalid_chain);
        return fail_stage("GetSwapChain(1) unexpectedly succeeded", E_FAIL);
    }
    if (invalid_chain != NULL)
        return fail_stage("GetSwapChain(1) did not clear output", E_FAIL);
    return D3D_OK;
}

static HRESULT verify_repeated_swap_chain_identity(void)
{
    IDirect3DSwapChain9 *repeated_chain = NULL;
    HRESULT hr;

    hr = IDirect3DDevice9_GetSwapChain(g_device, 0, &repeated_chain);
    trace_hresult("IDirect3DDevice9::GetSwapChain(0) repeated", hr);
    if (FAILED(hr) || !repeated_chain)
        return fail_stage("repeated GetSwapChain(0)",
                FAILED(hr) ? hr : E_FAIL);

    if (repeated_chain != g_swap_chain)
    {
        IDirect3DSwapChain9_Release(repeated_chain);
        return fail_stage("repeated GetSwapChain(0) COM identity", E_FAIL);
    }

    /* Balance the reference returned by the repeated GetSwapChain call. */
    IDirect3DSwapChain9_Release(repeated_chain);
    trace_text("repeated GetSwapChain(0) returned the same COM object");
    return D3D_OK;
}

static HRESULT verify_swap_chain_device_lifetime(void)
{
    IDirect3DDevice9 *original_device = g_device;
    IDirect3DDevice9 *recovered_device = NULL;
    HRESULT hr;

    /*
     * Drop exactly the reference returned by CreateDevice.  The swap-chain
     * reference must keep the parent alive until GetDevice returns its own
     * reference.  Store that returned reference back in g_device so the
     * normal global cleanup order remains valid on both PASS and later FAIL.
     */
    trace_text("releasing the original CreateDevice reference");
    IDirect3DDevice9_Release(g_device);
    g_device = NULL;

    hr = IDirect3DSwapChain9_GetDevice(g_swap_chain, &recovered_device);
    trace_hresult("IDirect3DSwapChain9::GetDevice after device Release", hr);
    if (recovered_device)
        g_device = recovered_device;
    if (FAILED(hr) || !recovered_device)
        return fail_stage("swap chain did not retain its device",
                FAILED(hr) ? hr : E_FAIL);
    if (recovered_device != original_device)
        return fail_stage("GetDevice changed COM identity after Release",
                E_FAIL);
    if (IDirect3DDevice9_GetNumberOfSwapChains(recovered_device) != 1)
        return fail_stage("recovered device is not usable", E_FAIL);

    trace_text("swap chain retained the device after original Release");
    return D3D_OK;
}

static HRESULT verify_back_buffer_container(void)
{
    IDirect3DSwapChain9 *container_chain = NULL;
    HRESULT hr;

    hr = IDirect3DSurface9_GetContainer(g_back_buffer,
            &IID_IDirect3DSwapChain9, (void **)&container_chain);
    trace_hresult("IDirect3DSurface9::GetContainer(swap chain)", hr);
    if (FAILED(hr) || !container_chain)
        return fail_stage("back buffer GetContainer(swap chain)",
                FAILED(hr) ? hr : E_FAIL);

    if (container_chain != g_swap_chain)
    {
        IDirect3DSwapChain9_Release(container_chain);
        return fail_stage("back buffer swap-chain COM identity", E_FAIL);
    }

    /* Balance the reference returned through GetContainer. */
    IDirect3DSwapChain9_Release(container_chain);
    trace_text("back buffer container matched the implicit swap chain");
    return D3D_OK;
}

/*
 * GetFrontBufferData hands back the image on screen, and D3D9 documents its
 * destination as D3DFMT_A8R8G8B8 whatever the swap chain's own format is. This
 * device's back buffer is the adapter's display format -- D3DFMT_X8R8G8B8 on
 * any ordinary desktop -- so the two formats differ by design, and treating
 * that as a mismatch is exactly the bug 3DMark06's Image Quality test hit
 * ("GetFrontBufferData failed: Invalid call").
 *
 * GetRenderTargetData is checked alongside it precisely because it must NOT
 * accept the same pair: it is a copy, not a conversion.
 */
static HRESULT verify_front_buffer_data_into(D3DPOOL pool, const char *pool_name)
{
    IDirect3DSurface9 *destination = NULL;
    D3DLOCKED_RECT locked;
    HRESULT hr;
    UINT opaque_checked = 0;
    UINT y;

    hr = IDirect3DDevice9_CreateOffscreenPlainSurface(g_device,
            TEST_CLIENT_WIDTH, TEST_CLIENT_HEIGHT, D3DFMT_A8R8G8B8,
            pool, &destination, NULL);
    trace_hresult(pool_name, hr);
    if (FAILED(hr) || !destination)
        return fail_stage("CreateOffscreenPlainSurface for the frame dump",
                FAILED(hr) ? hr : E_FAIL);

    hr = IDirect3DDevice9_GetFrontBufferData(g_device, 0, destination);
    trace_hresult("IDirect3DDevice9::GetFrontBufferData", hr);
    if (FAILED(hr))
    {
        IDirect3DSurface9_Release(destination);
        return fail_stage("IDirect3DDevice9::GetFrontBufferData", hr);
    }

    /* What is on screen is opaque. A dump saved with alpha 0 is a fully
     * transparent image, which looks like a rendering failure rather than the
     * readback bug it actually is. */
    hr = IDirect3DSurface9_LockRect(destination, &locked, NULL, D3DLOCK_READONLY);
    if (FAILED(hr))
    {
        IDirect3DSurface9_Release(destination);
        return fail_stage("LockRect on the front-buffer copy", hr);
    }
    for (y = 0; y < TEST_CLIENT_HEIGHT; ++y)
    {
        const BYTE *row = (const BYTE *)locked.pBits + y * locked.Pitch;
        UINT x;
        for (x = 0; x < TEST_CLIENT_WIDTH; ++x)
        {
            if (row[x * 4 + 3] != 0xFF)
            {
                IDirect3DSurface9_UnlockRect(destination);
                IDirect3DSurface9_Release(destination);
                return fail_stage("front-buffer copy is not opaque", E_FAIL);
            }
            ++opaque_checked;
        }
    }
    IDirect3DSurface9_UnlockRect(destination);
    if (!opaque_checked)
    {
        IDirect3DSurface9_Release(destination);
        return fail_stage("front-buffer copy had no pixels", E_FAIL);
    }

    /* The strict sibling: same surfaces, and it has to refuse. */
    hr = IDirect3DDevice9_GetRenderTargetData(g_device, g_back_buffer,
            destination);
    trace_hresult("IDirect3DDevice9::GetRenderTargetData(mismatched)", hr);
    IDirect3DSurface9_Release(destination);
    if (SUCCEEDED(hr))
        return fail_stage("GetRenderTargetData must reject a format mismatch",
                E_FAIL);

    trace_text("GetFrontBufferData converted the back buffer to A8R8G8B8");
    return D3D_OK;
}

static HRESULT verify_front_buffer_data(void)
{
    HRESULT hr;

    /* Both CPU pools. SCRATCH is not an afterthought: it is what 3DMark06's
     * Image Quality dump asks for, because the surface exists only to be
     * locked and written to a file, and refusing it was a real defect. */
    hr = verify_front_buffer_data_into(D3DPOOL_SYSTEMMEM,
            "CreateOffscreenPlainSurface(A8R8G8B8, SYSTEMMEM)");
    if (FAILED(hr))
        return hr;
    return verify_front_buffer_data_into(D3DPOOL_SCRATCH,
            "CreateOffscreenPlainSurface(A8R8G8B8, SCRATCH)");
}

static HRESULT verify_primary_swap_chain(HWND hwnd,
        const D3DPRESENT_PARAMETERS *created)
{
    IDirect3DDevice9 *parent_device = NULL;
    D3DPRESENT_PARAMETERS actual;
    D3DDISPLAYMODE display_mode;
    UINT count;
    HRESULT hr;

    count = IDirect3DDevice9_GetNumberOfSwapChains(g_device);
    if (count != 1)
        return fail_stage("GetNumberOfSwapChains did not return 1", E_FAIL);
    trace_text("GetNumberOfSwapChains returned 1");

    hr = verify_invalid_index();
    if (FAILED(hr))
        return hr;

    hr = IDirect3DDevice9_GetSwapChain(g_device, 0, &g_swap_chain);
    trace_hresult("IDirect3DDevice9::GetSwapChain(0)", hr);
    if (FAILED(hr) || !g_swap_chain)
        return fail_stage("IDirect3DDevice9::GetSwapChain(0)",
                FAILED(hr) ? hr : E_FAIL);

    hr = verify_repeated_swap_chain_identity();
    if (FAILED(hr))
        return hr;

    hr = verify_swap_chain_device_lifetime();
    if (FAILED(hr))
        return hr;

    hr = IDirect3DSwapChain9_GetDevice(g_swap_chain, &parent_device);
    trace_hresult("IDirect3DSwapChain9::GetDevice", hr);
    if (FAILED(hr) || parent_device != g_device)
    {
        if (parent_device)
            IDirect3DDevice9_Release(parent_device);
        return fail_stage("IDirect3DSwapChain9::GetDevice identity",
                FAILED(hr) ? hr : E_FAIL);
    }
    IDirect3DDevice9_Release(parent_device);
    parent_device = NULL;

    ZeroMemory(&actual, sizeof(actual));
    hr = IDirect3DSwapChain9_GetPresentParameters(g_swap_chain, &actual);
    trace_hresult("IDirect3DSwapChain9::GetPresentParameters", hr);
    if (FAILED(hr))
        return fail_stage("IDirect3DSwapChain9::GetPresentParameters", hr);
    if (actual.BackBufferWidth != created->BackBufferWidth
            || actual.BackBufferHeight != created->BackBufferHeight
            || actual.BackBufferFormat != created->BackBufferFormat
            || actual.BackBufferCount != 1
            || actual.SwapEffect != created->SwapEffect
            || actual.hDeviceWindow != hwnd
            || !actual.Windowed)
        return fail_stage("GetPresentParameters returned unexpected values",
                E_FAIL);

    ZeroMemory(&display_mode, sizeof(display_mode));
    hr = IDirect3DSwapChain9_GetDisplayMode(g_swap_chain, &display_mode);
    trace_hresult("IDirect3DSwapChain9::GetDisplayMode", hr);
    if (FAILED(hr))
        return fail_stage("IDirect3DSwapChain9::GetDisplayMode", hr);
    if (!display_mode.Width || !display_mode.Height
            || display_mode.Format == D3DFMT_UNKNOWN)
        return fail_stage("GetDisplayMode returned invalid values", E_FAIL);

    hr = IDirect3DSwapChain9_GetBackBuffer(g_swap_chain, 0,
            D3DBACKBUFFER_TYPE_MONO, &g_back_buffer);
    trace_hresult("IDirect3DSwapChain9::GetBackBuffer(0)", hr);
    if (FAILED(hr) || !g_back_buffer)
        return fail_stage("IDirect3DSwapChain9::GetBackBuffer(0)",
                FAILED(hr) ? hr : E_FAIL);

    hr = verify_back_buffer_container();
    if (FAILED(hr))
        return hr;

    hr = IDirect3DSwapChain9_Present(g_swap_chain, NULL, NULL, NULL, NULL, 0);
    trace_hresult("IDirect3DSwapChain9::Present", hr);
    if (FAILED(hr))
        return fail_stage("IDirect3DSwapChain9::Present", hr);

    /* After a Present, so there is a presented image to ask for. */
    hr = verify_front_buffer_data();
    if (FAILED(hr))
        return hr;

    return D3D_OK;
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam,
        LPARAM lparam)
{
    switch (message)
    {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        {
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            EndPaint(hwnd, &paint);
            return 0;
        }

        case WM_DESTROY:
            release_d3d9();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

static int run_test(HINSTANCE instance, int show_command)
{
    WNDCLASSA window_class;
    D3DPRESENT_PARAMETERS created;
    RECT window_rect;
    HWND hwnd;
    MSG message;
    HRESULT hr;

    ZeroMemory(&window_class, sizeof(window_class));
    window_class.style = CS_OWNDC;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorA(NULL, IDC_ARROW);
    window_class.lpszClassName = g_window_class;
    if (!RegisterClassA(&window_class))
        return 1;

    SetRect(&window_rect, 0, 0, TEST_CLIENT_WIDTH, TEST_CLIENT_HEIGHT);
    AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd = CreateWindowA(g_window_class, "D3D9 swap chain: starting",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            window_rect.right - window_rect.left,
            window_rect.bottom - window_rect.top,
            NULL, NULL, instance, NULL);
    if (!hwnd)
        return 2;

    ShowWindow(hwnd, show_command);
    UpdateWindow(hwnd);

    hr = create_device(hwnd, &created);
    if (SUCCEEDED(hr))
        hr = verify_primary_swap_chain(hwnd, &created);

    if (SUCCEEDED(hr))
    {
        SetWindowTextA(hwnd, "D3D9 swap chain: PASS");
        trace_text("PASS");
    }
    else
    {
        char title[192];
        wsprintfA(title, "D3D9 swap chain: %s (0x%08lX)",
                g_failed_stage, (unsigned long)hr);
        SetWindowTextA(hwnd, title);
        MessageBoxA(hwnd,
                "The D3D9 swap-chain smoke test failed. Check the window "
                "title and guest debug output for the last stage.",
                "D3D9 swap-chain test", MB_OK | MB_ICONERROR);
    }

    while (GetMessageA(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return FAILED(hr) ? 3 : 0;
}

void WINAPI WinMainCRTStartup(void)
{
    int result = run_test(GetModuleHandleA(NULL), SW_SHOWDEFAULT);
    ExitProcess((UINT)result);
}
