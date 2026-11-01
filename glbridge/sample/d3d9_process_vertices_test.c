// Direct3D 9 ProcessVertices test for the v86 WebGPU bridge.
//
// ProcessVertices is the one call whose whole result lands in guest memory:
// it runs the fixed-function vertex pipeline over a source stream and writes
// pre-transformed vertices into a destination vertex buffer the app can Lock
// and read. Nothing about it is visible on screen unless the app then draws
// the result, which is why this test does not look at pixels at all -- it
// Locks the destination and *checks the numbers*, so a wrong multiply order,
// a missing perspective divide or a viewport mapping off by half a pixel
// fails here with a message rather than surviving as art that looks plausible.
//
// The transform is chosen so every stage of the pipeline changes the answer:
//   - WORLD scales and translates, so an identity-world implementation fails.
//   - VIEW translates along -Z, so a missing view multiply fails.
//   - PROJECTION is a real perspective matrix, so w != 1 and the divide shows.
//   - The viewport is *not* the full render target, so its X/Y offset shows.
//
// Build for Windows XP as documented in ../d3d9proxy/README.md.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>

#define TEST_CLIENT_WIDTH  640
#define TEST_CLIENT_HEIGHT 480
/* Deliberately offset and smaller than the back buffer. */
#define TEST_VIEWPORT_X      40
#define TEST_VIEWPORT_Y      20
#define TEST_VIEWPORT_WIDTH  320
#define TEST_VIEWPORT_HEIGHT 240

#define TEST_VERTEX_COUNT 3

typedef struct SourceVertex
{
    FLOAT x, y, z;
    FLOAT nx, ny, nz;
    DWORD color;
    FLOAT u, v;
} SourceVertex;

typedef struct ResultVertex
{
    FLOAT x, y, z, rhw;
    DWORD color;
    FLOAT u, v;
} ResultVertex;

static const char g_window_class[] = "V86GLD3D9ProcessVerticesTest";

static const SourceVertex g_vertices[TEST_VERTEX_COUNT] =
{
    { 0.0f,  0.0f, 0.0f,  0.0f, 0.0f, -1.0f, D3DCOLOR_XRGB(255, 0, 0), 0.25f, 0.75f },
    { 1.0f,  0.0f, 0.0f,  0.0f, 0.0f, -1.0f, D3DCOLOR_XRGB(0, 255, 0), 0.50f, 0.25f },
    { 0.0f,  1.0f, 0.0f,  0.0f, 0.0f, -1.0f, D3DCOLOR_XRGB(0, 0, 255), 0.75f, 0.00f },
};

