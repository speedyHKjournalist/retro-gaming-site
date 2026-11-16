/* Actual SDK COM draws: raw fixed declarations paired with CPU processing. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <d3d8.h>
#include <stdint.h>
#include <windows.h>
#define CALL(n) IDirect3DDevice8_##n
#define BUF(n) IDirect3DVertexBuffer8_##n
#define IB(n) IDirect3DIndexBuffer8_##n
#define FACT(n) IDirect3D8_##n
static HANDLE logfile;
static IDirect3DDevice8* device;
static IDirect3DVertexBuffer8 *primary, *secondary, *output;
static IDirect3DIndexBuffer8* indices;
static void report(const char* s)
{
    DWORD n;
    WriteFile(logfile, s, lstrlenA(s), &n, NULL);
    WriteFile(logfile, "\r\n", 2, &n, NULL);
    FlushFileBuffers(logfile);
}
static void check(BOOL yes, const char* s)
{
    if (!yes) {
        report(s);
        ExitProcess(1);
    }
}
#define OK(x) check(SUCCEEDED(x), "FAIL " #x)
static void state(D3DRENDERSTATETYPE name, DWORD value) { OK(CALL(SetRenderState)(device, name, value)); }
static void floating(D3DRENDERSTATETYPE name, float value)
{
    DWORD word;
    CopyMemory(&word, &value, 4);
    state(name, word);
}
static void identity(D3DMATRIX* m)
{
    ZeroMemory(m, sizeof(*m));
    m->_11 = m->_22 = m->_33 = m->_44 = 1;
}
struct Input {
    float x, y, z, nx, ny, nz;
};
struct Output {
    float x, y, z, rhw;
    DWORD diffuse, specular;
};
struct Scene {
    UINT declaration;
    float factor;
    BOOL indexed, constant, plane, normalize;
};
static const struct Scene scenes[] = { { 0, .25f, FALSE, FALSE, FALSE, FALSE }, { 0, .75f, TRUE, FALSE, FALSE, FALSE }, { 1, .75f, FALSE, FALSE, FALSE, FALSE }, { 2, .75f, FALSE, FALSE, FALSE, FALSE }, { 0, .25f, FALSE, TRUE, FALSE, FALSE }, { 0, .75f, TRUE, FALSE, TRUE, FALSE }, { 0, .75f, FALSE, FALSE, FALSE, TRUE }, { 3, .75f, FALSE, FALSE, FALSE, FALSE } };
static void upload(IDirect3DVertexBuffer8* buffer, const void* source, UINT size)
{
    BYTE* data;
    OK(BUF(Lock)(buffer, 0, size, &data, 0));
    CopyMemory(data, source, size);
    OK(BUF(Unlock)(buffer));
}
static void frame(const struct Scene* scene, BOOL processed)
{
    if (processed) {
        OK(CALL(ProcessVertices)(device, 2, 0, 3, output, 0));
        OK(CALL(SetVertexShader)(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR));
        OK(CALL(SetStreamSource)(device, 0, output, sizeof(struct Output)));
        state(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
        state(D3DRS_LIGHTING, FALSE);
        state(D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
    }
    OK(CALL(Clear)(device, 0, NULL, D3DCLEAR_TARGET, 0xff101010, 1, 0));
    OK(CALL(BeginScene)(device));
    if (scene->indexed && !processed)
        OK(CALL(DrawIndexedPrimitive)(device, D3DPT_TRIANGLELIST, 1, 3, 0, 1));
    else
        OK(CALL(DrawPrimitive)(device, D3DPT_TRIANGLELIST, processed ? 0 : 2, 1));
    OK(CALL(EndScene)(device));
    OK(CALL(Present)(device, NULL, NULL, NULL, NULL));
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    unsigned at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    lstrcpyA(path, directory);
    lstrcatA(path, "TWEEN.TXT");
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    lstrcpyA(path, directory);
    lstrcatA(path, "D3D8.DLL");
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL local DLL");
    IDirect3D8*(WINAPI * make)(UINT) = (void*)(uintptr_t)GetProcAddress(module, "Direct3DCreate8");
    check(make != NULL, "FAIL export");
    IDirect3D8* factory = make(D3D_SDK_VERSION);
    check(factory != NULL, "FAIL factory");
    HWND window = CreateWindowExA(0, "STATIC", "Tween probe", WS_OVERLAPPEDWINDOW, 0, 0, 96, 96, NULL, NULL, GetModuleHandleA(NULL), NULL);
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.Windowed = TRUE;
    pp.hDeviceWindow = window;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    OK(FACT(CreateDevice)(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device));
    D3DCAPS8 factory_caps, device_caps;
    OK(FACT(GetDeviceCaps)(factory, 0, D3DDEVTYPE_HAL, &factory_caps));
    OK(CALL(GetDeviceCaps)(device, &device_caps));
    check((factory_caps.VertexProcessingCaps & D3DVTXPCAPS_TWEENING) && (device_caps.VertexProcessingCaps & D3DVTXPCAPS_TWEENING), "FAIL factory/device Tween caps");
    IDirect3DSurface8* target;
    OK(CALL(CreateRenderTarget)(device, 64, 64, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, FALSE, &target));
    OK(CALL(SetRenderTarget)(device, target, NULL));
    D3DVIEWPORT8 viewport = { 0, 0, 64, 64, 0, 1 };
    OK(CALL(SetViewport)(device, &viewport));
    const DWORD declarations[4][8] = {
        { D3DVSD_STREAM(15), D3DVSD_REG(D3DVSDE_POSITION, D3DVSDT_FLOAT3), D3DVSD_REG(D3DVSDE_NORMAL, D3DVSDT_FLOAT3), D3DVSD_STREAM(1), D3DVSD_REG(D3DVSDE_POSITION2, D3DVSDT_FLOAT3), D3DVSD_REG(D3DVSDE_NORMAL2, D3DVSDT_FLOAT3), D3DVSD_END() },
        { D3DVSD_STREAM(15), D3DVSD_REG(D3DVSDE_POSITION, D3DVSDT_FLOAT3), D3DVSD_REG(D3DVSDE_NORMAL, D3DVSDT_FLOAT3), D3DVSD_STREAM(1), D3DVSD_REG(D3DVSDE_POSITION2, D3DVSDT_FLOAT3), D3DVSD_END() },
        { D3DVSD_STREAM(15), D3DVSD_REG(D3DVSDE_POSITION, D3DVSDT_FLOAT3), D3DVSD_REG(D3DVSDE_NORMAL, D3DVSDT_FLOAT3), D3DVSD_STREAM(1), D3DVSD_SKIP(3), D3DVSD_REG(D3DVSDE_NORMAL2, D3DVSDT_FLOAT3), D3DVSD_END() },
        { D3DVSD_STREAM(15), D3DVSD_REG(D3DVSDE_POSITION, D3DVSDT_FLOAT3), D3DVSD_STREAM(1), D3DVSD_SKIP(3), D3DVSD_REG(D3DVSDE_NORMAL2, D3DVSDT_FLOAT3), D3DVSD_END() }
    };
    DWORD handles[4];
    for (unsigned i = 0; i < 4; ++i)
        OK(CALL(CreateVertexShader)(device, declarations[i], NULL, &handles[i], 0));
    const struct Input first[5] = { { -.875f, -.875f, .25f, 0, 0, 1 }, { -.875f, -.875f, .25f, 0, 0, 1 }, { -.75f, -.5f, .25f, 0, 0, 1 }, { -.25f, .5f, .25f, 0, 0, 1 }, { .25f, -.5f, .25f, 0, 0, 1 } };
    struct Input second[5];
    for (unsigned i = 0; i < 5; ++i)
        second[i] = (struct Input) { first[i].x + .5f, first[i].y + .25f, .75f, 0, 1, 0 };
    second[0] = (struct Input) { .5f, 0, .75f, 0, 1, 0 };
    OK(CALL(CreateVertexBuffer)(device, sizeof(first), 0, 0, D3DPOOL_MANAGED, &primary));
    OK(CALL(CreateVertexBuffer)(device, sizeof(second), 0, 0, D3DPOOL_MANAGED, &secondary));
    OK(CALL(CreateVertexBuffer)(device, 3 * sizeof(struct Output), 0, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR, D3DPOOL_MANAGED, &output));
    upload(primary, first, sizeof(first));
    upload(secondary, second, sizeof(second));
    OK(CALL(CreateIndexBuffer)(device, 6, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &indices));
    BYTE* data;
    OK(IB(Lock)(indices, 0, 6, &data, 0));
    ((WORD*)data)[0] = 1;
    ((WORD*)data)[1] = 2;
    ((WORD*)data)[2] = 3;
    OK(IB(Unlock)(indices));
    OK(CALL(SetIndices)(device, indices, 1));
    D3DMATRIX m;
    identity(&m);
    m._11 = 2;
    OK(CALL(SetTransform)(device, D3DTS_WORLD, &m));
    identity(&m);
    m._42 = -.125f;
    OK(CALL(SetTransform)(device, D3DTS_VIEW, &m));
    identity(&m);
    m._33 = .5f;
    OK(CALL(SetTransform)(device, D3DTS_PROJECTION, &m));
    D3DMATERIAL8 material;
    ZeroMemory(&material, sizeof(material));
    material.Diffuse = (D3DCOLORVALUE) { 1, .5f, .25f, .75f };
    OK(CALL(SetMaterial)(device, &material));
    D3DLIGHT8 lamp;
    ZeroMemory(&lamp, sizeof(lamp));
    lamp.Type = D3DLIGHT_DIRECTIONAL;
    lamp.Direction.y = -1;
    lamp.Diffuse = (D3DCOLORVALUE) { 1, 1, 1, 1 };
    OK(CALL(SetLight)(device, 0, &lamp));
    OK(CALL(LightEnable)(device, 0, TRUE));
    state(D3DRS_ZENABLE, FALSE);
    state(D3DRS_CULLMODE, D3DCULL_NONE);
    state(D3DRS_COLORVERTEX, FALSE);
    state(D3DRS_AMBIENT, 0);
    state(D3DRS_SPECULARENABLE, FALSE);
    state(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
    state(D3DRS_FOGENABLE, TRUE);
    state(D3DRS_FOGCOLOR, 0xff204060);
    state(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
    state(D3DRS_RANGEFOGENABLE, FALSE);
    floating(D3DRS_FOGSTART, 0);
    floating(D3DRS_FOGEND, 1);
    state(D3DRS_CLIPPING, TRUE);
    state(D3DRS_INDEXEDVERTEXBLENDENABLE, TRUE);
    OK(CALL(SetTextureStageState)(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1));
    OK(CALL(SetTextureStageState)(device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE));
    OK(CALL(SetTextureStageState)(device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1));
    OK(CALL(SetTextureStageState)(device, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE));
    OK(CALL(SetTextureStageState)(device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE));
    float plane[4] = { 1, 0, 0, 0 };
    OK(CALL(SetClipPlane)(device, 0, plane));
    for (unsigned i = 0; i < sizeof(scenes) / sizeof(scenes[0]); ++i) {
        const struct Scene* scene = &scenes[i];
        OK(CALL(SetVertexShader)(device, handles[scene->declaration]));
        OK(CALL(SetStreamSource)(device, 0, NULL, 0));
        OK(CALL(SetStreamSource)(device, 15, primary, sizeof(struct Input)));
        OK(CALL(SetStreamSource)(device, 1, secondary, scene->constant ? 0 : sizeof(struct Input)));
        state(D3DRS_VERTEXBLEND, D3DVBF_TWEENING);
        state(D3DRS_LIGHTING, TRUE);
        state(D3DRS_NORMALIZENORMALS, scene->normalize);
        state(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
        state(D3DRS_CLIPPLANEENABLE, scene->plane ? 1 : 0);
        floating(D3DRS_TWEENFACTOR, scene->factor);
        frame(scene, FALSE);
        frame(scene, TRUE);
    }
    OK(CALL(SetVertexShader)(device, 0));
    for (unsigned i = 0; i < 4; ++i)
        OK(CALL(DeleteVertexShader)(device, handles[i]));
    OK(CALL(SetStreamSource)(device, 0, NULL, 0));
    OK(CALL(SetStreamSource)(device, 15, NULL, 0));
    OK(CALL(SetStreamSource)(device, 1, NULL, 0));
    OK(CALL(SetIndices)(device, NULL, 0));
    BUF(Release)(primary);
    BUF(Release)(secondary);
    BUF(Release)(output);
    IB(Release)(indices);
    IDirect3DSurface8_Release(target);
    CALL(Release)(device);
    FACT(Release)(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    report("PASS D3D8 Tween draws: 16 paired declaration/CPU frames, factors, independent channels, indexed base, constant stream, user plane and normalized normals");
    CloseHandle(logfile);
    ExitProcess(0);
}
