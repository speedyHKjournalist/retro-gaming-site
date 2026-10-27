// Direct3D 9 render-target/depth-surface binding lifetime regression test.
//
// Besides the ordinary same-object rebind with an application reference, this
// deliberately covers a compatibility edge seen in old games: the application
// keeps the pointer value after Release(), while the device binding is the only
// reference keeping the surface alive, then passes that same pointer back to
// SetRenderTarget/SetDepthStencilSurface.  The proxy must retain the incoming
// alias before releasing its old binding; doing those operations in the other
// order destroys the object and immediately calls AddRef through freed memory.
//
// Build for Windows XP with ../d3d9proxy/build.sh --tests.  Like the other
// smoke tests, this avoids the MinGW C runtime.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
/* Instantiates the d3d9 IIDs in this object file, as d3d9_proxy.c does, so
 * GetContainer can be checked without linking libdxguid (these tests build
 * -nostdlib). */
#include <initguid.h>
#include <d3d9.h>

#define TEST_WIDTH  320
#define TEST_HEIGHT 240

static const char g_window_class[] = "V86GLD3D9SurfaceLifetimeTest";
static IDirect3D9 *g_d3d;
static IDirect3DDevice9 *g_device;
static HWND g_window;
static const char *g_stage = "startup";

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d9-surface-lifetime] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void trace_result(const char *stage, HRESULT hr)
{
    char line[192];

    wsprintfA(line, "[d3d9-surface-lifetime] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
}

static HRESULT fail(const char *stage, HRESULT hr)
{
    g_stage = stage;
    trace_result(stage, hr);
    return FAILED(hr) ? hr : E_FAIL;
}

static LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS *exception)
{
    DWORD code = exception && exception->ExceptionRecord
            ? exception->ExceptionRecord->ExceptionCode : 0xE0000001u;
    char line[192];

    wsprintfA(line, "UNHANDLED 0x%08lX during %s",
            (unsigned long)code, g_stage);
    trace_text(line);
    if (g_window)
        SetWindowTextA(g_window, line);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void release_d3d9(void)
{
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

static HRESULT create_device(HWND hwnd)
{
    D3DDISPLAYMODE mode;
    D3DPRESENT_PARAMETERS present;
    HRESULT hr;

    g_stage = "Direct3DCreate9";
    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d)
        return fail(g_stage, E_FAIL);

    ZeroMemory(&mode, sizeof(mode));
    g_stage = "GetAdapterDisplayMode";
    hr = IDirect3D9_GetAdapterDisplayMode(g_d3d, D3DADAPTER_DEFAULT, &mode);
    if (FAILED(hr))
        return fail(g_stage, hr);

    ZeroMemory(&present, sizeof(present));
    present.BackBufferWidth = TEST_WIDTH;
    present.BackBufferHeight = TEST_HEIGHT;
    present.BackBufferFormat = mode.Format;
    present.BackBufferCount = 1;
    present.MultiSampleType = D3DMULTISAMPLE_NONE;
    present.SwapEffect = D3DSWAPEFFECT_DISCARD;
    present.hDeviceWindow = hwnd;
    present.Windowed = TRUE;
    /* Enabled so the device actually has an auto depth-stencil for
     * verify_implicit_surfaces() to ask for. */
    present.EnableAutoDepthStencil = TRUE;
    present.AutoDepthStencilFormat = D3DFMT_D24S8;
    present.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

    g_stage = "CreateDevice";
    hr = IDirect3D9_CreateDevice(g_d3d, D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &present, &g_device);
    if (FAILED(hr) || !g_device)
        return fail(g_stage, FAILED(hr) ? hr : E_FAIL);
    return D3D_OK;
}

/*
 * The implicit back buffer and the auto depth-stencil are sub-objects of the
 * device in the same way a texture level surface is a sub-object of its
 * texture: every GetBackBuffer / GetDepthStencilSurface call hands back the
 * same object, and dropping the last public reference does not destroy it.
 *
 * GTA San Andreas depends on both. RenderWare takes each surface once during
 * device setup, releases it immediately, and binds the saved pointers for the
 * rest of the run. Against a proxy that allocates a fresh self-owning surface
 * per call, that release frees the object -- and the heap then hands the same
 * block out as the next resource the game creates, which is a vertex buffer.
 */
static HRESULT verify_implicit_surfaces(void)
{
    IDirect3DSurface9 *back = NULL;
    IDirect3DSurface9 *back_again = NULL;
    IDirect3DSurface9 *depth = NULL;
    IDirect3DSurface9 *depth_again = NULL;
    IDirect3DVertexBuffer9 *churn = NULL;
    IDirect3DSurface9 *borrowed_back;
    IDirect3DSurface9 *borrowed_depth;
    D3DSURFACE_DESC back_desc;
    D3DSURFACE_DESC depth_desc;
    D3DSURFACE_DESC desc;
    HRESULT hr;

    g_stage = "GetBackBuffer(0)";
    hr = IDirect3DDevice9_GetBackBuffer(g_device, 0, 0,
            D3DBACKBUFFER_TYPE_MONO, &back);
    if (FAILED(hr) || !back)
        return fail(g_stage, FAILED(hr) ? hr : E_FAIL);

    g_stage = "GetBackBuffer(0, second call returns same object)";
    hr = IDirect3DDevice9_GetBackBuffer(g_device, 0, 0,
            D3DBACKBUFFER_TYPE_MONO, &back_again);
    if (FAILED(hr) || back_again != back)
    {
        hr = FAILED(hr) ? hr : E_FAIL;
        goto done;
    }

    g_stage = "GetDepthStencilSurface";
    hr = IDirect3DDevice9_GetDepthStencilSurface(g_device, &depth);
    if (FAILED(hr) || !depth)
    {
        hr = FAILED(hr) ? hr : E_FAIL;
        goto done;
    }

    g_stage = "GetDepthStencilSurface(second call returns same object)";
    hr = IDirect3DDevice9_GetDepthStencilSurface(g_device, &depth_again);
    if (FAILED(hr) || depth_again != depth)
    {
        hr = FAILED(hr) ? hr : E_FAIL;
        goto done;
    }

    /* Recorded while the references are still held, so the checks after the
     * release compare against what the surfaces themselves reported. */
    ZeroMemory(&back_desc, sizeof(back_desc));
    ZeroMemory(&depth_desc, sizeof(depth_desc));
    g_stage = "GetDesc(while referenced)";
    hr = IDirect3DSurface9_GetDesc(back, &back_desc);
    if (SUCCEEDED(hr))
        hr = IDirect3DSurface9_GetDesc(depth, &depth_desc);
    if (FAILED(hr))
        goto done;

    /* Drop every public reference, exactly as RenderWare does during setup. */
    borrowed_back = back;
    borrowed_depth = depth;
    IDirect3DSurface9_Release(back_again);
    IDirect3DSurface9_Release(back);
    IDirect3DSurface9_Release(depth_again);
    IDirect3DSurface9_Release(depth);
    back = back_again = depth = depth_again = NULL;

    /* Allocated after that release on purpose: if either surface was freed,
     * this is what the heap reuses the block for. The pointer-identity checks
     * below are the deterministic part -- this only decides whether a surviving
     * bug shows up as wrong data or as a crash. */
    g_stage = "CreateVertexBuffer(churns the heap)";
    hr = IDirect3DDevice9_CreateVertexBuffer(g_device, 1024, 0, D3DFVF_XYZ,
            D3DPOOL_MANAGED, &churn, NULL);
    if (FAILED(hr) || !churn)
    {
        hr = FAILED(hr) ? hr : E_FAIL;
        goto done;
    }

    ZeroMemory(&desc, sizeof(desc));
    g_stage = "BackBuffer::GetDesc(through borrowed pointer)";
    hr = IDirect3DSurface9_GetDesc(borrowed_back, &desc);
    if (FAILED(hr) || desc.Width != back_desc.Width
            || desc.Height != back_desc.Height
            || desc.Format != back_desc.Format)
    {
        hr = FAILED(hr) ? hr : E_FAIL;
        goto done;
    }

    ZeroMemory(&desc, sizeof(desc));
    g_stage = "DepthSurface::GetDesc(through borrowed pointer)";
    hr = IDirect3DSurface9_GetDesc(borrowed_depth, &desc);
    if (FAILED(hr) || desc.Width != depth_desc.Width
            || desc.Height != depth_desc.Height
            || desc.Format != depth_desc.Format)
    {
        hr = FAILED(hr) ? hr : E_FAIL;
        goto done;
    }

    /* The saved pointer still binds, which is all the game ever does with it. */
    g_stage = "SetDepthStencilSurface(through borrowed pointer)";
    hr = IDirect3DDevice9_SetDepthStencilSurface(g_device, borrowed_depth);
    if (FAILED(hr))
        goto done;

    g_stage = "GetBackBuffer(after every reference was dropped)";
    hr = IDirect3DDevice9_GetBackBuffer(g_device, 0, 0,
            D3DBACKBUFFER_TYPE_MONO, &back);
    if (FAILED(hr) || back != borrowed_back)
        hr = FAILED(hr) ? hr : E_FAIL;

done:
    if (churn)
        IDirect3DVertexBuffer9_Release(churn);
    if (back_again)
        IDirect3DSurface9_Release(back_again);
    if (back)
        IDirect3DSurface9_Release(back);
    if (depth_again)
        IDirect3DSurface9_Release(depth_again);
    if (depth)
        IDirect3DSurface9_Release(depth);
    if (FAILED(hr))
        return fail(g_stage, hr);
    trace_text("PASS implicit back-buffer and auto depth-stencil lifetime");
    return D3D_OK;
}

static HRESULT verify_render_target_alias(void)
{
    IDirect3DSurface9 *original = NULL;
    IDirect3DSurface9 *target = NULL;
    IDirect3DSurface9 *current = NULL;
    IDirect3DSurface9 *alias;
    D3DSURFACE_DESC desc;
    HRESULT hr;

    g_stage = "GetRenderTarget(default)";
    hr = IDirect3DDevice9_GetRenderTarget(g_device, 0, &original);
    if (FAILED(hr) || !original)
        return fail(g_stage, FAILED(hr) ? hr : E_FAIL);

    g_stage = "CreateRenderTarget";
    hr = IDirect3DDevice9_CreateRenderTarget(g_device, 64, 64,
            D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, TRUE, &target, NULL);
    if (FAILED(hr) || !target)
    {
        IDirect3DSurface9_Release(original);
        return fail(g_stage, FAILED(hr) ? hr : E_FAIL);
    }

    g_stage = "SetRenderTarget(initial)";
    hr = IDirect3DDevice9_SetRenderTarget(g_device, 0, target);
    if (FAILED(hr))
        goto done;

    /* Ordinary COM-correct same-pointer binding while target is still held. */
    g_stage = "SetRenderTarget(same, externally retained)";
    hr = IDirect3DDevice9_SetRenderTarget(g_device, 0, target);
    if (FAILED(hr))
        goto done;

    /* Keep only the binding reference.  alias is intentionally non-owning. */
    alias = target;
    IDirect3DSurface9_Release(target);
    target = NULL;

    g_stage = "SetRenderTarget(same, binding-only alias)";
    hr = IDirect3DDevice9_SetRenderTarget(g_device, 0, alias);
    if (FAILED(hr))
        goto done;

    g_stage = "GetRenderTarget(after same-pointer rebind)";
    hr = IDirect3DDevice9_GetRenderTarget(g_device, 0, &current);
    if (FAILED(hr) || !current || current != alias)
    {
        hr = FAILED(hr) ? hr : E_FAIL;
        goto done;
    }

    ZeroMemory(&desc, sizeof(desc));
    g_stage = "RenderTarget::GetDesc(after same-pointer rebind)";
    hr = IDirect3DSurface9_GetDesc(current, &desc);
    if (FAILED(hr) || desc.Width != 64 || desc.Height != 64
            || desc.Format != D3DFMT_A8R8G8B8)
        hr = FAILED(hr) ? hr : E_FAIL;

done:
    if (current)
        IDirect3DSurface9_Release(current);
    /* Restoring slot 0 releases the device's target binding. */
    if (original)
    {
        HRESULT restore_hr;
        g_stage = "SetRenderTarget(restore default)";
        restore_hr = IDirect3DDevice9_SetRenderTarget(g_device, 0, original);
        IDirect3DSurface9_Release(original);
        if (SUCCEEDED(hr) && FAILED(restore_hr))
            hr = restore_hr;
    }
    if (target)
        IDirect3DSurface9_Release(target);
    if (FAILED(hr))
        return fail(g_stage, hr);
    trace_text("PASS render-target same-pointer lifetime");
    return D3D_OK;
}

static HRESULT verify_depth_surface_alias(void)
{
    IDirect3DSurface9 *depth = NULL;
    IDirect3DSurface9 *current = NULL;
    IDirect3DSurface9 *alias;
    D3DSURFACE_DESC desc;
    HRESULT hr;

    g_stage = "CreateDepthStencilSurface";
    hr = IDirect3DDevice9_CreateDepthStencilSurface(g_device, 64, 64,
            D3DFMT_D16, D3DMULTISAMPLE_NONE, 0, TRUE, &depth, NULL);
    if (FAILED(hr) || !depth)
        return fail(g_stage, FAILED(hr) ? hr : E_FAIL);

    g_stage = "SetDepthStencilSurface(initial)";
    hr = IDirect3DDevice9_SetDepthStencilSurface(g_device, depth);
    if (FAILED(hr))
        goto done;

    g_stage = "SetDepthStencilSurface(same, externally retained)";
    hr = IDirect3DDevice9_SetDepthStencilSurface(g_device, depth);
    if (FAILED(hr))
        goto done;

    alias = depth;
    IDirect3DSurface9_Release(depth);
    depth = NULL;

    g_stage = "SetDepthStencilSurface(same, binding-only alias)";
    hr = IDirect3DDevice9_SetDepthStencilSurface(g_device, alias);
    if (FAILED(hr))
        goto done;

    g_stage = "GetDepthStencilSurface(after same-pointer rebind)";
    hr = IDirect3DDevice9_GetDepthStencilSurface(g_device, &current);
    if (FAILED(hr) || !current || current != alias)
    {
        hr = FAILED(hr) ? hr : E_FAIL;
        goto done;
    }

    ZeroMemory(&desc, sizeof(desc));
    g_stage = "DepthSurface::GetDesc(after same-pointer rebind)";
    hr = IDirect3DSurface9_GetDesc(current, &desc);
    if (FAILED(hr) || desc.Width != 64 || desc.Height != 64
            || desc.Format != D3DFMT_D16)
        hr = FAILED(hr) ? hr : E_FAIL;

done:
    if (current)
        IDirect3DSurface9_Release(current);
    {
        HRESULT unbind_hr;
        g_stage = "SetDepthStencilSurface(NULL)";
        unbind_hr = IDirect3DDevice9_SetDepthStencilSurface(g_device, NULL);
        if (SUCCEEDED(hr) && FAILED(unbind_hr))
            hr = unbind_hr;
    }
    if (depth)
        IDirect3DSurface9_Release(depth);
    if (FAILED(hr))
        return fail(g_stage, hr);
    trace_text("PASS depth-surface same-pointer lifetime");
    return D3D_OK;
}

/*
 * A texture level surface is a sub-object of its texture, not a free-standing
 * COM object: GetSurfaceLevel returns the same pointer every call, and the
 * reference it hands out is a reference on the texture.  So an application may
 * release its surface reference while still holding the texture and keep using
 * the surface pointer.  Kart Rider does precisely that around an upload --
 * GetSurfaceLevel, LockRect, Release, then UnlockRect through the saved
 * pointer -- and a proxy that hands out per-call self-owning surfaces frees the
 * object at that Release and crashes on the UnlockRect that follows.
 */
static HRESULT verify_texture_level_surface(void)
{
    IDirect3DTexture9 *texture = NULL;
    IDirect3DSurface9 *surface = NULL;
    IDirect3DSurface9 *again = NULL;
    IDirect3DSurface9 *borrowed;
    IDirect3DBaseTexture9 *container = NULL;
    D3DLOCKED_RECT locked;
    D3DSURFACE_DESC desc;
    HRESULT hr;

    g_stage = "CreateTexture";
    hr = IDirect3DDevice9_CreateTexture(g_device, 64, 64, 1, 0,
            D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, NULL);
    if (FAILED(hr) || !texture)
        return fail(g_stage, FAILED(hr) ? hr : E_FAIL);

    g_stage = "GetSurfaceLevel(0)";
    hr = IDirect3DTexture9_GetSurfaceLevel(texture, 0, &surface);
    if (FAILED(hr) || !surface)
    {
        hr = FAILED(hr) ? hr : E_FAIL;
        goto done;
    }

    /* Every call for the same level must yield the same object; apps compare
     * these pointers to decide whether a surface is already bound. */
    g_stage = "GetSurfaceLevel(0, second call returns same object)";
    hr = IDirect3DTexture9_GetSurfaceLevel(texture, 0, &again);
    if (FAILED(hr) || again != surface)
    {
        hr = FAILED(hr) ? hr : E_FAIL;
        goto done;
    }
    IDirect3DSurface9_Release(again);
    again = NULL;

    /* The Kart Rider sequence: lock, drop the surface reference, then unlock
     * through the pointer the application kept. */
    g_stage = "Surface::LockRect(before releasing surface reference)";
    ZeroMemory(&locked, sizeof(locked));
    hr = IDirect3DSurface9_LockRect(surface, &locked, NULL, 0);
    if (FAILED(hr) || !locked.pBits)
    {
        hr = FAILED(hr) ? hr : E_FAIL;
        goto done;
    }
    FillMemory(locked.pBits, 64u * 4u, 0x7Fu);

    borrowed = surface;
    g_stage = "Surface::Release(while locked, texture still held)";
    IDirect3DSurface9_Release(surface);
    surface = NULL;

    g_stage = "Surface::UnlockRect(through borrowed pointer)";
    hr = IDirect3DSurface9_UnlockRect(borrowed);
    if (FAILED(hr))
        goto done;

    /* The object must still be fully usable, not merely not-yet-overwritten. */
    ZeroMemory(&desc, sizeof(desc));
    g_stage = "Surface::GetDesc(through borrowed pointer)";
    hr = IDirect3DSurface9_GetDesc(borrowed, &desc);
    if (FAILED(hr) || desc.Width != 64 || desc.Height != 64
            || desc.Format != D3DFMT_A8R8G8B8)
    {
        hr = FAILED(hr) ? hr : E_FAIL;
        goto done;
    }

    g_stage = "Surface::GetContainer(through borrowed pointer)";
    hr = IDirect3DSurface9_GetContainer(borrowed, &IID_IDirect3DBaseTexture9,
            (void **)&container);
    if (FAILED(hr) || (IDirect3DTexture9 *)container != texture)
        hr = FAILED(hr) ? hr : E_FAIL;

done:
    if (container)
        IDirect3DBaseTexture9_Release(container);
    if (again)
        IDirect3DSurface9_Release(again);
    if (surface)
        IDirect3DSurface9_Release(surface);
    if (texture)
        IDirect3DTexture9_Release(texture);
    if (FAILED(hr))
        return fail(g_stage, hr);
    trace_text("PASS texture-level surface sub-object lifetime");
    return D3D_OK;
}

static HRESULT run_surface_tests(HWND hwnd)
{
    HRESULT hr;

    hr = create_device(hwnd);
    if (FAILED(hr))
        return hr;
    /* Ahead of verify_depth_surface_alias(), which finishes by unbinding depth
     * altogether and so leaves the device with no auto depth-stencil to get. */
    hr = verify_implicit_surfaces();
    if (FAILED(hr))
        return hr;
    hr = verify_render_target_alias();
    if (FAILED(hr))
        return hr;
    hr = verify_depth_surface_alias();
    if (FAILED(hr))
        return hr;
    return verify_texture_level_surface();
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam,
        LPARAM lparam)
{
    (void)wparam;
    (void)lparam;
    switch (message)
    {
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            g_window = NULL;
            release_d3d9();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

static int run_test(HINSTANCE instance)
{
    WNDCLASSA window_class;
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

    hwnd = CreateWindowA(g_window_class, "D3D9 surface lifetime: running",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 520, 180,
            NULL, NULL, instance, NULL);
    if (!hwnd)
        return 2;
    g_window = hwnd;
    SetUnhandledExceptionFilter(unhandled_exception_filter);
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    hr = run_surface_tests(hwnd);
    if (FAILED(hr))
    {
        char title[192];
        wsprintfA(title, "D3D9 surface lifetime: FAIL %s (0x%08lX)",
                g_stage, (unsigned long)hr);
        SetWindowTextA(hwnd, title);
        MessageBoxA(hwnd, title, "D3D9 surface lifetime", MB_OK | MB_ICONERROR);
    }
    else
    {
        SetWindowTextA(hwnd, "D3D9 surface lifetime: PASS");
        trace_text("PASS all surface lifetime checks");
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
    int result = run_test(GetModuleHandleA(NULL));

    ExitProcess((UINT)result);
}