static const D3DVERTEXELEMENT9 g_source_declaration[] =
{
    { 0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
    { 0, 12, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0 },
    { 0, 24, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0 },
    { 0, 28, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
    D3DDECL_END()
};

/* D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1, spelled out so the layout the
 * checks below assume is stated rather than inferred. */
static const D3DVERTEXELEMENT9 g_result_declaration[] =
{
    { 0,  0, D3DDECLTYPE_FLOAT4,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITIONT, 0 },
    { 0, 16, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,     0 },
    { 0, 20, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,  0 },
    D3DDECL_END()
};

static IDirect3D9 *g_d3d;
static IDirect3DDevice9 *g_device;
static IDirect3DVertexBuffer9 *g_source_buffer;
static IDirect3DVertexBuffer9 *g_result_buffer;
static IDirect3DVertexDeclaration9 *g_source_decl;
static IDirect3DVertexDeclaration9 *g_result_decl;
static HWND g_window;
static const char *g_failed_stage = "unknown stage";
static int g_failures;

static void trace_text(const char *text)
{
    OutputDebugStringA("[d3d9-processvertices] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
}

static void trace_hresult(const char *stage, HRESULT hr)
{
    char line[192];
    wsprintfA(line, "[d3d9-processvertices] %s -> 0x%08lX\r\n",
            stage, (unsigned long)hr);
    OutputDebugStringA(line);
}

static void begin_stage(const char *stage)
{
    g_failed_stage = stage;
    trace_text(stage);
}

static HRESULT failed(const char *stage, HRESULT hr)
{
    g_failed_stage = stage;
    trace_hresult(stage, hr);
    return hr;
}

/*
 * wsprintfA has no %f, and this DLL links no C runtime, so a float is reported
 * as a scaled integer pair. 1/1000 is far finer than any error worth seeing.
 */
static void trace_float(const char *label, float value, float expected)
{
    char line[192];
    long got = (long)(value * 1000.0f + (value < 0.0f ? -0.5f : 0.5f));
    long want = (long)(expected * 1000.0f + (expected < 0.0f ? -0.5f : 0.5f));
    wsprintfA(line, "[d3d9-processvertices] %s got %ld/1000 want %ld/1000\r\n",
            label, got, want);
    OutputDebugStringA(line);
}

/* Half a pixel: tight enough to catch a wrong viewport mapping or a missing
 * divide, loose enough to survive float accumulation across four matrices. */
static void expect_near(const char *label, float value, float expected,
        float tolerance)
{
    float delta = value - expected;
    if (delta < 0.0f) delta = -delta;
    if (delta <= tolerance) return;
    ++g_failures;
    trace_float(label, value, expected);
}

static void expect_equal_dword(const char *label, DWORD value, DWORD expected)
{
    char line[192];
    if (value == expected) return;
    ++g_failures;
    wsprintfA(line, "[d3d9-processvertices] %s got 0x%08lX want 0x%08lX\r\n",
            label, (unsigned long)value, (unsigned long)expected);
    OutputDebugStringA(line);
}

static void set_identity(D3DMATRIX *matrix)
{
    ZeroMemory(matrix, sizeof(*matrix));
    matrix->_11 = matrix->_22 = matrix->_33 = matrix->_44 = 1.0f;
}

/* The same row-vector convention D3D uses: v' = v * M. */
static void transform_vector4(const D3DMATRIX *matrix, const float *in,
        float *out)
{
    int row;
    for (row = 0; row < 4; ++row)
    {
        out[row] = in[0] * matrix->m[0][row] + in[1] * matrix->m[1][row]
                + in[2] * matrix->m[2][row] + in[3] * matrix->m[3][row];
    }
}

static void multiply_matrix(const D3DMATRIX *left, const D3DMATRIX *right,
        D3DMATRIX *out)
{
    int row, column;
    for (row = 0; row < 4; ++row)
    {
        for (column = 0; column < 4; ++column)
        {
            out->m[row][column] =
                    left->m[row][0] * right->m[0][column]
                    + left->m[row][1] * right->m[1][column]
                    + left->m[row][2] * right->m[2][column]
                    + left->m[row][3] * right->m[3][column];
        }
    }
}

static void release_d3d9(void)
{
    if (g_result_decl) { IDirect3DVertexDeclaration9_Release(g_result_decl); g_result_decl = NULL; }
    if (g_source_decl) { IDirect3DVertexDeclaration9_Release(g_source_decl); g_source_decl = NULL; }
    if (g_result_buffer) { IDirect3DVertexBuffer9_Release(g_result_buffer); g_result_buffer = NULL; }
    if (g_source_buffer) { IDirect3DVertexBuffer9_Release(g_source_buffer); g_source_buffer = NULL; }
    if (g_device) { IDirect3DDevice9_Release(g_device); g_device = NULL; }
    if (g_d3d) { IDirect3D9_Release(g_d3d); g_d3d = NULL; }
}

static HRESULT run_process_vertices(HWND hwnd)
{
    D3DPRESENT_PARAMETERS present;
    D3DVIEWPORT9 viewport;
    D3DMATRIX world, view, projection, world_view, world_view_projection;
    HRESULT hr;
    void *bits;
    UINT index;
    const ResultVertex *result;

    begin_stage("Direct3DCreate9");
    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d) return failed("Direct3DCreate9", E_FAIL);

    ZeroMemory(&present, sizeof(present));
    present.Windowed = TRUE;
    present.SwapEffect = D3DSWAPEFFECT_DISCARD;
    present.BackBufferFormat = D3DFMT_UNKNOWN;
    present.hDeviceWindow = hwnd;

    begin_stage("CreateDevice");
    hr = IDirect3D9_CreateDevice(g_d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
            hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &present, &g_device);
    if (FAILED(hr)) return failed("CreateDevice", hr);

    begin_stage("CreateVertexDeclaration(source)");
    hr = IDirect3DDevice9_CreateVertexDeclaration(g_device, g_source_declaration,
            &g_source_decl);
    if (FAILED(hr)) return failed("CreateVertexDeclaration(source)", hr);

    begin_stage("CreateVertexDeclaration(result)");
    hr = IDirect3DDevice9_CreateVertexDeclaration(g_device, g_result_declaration,
            &g_result_decl);
    if (FAILED(hr)) return failed("CreateVertexDeclaration(result)", hr);

    begin_stage("CreateVertexBuffer(source)");
    hr = IDirect3DDevice9_CreateVertexBuffer(g_device, sizeof(g_vertices), 0, 0,
            D3DPOOL_MANAGED, &g_source_buffer, NULL);
    if (FAILED(hr)) return failed("CreateVertexBuffer(source)", hr);

    begin_stage("Lock(source)");
    hr = IDirect3DVertexBuffer9_Lock(g_source_buffer, 0, sizeof(g_vertices),
            &bits, 0);
    if (FAILED(hr)) return failed("Lock(source)", hr);
    CopyMemory(bits, g_vertices, sizeof(g_vertices));
    IDirect3DVertexBuffer9_Unlock(g_source_buffer);

    begin_stage("CreateVertexBuffer(result)");
    hr = IDirect3DDevice9_CreateVertexBuffer(g_device,
            sizeof(ResultVertex) * TEST_VERTEX_COUNT, 0, 0, D3DPOOL_MANAGED,
            &g_result_buffer, NULL);
    if (FAILED(hr)) return failed("CreateVertexBuffer(result)", hr);

    /* A viewport that is neither the full target nor at the origin, so its
     * offset and scale both participate in the expected answer. */
    viewport.X = TEST_VIEWPORT_X;
    viewport.Y = TEST_VIEWPORT_Y;
    viewport.Width = TEST_VIEWPORT_WIDTH;
    viewport.Height = TEST_VIEWPORT_HEIGHT;
    viewport.MinZ = 0.0f;
    viewport.MaxZ = 1.0f;
    begin_stage("SetViewport");
    hr = IDirect3DDevice9_SetViewport(g_device, &viewport);
    if (FAILED(hr)) return failed("SetViewport", hr);

    set_identity(&world);
    world._11 = 2.0f;
    world._22 = 3.0f;
    world._41 = 0.5f;
    world._42 = -0.25f;

    set_identity(&view);
    view._43 = 6.0f; /* push the geometry away from the eye */

    /* A plain perspective projection: 90 degrees vertical, 4:3, near 1, far
     * 100. Written out rather than built with D3DX, which this test cannot
     * link. */
    ZeroMemory(&projection, sizeof(projection));
    projection._11 = 1.0f / (4.0f / 3.0f);
    projection._22 = 1.0f;
    projection._33 = 100.0f / (100.0f - 1.0f);
    projection._34 = 1.0f;
    projection._43 = -1.0f * 100.0f / (100.0f - 1.0f);

    begin_stage("SetTransform");
    hr = IDirect3DDevice9_SetTransform(g_device, D3DTS_WORLD, &world);
    if (FAILED(hr)) return failed("SetTransform(WORLD)", hr);
    hr = IDirect3DDevice9_SetTransform(g_device, D3DTS_VIEW, &view);
    if (FAILED(hr)) return failed("SetTransform(VIEW)", hr);
    hr = IDirect3DDevice9_SetTransform(g_device, D3DTS_PROJECTION, &projection);
    if (FAILED(hr)) return failed("SetTransform(PROJECTION)", hr);

    /* Lighting off keeps the diffuse channel a pure copy, so a colour
     * mismatch means the copy-through is broken rather than the (separately
     * exercised) lighting maths. */
    IDirect3DDevice9_SetRenderState(g_device, D3DRS_LIGHTING, FALSE);

    begin_stage("SetStreamSource");
    hr = IDirect3DDevice9_SetStreamSource(g_device, 0, g_source_buffer, 0,
            sizeof(SourceVertex));
    if (FAILED(hr)) return failed("SetStreamSource", hr);
    hr = IDirect3DDevice9_SetVertexDeclaration(g_device, g_source_decl);
    if (FAILED(hr)) return failed("SetVertexDeclaration", hr);

    begin_stage("ProcessVertices");
    hr = IDirect3DDevice9_ProcessVertices(g_device, 0, 0, TEST_VERTEX_COUNT,
            g_result_buffer, g_result_decl, 0);
    if (FAILED(hr)) return failed("ProcessVertices", hr);

    multiply_matrix(&world, &view, &world_view);
    multiply_matrix(&world_view, &projection, &world_view_projection);

    begin_stage("Lock(result)");
    hr = IDirect3DVertexBuffer9_Lock(g_result_buffer, 0, 0, &bits, D3DLOCK_READONLY);
    if (FAILED(hr)) return failed("Lock(result)", hr);
    result = (const ResultVertex *)bits;

    for (index = 0; index < TEST_VERTEX_COUNT; ++index)
    {
        float position[4];
        float clip[4];
        float rhw;
        float expect_x, expect_y, expect_z;
        char label[64];

        position[0] = g_vertices[index].x;
        position[1] = g_vertices[index].y;
        position[2] = g_vertices[index].z;
        position[3] = 1.0f;
        transform_vector4(&world_view_projection, position, clip);
        rhw = (clip[3] > 1e-6f || clip[3] < -1e-6f) ? 1.0f / clip[3] : 1.0f;
        expect_x = (clip[0] * rhw * 0.5f + 0.5f) * (float)TEST_VIEWPORT_WIDTH
                + (float)TEST_VIEWPORT_X;
        expect_y = (0.5f - clip[1] * rhw * 0.5f) * (float)TEST_VIEWPORT_HEIGHT
                + (float)TEST_VIEWPORT_Y;
        expect_z = clip[2] * rhw;

        wsprintfA(label, "vertex %lu x", (unsigned long)index);
        expect_near(label, result[index].x, expect_x, 0.5f);
        wsprintfA(label, "vertex %lu y", (unsigned long)index);
        expect_near(label, result[index].y, expect_y, 0.5f);
        wsprintfA(label, "vertex %lu z", (unsigned long)index);
        expect_near(label, result[index].z, expect_z, 0.01f);
        wsprintfA(label, "vertex %lu rhw", (unsigned long)index);
        expect_near(label, result[index].rhw, rhw, 0.01f);
        wsprintfA(label, "vertex %lu colour", (unsigned long)index);
        expect_equal_dword(label, result[index].color, g_vertices[index].color);
        wsprintfA(label, "vertex %lu u", (unsigned long)index);
        expect_near(label, result[index].u, g_vertices[index].u, 0.001f);
        wsprintfA(label, "vertex %lu v", (unsigned long)index);
        expect_near(label, result[index].v, g_vertices[index].v, 0.001f);
    }
    IDirect3DVertexBuffer9_Unlock(g_result_buffer);

    /* Lighting is disabled in this case. DONOTCOPYDATA transforms position
     * while preserving these passthrough channels. The shared three-API XP
     * probe separately checks that enabled lighting still writes colors. */
    begin_stage("ProcessVertices(D3DPV_DONOTCOPYDATA)");
    hr = IDirect3DDevice9_ProcessVertices(g_device, 0, 0, TEST_VERTEX_COUNT,
            g_result_buffer, g_result_decl, D3DPV_DONOTCOPYDATA);
    if (FAILED(hr)) return failed("ProcessVertices(DONOTCOPYDATA)", hr);

    g_failed_stage = g_failures ? "value checks" : "all checks passed";
    return g_failures ? E_FAIL : D3D_OK;
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam,
        LPARAM lparam)
{
    if (message == WM_DESTROY)
    {
        g_window = NULL;
        release_d3d9();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

static int run_test(HINSTANCE instance, int show_command)
{
    WNDCLASSA window_class;
    RECT window_rect;
    HWND hwnd;
    MSG message;
    HRESULT hr;
    char title[192];

    ZeroMemory(&window_class, sizeof(window_class));
    window_class.style = CS_OWNDC;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorA(NULL, IDC_ARROW);
    window_class.lpszClassName = g_window_class;
    if (!RegisterClassA(&window_class))
    {
        trace_text("RegisterClass failed");
        return 1;
    }

    SetRect(&window_rect, 0, 0, TEST_CLIENT_WIDTH, TEST_CLIENT_HEIGHT);
    AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd = CreateWindowA(g_window_class, "D3D9 ProcessVertices: starting",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            window_rect.right - window_rect.left,
            window_rect.bottom - window_rect.top,
            NULL, NULL, instance, NULL);
    if (!hwnd) { trace_text("CreateWindow failed"); return 2; }

    g_window = hwnd;
    ShowWindow(hwnd, show_command);
    UpdateWindow(hwnd);

    hr = run_process_vertices(hwnd);
    wsprintfA(title, "D3D9 ProcessVertices: %s (%ld mismatch(es), 0x%08lX)",
            g_failed_stage, (long)g_failures, (unsigned long)hr);
    SetWindowTextA(hwnd, title);
    trace_text(title);

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
