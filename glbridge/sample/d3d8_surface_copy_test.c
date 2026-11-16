/* XP draw hints: real COM calls and signed-base geometry, without native HAL claims. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_DRAW_API == 8
#include <d3d8.h>
typedef IDirect3D8 Factory;
typedef IDirect3DDevice8 Device;
typedef IDirect3DVertexBuffer8 VertexBuffer;
typedef IDirect3DIndexBuffer8 IndexBuffer;
#define VB_LOCK IDirect3DVertexBuffer8_Lock
#define IB_LOCK IDirect3DIndexBuffer8_Lock
#define VB_UNLOCK IDirect3DVertexBuffer8_Unlock
#define IB_UNLOCK IDirect3DIndexBuffer8_Unlock
#define VB_RELEASE IDirect3DVertexBuffer8_Release
#define IB_RELEASE IDirect3DIndexBuffer8_Release
#define DEV(method) IDirect3DDevice8_##method
#define FACTORY_RELEASE IDirect3D8_Release
#else
#include <d3d9.h>
typedef IDirect3D9 Factory;
typedef IDirect3DDevice9 Device;
typedef IDirect3DVertexBuffer9 VertexBuffer;
typedef IDirect3DIndexBuffer9 IndexBuffer;
#define VB_LOCK IDirect3DVertexBuffer9_Lock
#define IB_LOCK IDirect3DIndexBuffer9_Lock
#define VB_UNLOCK IDirect3DVertexBuffer9_Unlock
#define IB_UNLOCK IDirect3DIndexBuffer9_Unlock
#define VB_RELEASE IDirect3DVertexBuffer9_Release
#define IB_RELEASE IDirect3DIndexBuffer9_Release
#define DEV(method) IDirect3DDevice9_##method
#define FACTORY_RELEASE IDirect3D9_Release
#endif
static Factory* factory;
static Device* device;
static HANDLE logfile;
static void report(const char* text)
{
    DWORD written;
    WriteFile(logfile, text, lstrlenA(text), &written, NULL);
    WriteFile(logfile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(logfile);
}
static void check(BOOL condition, const char* message)
{
    if (!condition) {
        report(message);
        ExitProcess(1);
    }
}

static DWORD expected_source[64 * 64], expected_back[64 * 64], expected_tile[32 * 32], tile_before[32 * 32], expected_large[80 * 80], expected_cube[32 * 32], expected_small[16 * 16];
static void verify(IDirect3DSurface8* surface, UINT width, UINT height, const DWORD* expected, const char* name)
{
    D3DLOCKED_RECT lock;
    check(IDirect3DSurface8_LockRect(surface, &lock, NULL, D3DLOCK_READONLY) == D3D_OK, "FAIL verify lock");
    for (UINT y = 0; y < height; ++y) {
        const DWORD* row = (const DWORD*)((const BYTE*)lock.pBits + y * lock.Pitch);
        for (UINT x = 0; x < width; ++x)
            check(row[x] == expected[y * width + x], "FAIL surface pixels");
    }
    check(IDirect3DSurface8_UnlockRect(surface) == D3D_OK, "FAIL verify unlock");
    report(name);
}
static void fill(IDirect3DSurface8* surface, UINT width, UINT height, const DWORD* pixels)
{
    D3DLOCKED_RECT lock;
    check(IDirect3DSurface8_LockRect(surface, &lock, NULL, 0) == D3D_OK, "FAIL fill lock");
    for (UINT y = 0; y < height; ++y)
        CopyMemory((BYTE*)lock.pBits + y * lock.Pitch, pixels + y * width, width * 4);
    check(IDirect3DSurface8_UnlockRect(surface) == D3D_OK, "FAIL fill unlock");
}
static void draw_rect(float left, float top, float right, float bottom, DWORD color)
{
    struct {
        float x, y, z, rhw;
        DWORD color;
    } v[4] = { { left, top, .5f, 1, color }, { right, top, .5f, 1, color }, { left, bottom, .5f, 1, color }, { right, bottom, .5f, 1, color } };
    check(DEV(BeginScene)(device) == D3D_OK, "FAIL begin");
    check(DEV(DrawPrimitiveUP)(device, D3DPT_TRIANGLESTRIP, 2, v, sizeof(v[0])) == D3D_OK, "FAIL draw");
    check(DEV(EndScene)(device) == D3D_OK, "FAIL end");
}
static void front(IDirect3DSurface8* image)
{
    for (UINT i = 0; i < 80 * 80; ++i)
        expected_large[i] = 0xff204060u;
    fill(image, 80, 80, expected_large);
    for (UINT y = 0; y < 64; ++y)
        for (UINT x = 0; x < 64; ++x)
            expected_large[y * 80 + x] = expected_back[y * 64 + x];
    check(DEV(GetFrontBuffer)(device, image) == D3D_OK, "FAIL GetFrontBuffer");
    verify(image, 80, 80, expected_large, "CHECK front opaque content and retained exterior");
}
static void copies(void)
{
    IDirect3DSurface8 *back = NULL, *target = NULL, *dest = NULL, *image = NULL, *large = NULL, *tile = NULL, *cube_face = NULL, *small_dest = NULL, *small_cpu = NULL, *system_level = NULL, *dest_level = NULL;
    IDirect3DTexture8 *texture = NULL, *small_texture = NULL, *system_texture = NULL;
    IDirect3DCubeTexture8* cube = NULL;
    check(DEV(GetRenderTarget)(device, &back) == D3D_OK, "FAIL back target");
    check(DEV(CreateRenderTarget)(device, 64, 64, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, TRUE, &target) == D3D_OK, "FAIL create RT");
    check(DEV(SetRenderTarget)(device, target, NULL) == D3D_OK, "FAIL set RT");
    D3DVIEWPORT8 viewport = { 0, 0, 64, 64, 0, 1 };
    check(DEV(SetViewport)(device, &viewport) == D3D_OK, "FAIL viewport");
    DEV(SetRenderState)(device, D3DRS_LIGHTING, FALSE);
    DEV(SetRenderState)(device, D3DRS_ZENABLE, FALSE);
    DEV(SetRenderState)(device, D3DRS_CULLMODE, D3DCULL_NONE);
    DEV(SetRenderState)(device, D3DRS_ALPHABLENDENABLE, FALSE);
    DEV(SetRenderState)(device, D3DRS_ALPHATESTENABLE, FALSE);
    DEV(SetRenderState)(device, D3DRS_FOGENABLE, FALSE);
    DEV(SetTexture)(device, 0, NULL);
    DEV(SetTextureStageState)(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    DEV(SetTextureStageState)(device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    DEV(SetTextureStageState)(device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    DEV(SetTextureStageState)(device, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    check(DEV(SetVertexShader)(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE) == D3D_OK, "FAIL FVF");
    check(DEV(Clear)(device, 0, NULL, D3DCLEAR_TARGET, 0xff101010u, 1, 0) == D3D_OK, "FAIL clear");
    draw_rect(0, 0, 32, 64, 0xffff0000u);
    draw_rect(32, 0, 64, 64, 0xff00ff00u);
    for (UINT y = 0; y < 64; ++y)
        for (UINT x = 0; x < 64; ++x)
            expected_source[y * 64 + x] = x < 32 ? 0xffff0000u : 0xff00ff00u;
    verify(target, 64, 64, expected_source, "CHECK RT readonly sees rendered bytes");
    check(DEV(CreateTexture)(device, 64, 64, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &texture) == D3D_OK, "FAIL create default texture");
    check(IDirect3DTexture8_GetSurfaceLevel(texture, 0, &dest) == D3D_OK, "FAIL dest view");
    check(DEV(CopyRects)(device, target, NULL, 0, dest, NULL) == D3D_OK, "FAIL RT to texture");
    verify(dest, 64, 64, expected_source, "CHECK GPU copy invalidates texture mirror");
    check(DEV(CreateImageSurface)(device, 64, 64, D3DFMT_X8R8G8B8, &image) == D3D_OK, "FAIL image");
    check(DEV(CopyRects)(device, target, NULL, 0, image, NULL) == D3D_OK, "FAIL RT to CPU");
    verify(image, 64, 64, expected_source, "CHECK RT to CPU copy");
    RECT patch = { 8, 8, 16, 16 };
    D3DLOCKED_RECT lock;
    check(IDirect3DSurface8_LockRect(target, &lock, &patch, 0) == D3D_OK, "FAIL RT write lock");
    for (UINT y = 0; y < 8; ++y)
        for (UINT x = 0; x < 8; ++x)
            ((DWORD*)((BYTE*)lock.pBits + y * lock.Pitch))[x] = 0xff0000ffu;
    check(IDirect3DSurface8_UnlockRect(target) == D3D_OK, "FAIL RT write unlock");
    for (UINT y = 8; y < 16; ++y)
        for (UINT x = 8; x < 16; ++x)
            expected_source[y * 64 + x] = 0xff0000ffu;
    verify(target, 64, 64, expected_source, "CHECK partial RT write preserves rendered exterior");
    check(DEV(SetRenderTarget)(device, back, NULL) == D3D_OK, "FAIL restore back target");
    check(DEV(CopyRects)(device, target, NULL, 0, back, NULL) == D3D_OK, "FAIL RT to back buffer");
    CopyMemory(expected_back, expected_source, sizeof(expected_back));
    check(DEV(CreateImageSurface)(device, 80, 80, D3DFMT_A8R8G8B8, &large) == D3D_OK, "FAIL large front image");
    front(large);
    check(DEV(CreateImageSurface)(device, 32, 32, D3DFMT_X8R8G8B8, &tile) == D3D_OK, "FAIL tile image");
    for (UINT y = 0; y < 32; ++y)
        for (UINT x = 0; x < 32; ++x)
            expected_tile[y * 32 + x] = 0xff000000u | (((x * 7 + 17) & 255) << 16) | (((y * 9 + 23) & 255) << 8) | (((x + y) * 5) & 255);
    fill(tile, 32, 32, expected_tile);
    CopyMemory(tile_before, expected_tile, sizeof(tile_before));
    RECT overlap = { 0, 0, 24, 24 };
    POINT shifted = { 4, 4 };
    check(DEV(CopyRects)(device, tile, &overlap, 1, tile, &shifted) == D3D_OK, "FAIL overlap snapshot");
    for (UINT y = 0; y < 24; ++y)
        for (UINT x = 0; x < 24; ++x)
            expected_tile[(y + 4) * 32 + x + 4] = tile_before[y * 32 + x];
    verify(tile, 32, 32, expected_tile, "CHECK overlapping CPU source snapshot");
    POINT placed = { 16, 16 };
    check(DEV(CopyRects)(device, tile, NULL, 0, back, &placed) == D3D_OK, "FAIL full copy with unused points");
    /* A non-NULL pointer with count zero is a no-op; use an explicit rect for placement. */
    RECT full_tile = { 0, 0, 32, 32 };
    check(DEV(CopyRects)(device, tile, &full_tile, 1, back, &placed) == D3D_OK, "FAIL CPU image to back buffer");
    for (UINT y = 0; y < 32; ++y)
        for (UINT x = 0; x < 32; ++x)
            expected_back[(y + 16) * 64 + x + 16] = expected_tile[y * 32 + x];
    front(large);
    check(DEV(CreateCubeTexture)(device, 64, 2, 0, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &cube) == D3D_OK, "FAIL cube");
    check(IDirect3DCubeTexture8_GetCubeMapSurface(cube, D3DCUBEMAP_FACE_POSITIVE_Z, 1, &cube_face) == D3D_OK, "FAIL cube view");
    RECT crop = { 0, 0, 32, 32 };
    POINT origin = { 0, 0 };
    check(DEV(CopyRects)(device, target, &crop, 1, cube_face, &origin) == D3D_OK, "FAIL RT to cube mip");
    for (UINT y = 0; y < 32; ++y)
        for (UINT x = 0; x < 32; ++x)
            expected_cube[y * 32 + x] = expected_source[y * 64 + x];
    verify(cube_face, 32, 32, expected_cube, "CHECK GPU cube face and mip mirror");
    check(DEV(CreateTexture)(device, 32, 32, 2, 0, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &small_texture) == D3D_OK, "FAIL small default texture");
    check(IDirect3DTexture8_GetSurfaceLevel(small_texture, 0, &small_dest) == D3D_OK, "FAIL small destination view");
    check(DEV(CopyRects)(device, cube_face, NULL, 0, small_dest, NULL) == D3D_OK, "FAIL cube to texture");
    verify(small_dest, 32, 32, expected_cube, "CHECK cube source face and mip GPU copy");
    check(DEV(CreateImageSurface)(device, 32, 32, D3DFMT_X8R8G8B8, &small_cpu) == D3D_OK, "FAIL small CPU image");
    check(DEV(CopyRects)(device, cube_face, NULL, 0, small_cpu, NULL) == D3D_OK, "FAIL cube to CPU");
    verify(small_cpu, 32, 32, expected_cube, "CHECK valid cube mirror CPU copy");
    check(DEV(CreateTexture)(device, 32, 32, 2, 0, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &system_texture) == D3D_OK, "FAIL system texture");
    check(IDirect3DTexture8_GetSurfaceLevel(system_texture, 1, &system_level) == D3D_OK, "FAIL system mip view");
    check(IDirect3DTexture8_GetSurfaceLevel(small_texture, 1, &dest_level) == D3D_OK, "FAIL destination mip view");
    check(IDirect3DSurface8_LockRect(system_level, &lock, NULL, 0) == D3D_OK, "FAIL system mip lock");
    for (UINT y = 0; y < 16; ++y)
        for (UINT x = 0; x < 16; ++x)
            ((DWORD*)((BYTE*)lock.pBits + y * lock.Pitch))[x] = 0xffffff00u;
    check(DEV(CopyRects)(device, system_level, NULL, 0, dest_level, NULL) == D3DERR_INVALIDCALL, "FAIL locked mip accepted");
    check(IDirect3DSurface8_UnlockRect(system_level) == D3D_OK, "FAIL system mip unlock");
    check(DEV(CopyRects)(device, system_level, NULL, 0, dest_level, NULL) == D3D_OK, "FAIL unlocked mip copy");
    for (UINT i = 0; i < 16 * 16; ++i)
        expected_small[i] = 0xffffff00u;
    verify(dest_level, 16, 16, expected_small, "CHECK locked subresource refusal then recovery");
    check(DEV(CopyRects)(device, target, NULL, 0, large, NULL) == D3DERR_INVALIDCALL, "FAIL format mismatch accepted");
    POINT invalid = { 0x7fffffff, 0 };
    check(DEV(CopyRects)(device, target, &crop, 1, image, &invalid) == D3DERR_INVALIDCALL, "FAIL oversized destination accepted");
    lock.Pitch = 123;
    lock.pBits = (void*)0x1234;
    check(IDirect3DSurface8_LockRect(target, &lock, NULL, D3DLOCK_READONLY) == D3DERR_DRIVERINTERNALERROR && lock.Pitch == 123 && lock.pBits == (void*)0x1234, "FAIL failed RT lock output or result");
    verify(target, 64, 64, expected_source, "CHECK failed RT readback lock rollback and retry");
    IDirect3DSurface8_Release(dest_level);
    IDirect3DSurface8_Release(system_level);
    IDirect3DTexture8_Release(system_texture);
    IDirect3DSurface8_Release(small_cpu);
    IDirect3DSurface8_Release(small_dest);
    IDirect3DTexture8_Release(small_texture);
    IDirect3DSurface8_Release(cube_face);
    IDirect3DCubeTexture8_Release(cube);
    IDirect3DSurface8_Release(tile);
    IDirect3DSurface8_Release(large);
    IDirect3DSurface8_Release(image);
    IDirect3DSurface8_Release(dest);
    IDirect3DTexture8_Release(texture);
    IDirect3DSurface8_Release(target);
    IDirect3DSurface8_Release(back);
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[40];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    lstrcpyA(name, "SURF8.TXT");
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    HWND window = CreateWindowExA(0, "STATIC", "D3D8 surface copy probe", WS_OVERLAPPEDWINDOW, 30, 40, 160, 160, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    wsprintfA(name, "D3D%u.DLL", D9WG_DRAW_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_DRAW_API);
    Factory*(WINAPI * make)(UINT) = (void*)(uintptr_t)GetProcAddress(module, name);
    check(make != NULL, "FAIL factory export");
    factory = make(D3D_SDK_VERSION);
    check(factory != NULL, "FAIL factory");
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
#if D9WG_DRAW_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    copies();
    DEV(Release)(device);
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS D3D8 surface copies API=%u", D9WG_DRAW_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
