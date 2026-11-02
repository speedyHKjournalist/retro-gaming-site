/* Actual D3D8/9 COM calls, captured on XP for a separate WebGPU replay.
 * The procedural triangles match the independent D3D7 clipping reference. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_CLIP_API == 9
#include <d3d9.h>
typedef IDirect3D9 Factory;
typedef IDirect3DDevice9 Device;
typedef IDirect3DVertexBuffer9 Buffer;
typedef IDirect3DIndexBuffer9 Indices;
typedef D3DVIEWPORT9 Viewport;
#define CALL(name) IDirect3DDevice9_##name
#define BUF(name) IDirect3DVertexBuffer9_##name
#define IB(name) IDirect3DIndexBuffer9_##name
#define FACT(name) IDirect3D9_##name
#else
#include <d3d8.h>
typedef IDirect3D8 Factory;
typedef IDirect3DDevice8 Device;
typedef IDirect3DVertexBuffer8 Buffer;
typedef IDirect3DIndexBuffer8 Indices;
typedef D3DVIEWPORT8 Viewport;
#define CALL(name) IDirect3DDevice8_##name
#define BUF(name) IDirect3DVertexBuffer8_##name
#define IB(name) IDirect3DIndexBuffer8_##name
#define FACT(name) IDirect3D8_##name
#endif
static HANDLE logfile;
static Device *device;
static void report(const char *s) {
  DWORD written;
  WriteFile(logfile, s, lstrlenA(s), &written, NULL);
  WriteFile(logfile, "\r\n", 2, &written, NULL);
  FlushFileBuffers(logfile);
}
static void check(BOOL valid, const char *where) {
  if (!valid) {
    char text[180];
    wsprintfA(text, "FAIL API %u %s error=%lu", D9WG_CLIP_API, where,
              GetLastError());
    report(text);
    ExitProcess(1);
  }
}
#define OK(call) check(SUCCEEDED(call), #call)
struct Input {
  float x, y, z;
  DWORD color;
};
struct Scene {
  struct Input vertices[3];
  float min_z, max_z;
  BOOL perspective;
};
#ifdef D9WG_USER_CLIP_CAPTURE
#include "d3d_user_clip_scenes.h"
#else
static const struct Scene scenes[] = {
    {{{-.8f, -.7f, -.4f, 0xffff0000},
      {.8f, -.7f, .5f, 0xff00ff00},
      {0, .8f, .5f, 0xff0000ff}},
     .25f,
     .75f,
     FALSE},
    {{{-.8f, -.7f, 1.4f, 0xffff0000},
      {.8f, -.7f, .5f, 0xff00ff00},
      {0, .8f, .5f, 0xff0000ff}},
     .25f,
     .75f,
     FALSE},
    {{{-.8f, -.7f, -.4f, 0xffff0000},
      {.8f, -.7f, .5f, 0xff00ff00},
      {0, .8f, .5f, 0xff0000ff}},
     0,
     0,
     FALSE},
    {{{-.2f, -.2f, -.5f, 0xffff0000},
      {.8f, -.7f, 1, 0xff00ff00},
      {0, .8f, 1, 0xff0000ff}},
     .25f,
     .75f,
     TRUE},
    {{{-.2f, -.2f, 0, 0xffff0000},
      {.8f, -.7f, 1, 0xff00ff00},
      {0, .8f, 1, 0xff0000ff}},
     .25f,
     .75f,
     TRUE},
    {{{-200, -.7f, .5f, 0xffff0000},
      {.8f, -.7f, .5f, 0xff00ff00},
      {0, .8f, .5f, 0xff0000ff}},
     .25f,
     .75f,
     FALSE},
};
#endif
static void set_fvf(DWORD fvf) {
#if D9WG_CLIP_API == 9
  OK(CALL(SetFVF)(device, fvf));
#else
  OK(CALL(SetVertexShader)(device, fvf));
#endif
}
static void stream(Buffer *buffer, UINT offset, UINT stride) {
#if D9WG_CLIP_API == 9
  OK(CALL(SetStreamSource)(device, 0, buffer, offset, stride));
#else
  (void)offset;
  OK(CALL(SetStreamSource)(device, 0, buffer, stride));
#endif
}
static void process(Buffer *buffer) {
#if D9WG_CLIP_API == 9
  OK(CALL(ProcessVertices)(device, 1, 2, 3, buffer, NULL, 0));
#else
  OK(CALL(ProcessVertices)(device, 1, 2, 3, buffer, 0));
#endif
}
static void indices(Indices *buffer) {
#if D9WG_CLIP_API == 9
  OK(CALL(SetIndices)(device, buffer));
#else
  OK(CALL(SetIndices)(device, buffer, 1));
#endif
}
static void indexed_draw(void) {
#if D9WG_CLIP_API == 9
  OK(CALL(DrawIndexedPrimitive)(device, D3DPT_TRIANGLELIST, -1, 3, 3, 1, 1));
#else
  OK(CALL(DrawIndexedPrimitive)(device, D3DPT_TRIANGLELIST, 1, 3, 1, 1));
#endif
}
static void bindings(Buffer *expected, Indices *expected_indices) {
  Buffer *bound;
  UINT stride;
#if D9WG_CLIP_API == 9
  UINT offset;
  OK(CALL(GetStreamSource)(device, 0, &bound, &offset, &stride));
#else
  OK(CALL(GetStreamSource)(device, 0, &bound, &stride));
#endif
  check(bound == expected && stride == 20,
        "processed draw preserves stream binding");
  BUF(Release)(bound);
  if (expected_indices) {
    Indices *bound_indices;
#if D9WG_CLIP_API == 9
    OK(CALL(GetIndices)(device, &bound_indices));
#else
    UINT base;
    OK(CALL(GetIndices)(device, &bound_indices, &base));
    check(base == 1, "preserved index base");
#endif
    check(bound_indices == expected_indices,
          "processed draw preserves index binding");
    IB(Release)(bound_indices);
  }
}
void WINAPI WinMainCRTStartup(void) {
  char directory[MAX_PATH], path[MAX_PATH], text[160];
  GetModuleFileNameA(NULL, directory, sizeof(directory));
  unsigned at = lstrlenA(directory);
  while (at && directory[at - 1] != '\\')
    --at;
  directory[at] = 0;
  lstrcpyA(path, directory);
  lstrcatA(path, "CLIP.TXT");
  logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (logfile == INVALID_HANDLE_VALUE)
    ExitProcess(2);
  report("READY D3D8/9 processed clipping COM probe");
  lstrcpyA(path, directory);
  wsprintfA(text, "D3D%u.DLL", D9WG_CLIP_API);
  lstrcatA(path, text);
  HMODULE module = LoadLibraryA(path);
  check(module != NULL, "local DLL");
  wsprintfA(text, "Direct3DCreate%u", D9WG_CLIP_API);
  Factory *(WINAPI * create)(UINT) =
      (void *)(uintptr_t)GetProcAddress(module, text);
  check(create != NULL, "factory export");
  Factory *factory = create(D3D_SDK_VERSION);
  check(factory != NULL, "factory");
  HWND window =
      CreateWindowExA(0, "STATIC", "Processed clipping", WS_OVERLAPPEDWINDOW, 0,
                      0, 160, 160, NULL, NULL, GetModuleHandleA(NULL), NULL);
  D3DPRESENT_PARAMETERS pp;
  ZeroMemory(&pp, sizeof(pp));
  pp.BackBufferWidth = pp.BackBufferHeight = 128;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8;
  pp.BackBufferCount = 1;
  pp.hDeviceWindow = window;
  pp.Windowed = TRUE;
  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.EnableAutoDepthStencil = TRUE;
  pp.AutoDepthStencilFormat = D3DFMT_D16;
  OK(FACT(CreateDevice)(factory, 0, D3DDEVTYPE_HAL, window,
                        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device));
  Buffer *source, *destination;
#if D9WG_CLIP_API == 9
  OK(CALL(CreateVertexBuffer)(device, 128, D3DUSAGE_SOFTWAREPROCESSING,
                              D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_SYSTEMMEM,
                              &source, NULL));
  OK(CALL(CreateVertexBuffer)(device, 128, 0, D3DFVF_XYZRHW | D3DFVF_DIFFUSE,
                              D3DPOOL_SYSTEMMEM, &destination, NULL));
#else
  OK(CALL(CreateVertexBuffer)(device, 128, D3DUSAGE_SOFTWAREPROCESSING,
                              D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_SYSTEMMEM,
                              &source));
  OK(CALL(CreateVertexBuffer)(device, 128, 0, D3DFVF_XYZRHW | D3DFVF_DIFFUSE,
                              D3DPOOL_SYSTEMMEM, &destination));
#endif
  Indices *ib16, *ib32;
#if D9WG_CLIP_API == 9
  WORD index16[4] = {0xffff, 3, 4, 5};
  DWORD index32[4] = {0xffffffff, 3, 4, 5};
  OK(CALL(CreateIndexBuffer)(device, sizeof(index16), 0, D3DFMT_INDEX16,
                             D3DPOOL_SYSTEMMEM, &ib16, NULL));
  OK(CALL(CreateIndexBuffer)(device, sizeof(index32), 0, D3DFMT_INDEX32,
                             D3DPOOL_SYSTEMMEM, &ib32, NULL));
  Buffer *decl_buffer;
  IDirect3DVertexDeclaration9 *declaration;
  D3DVERTEXELEMENT9 elements[] = {
      {0, 0, D3DDECLTYPE_D3DCOLOR, 0, D3DDECLUSAGE_COLOR, 0},
      {0, 4, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITIONT, 0},
      {0, 20, D3DDECLTYPE_FLOAT1, 0, D3DDECLUSAGE_TEXCOORD, 0},
      D3DDECL_END()};
  OK(CALL(CreateVertexBuffer)(device, 160, 0, D3DFVF_XYZRHW | D3DFVF_DIFFUSE,
                              D3DPOOL_SYSTEMMEM, &decl_buffer, NULL));
  OK(CALL(CreateVertexDeclaration)(device, elements, &declaration));
#else
  WORD index16[4] = {0xffff, 1, 2, 3};
  DWORD index32[4] = {0xffffffff, 1, 2, 3};
  OK(CALL(CreateIndexBuffer)(device, sizeof(index16), 0, D3DFMT_INDEX16,
                             D3DPOOL_SYSTEMMEM, &ib16));
  OK(CALL(CreateIndexBuffer)(device, sizeof(index32), 0, D3DFMT_INDEX32,
                             D3DPOOL_SYSTEMMEM, &ib32));
#endif
  void *data;
  OK(IB(Lock)(ib16, 0, 0, (void *)&data, 0));
  CopyMemory(data, index16, sizeof(index16));
  OK(IB(Unlock)(ib16));
  OK(IB(Lock)(ib32, 0, 0, (void *)&data, 0));
  CopyMemory(data, index32, sizeof(index32));
  OK(IB(Unlock)(ib32));
  D3DMATRIX identity;
  ZeroMemory(&identity, sizeof(identity));
  identity._11 = identity._22 = identity._33 = identity._44 = 1;
  OK(CALL(SetTransform)(device, D3DTS_WORLD, &identity));
  OK(CALL(SetTransform)(device, D3DTS_VIEW, &identity));
#ifdef D9WG_USER_CLIP_CAPTURE
  D3DMATRIX world = identity, view = identity;
  world._41 = .125f;world._22 = .75f;view._41 = -.25f;view._42 = .125f;
  OK(CALL(SetTransform)(device,D3DTS_WORLD,&world));OK(CALL(SetTransform)(device,D3DTS_VIEW,&view));
#endif
  OK(CALL(SetRenderState)(device, D3DRS_LIGHTING, FALSE));
  OK(CALL(SetRenderState)(device, D3DRS_CULLMODE, D3DCULL_NONE));
  OK(CALL(SetRenderState)(device, D3DRS_ZENABLE, TRUE));
  for (unsigned scene = 0; scene < 6; ++scene) {
    const struct Scene *s = &scenes[scene];
#ifdef D9WG_USER_CLIP_CAPTURE
    OK(CALL(SetClipPlane)(device,scene,user_planes[scene]));
    OK(CALL(SetRenderState)(device,D3DRS_CLIPPLANEENABLE,1u<<scene));
#endif
    OK(BUF(Lock)(source, 0, 0, (void *)&data, 0));
    ZeroMemory(data, 128);
    CopyMemory((BYTE *)data + (D9WG_CLIP_API == 9 ? 32 : 16), s->vertices,
               sizeof(s->vertices));
    OK(BUF(Unlock)(source));
    OK(BUF(Lock)(destination, 0, 0, (void *)&data, 0));
    FillMemory(data, 128, 0xcc);
    OK(BUF(Unlock)(destination));
    D3DMATRIX projection = identity;
    if (s->perspective) {
      projection._33 = .5f;
      projection._43 = .1f;
      projection._34 = 1;
      projection._44 = 0;
    }
    OK(CALL(SetTransform)(device, D3DTS_PROJECTION, &projection));
    Viewport vp = {7, 11, 100, 80, s->min_z, s->max_z},
             full = {0, 0, 128, 128, 0, 1};
    OK(CALL(SetViewport)(device, &vp));
    OK(CALL(SetRenderState)(device, D3DRS_CLIPPING, TRUE));
    set_fvf(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    stream(source, 16, 16);
    process(destination);
    OK(BUF(Lock)(destination, 0, 0, (void *)&data, D3DLOCK_READONLY));
    for (unsigned i = 0; i < 40; ++i)
      check(((BYTE *)data)[i] == 0xcc, "destination prefix unchanged");
    for (unsigned vertex = 0; vertex < 3; ++vertex) {
      DWORD *v = (DWORD *)data + 10 + 5 * vertex;
      wsprintfA(text, "VERTEX scene=%u v=%u words=%08lx,%08lx,%08lx,%08lx",
                scene, vertex, v[0], v[1], v[2], v[3]);
      report(text);
    }
    OK(BUF(Unlock)(destination));
    for (unsigned mode = 0; mode < 6; ++mode) {
      OK(CALL(SetViewport)(device, &full));
      OK(CALL(Clear)(device, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                     0xff102030, 1, 0));
      OK(CALL(SetViewport)(device, &vp));
      OK(CALL(BeginScene)(device));
      if (!mode) {
        set_fvf(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        OK(CALL(DrawPrimitiveUP)(device, D3DPT_TRIANGLELIST, 1, s->vertices,
                                 16));
      } else if (mode == 1) {
        set_fvf(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        stream(destination, 0, 20);
        OK(CALL(SetRenderState)(device, D3DRS_CLIPPING, FALSE));
        OK(CALL(DrawPrimitive)(device, D3DPT_TRIANGLELIST, 2, 1));
        bindings(destination, NULL);
      } else if (mode == 2) {
        indices(ib16);
        indexed_draw();
        bindings(destination, ib16);
      } else if (mode == 3) {
#ifdef D9WG_USER_CLIP_CAPTURE
        set_fvf(D3DFVF_XYZ|D3DFVF_DIFFUSE);
        OK(CALL(DrawPrimitiveUP)(device,D3DPT_TRIANGLELIST,1,s->vertices,16));
#else
        indexed_draw();bindings(destination,ib16);
#endif
      }
#if D9WG_CLIP_API == 9
      else if (mode == 4) {
#ifdef D9WG_USER_CLIP_CAPTURE
        set_fvf(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);stream(destination,0,20);
#endif
        stream(destination, 20, 20);
        OK(CALL(DrawPrimitive)(device, D3DPT_TRIANGLELIST, 1, 1));
        bindings(destination, ib16);
      } else {
        OK(CALL(SetRenderState)(device, D3DRS_CLIPPING, TRUE));
        set_fvf(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        stream(source, 16, 16);
        OK(CALL(ProcessVertices)(device, 1, 2, 3, decl_buffer, declaration, 0));
        OK(CALL(SetVertexDeclaration)(device, declaration));
        stream(decl_buffer, 0, 24);
        OK(CALL(DrawPrimitive)(device, D3DPT_TRIANGLELIST, 2, 1));
      }
#else
      else if (mode == 4) {
#ifdef D9WG_USER_CLIP_CAPTURE
        set_fvf(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);stream(destination,0,20);
#endif
        indices(ib32);
        indexed_draw();
        bindings(destination, ib32);
      } else {
        indexed_draw();
        bindings(destination, ib32);
      }
#endif
      OK(CALL(EndScene)(device));
      OK(CALL(Present)(device, NULL, NULL, NULL, NULL));
      wsprintfA(text, "IMAGE scene=%u processed=%u bytes=65536", scene, mode);
      report(text);
    }
  }
#if D9WG_CLIP_API == 9
  IDirect3DVertexDeclaration9_Release(declaration);
  BUF(Release)(decl_buffer);
#endif
  IB(Release)(ib16);
  IB(Release)(ib32);
  BUF(Release)(source);
  BUF(Release)(destination);
  CALL(Release)(device);
  FACT(Release)(factory);
  DestroyWindow(window);
  report("PASS D3D8/9 processed clipping COM probe");
  CloseHandle(logfile);
  ExitProcess(0);
}
