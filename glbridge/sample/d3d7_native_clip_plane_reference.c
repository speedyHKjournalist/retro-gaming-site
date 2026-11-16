/* Microsoft system DDraw7 RGB reference, never loads the app-local proxy.
 * Records public ProcessVertices words and independently draws XYZRHW depth.
 * The XP runner writes only to its RAM-backed tools volume. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define DIRECTDRAW_VERSION 0x0700
#define DIRECT3D_VERSION 0x0700
#include <d3d.h>
#include <ddraw.h>
#include <windows.h>
static HANDLE logfile;
static void line(const char *s) {
  DWORD n;
  WriteFile(logfile, s, lstrlenA(s), &n, 0);
  WriteFile(logfile, "\r\n", 2, &n, 0);
  FlushFileBuffers(logfile);
}
static void status(const char *s, HRESULT hr) {
  char t[160];
  wsprintfA(t, "%s hr=%08lx", s, hr);
  line(t);
}
#define TRY(x)                                                                 \
  do {                                                                         \
    HRESULT hr = (x);                                                          \
    status(#x, hr);                                                            \
    if (FAILED(hr))                                                            \
      goto done;                                                               \
  } while (0)
void WINAPI WinMainCRTStartup(void) {
  char p[MAX_PATH], t[200];
  GetModuleFileNameA(0, p, sizeof(p));
  int at = lstrlenA(p);
  while (at && p[at - 1] != '\\')
    --at;
  p[at] = 0;
  lstrcatA(p, "CLIP.TXT");
  logfile =
      CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
  if (logfile == INVALID_HANDLE_VALUE)
    ExitProcess(2);
  line("READY native Microsoft DDraw7 user-plane reference");
  GetSystemDirectoryA(p, sizeof(p));
  lstrcatA(p, "\\ddraw.dll");
  HMODULE lib = LoadLibraryA(p);
  if (!lib) {
    status("load native ddraw", HRESULT_FROM_WIN32(GetLastError()));
    goto done;
  }
  HRESULT(WINAPI * create)(GUID *, void **, REFIID, IUnknown *) =
      (void *)GetProcAddress(lib, "DirectDrawCreateEx");
  if (!create) {
    status("native DirectDrawCreateEx export",
           HRESULT_FROM_WIN32(GetLastError()));
    goto done;
  }
  IDirectDraw7 *dd = 0;
  IDirect3D7 *d3d = 0;
  IDirectDrawSurface7 *target = 0, *depth = 0;
  IDirect3DDevice7 *dev = 0;
  IDirect3DVertexBuffer7 *src = 0, *dst = 0;
  HWND window =
      CreateWindowExA(0, "STATIC", "Clip reference", WS_OVERLAPPEDWINDOW, 0, 0,
                      128, 128, 0, 0, GetModuleHandleA(0), 0);
  TRY(create(0, (void **)&dd, &IID_IDirectDraw7, 0));
  TRY(IDirectDraw7_SetCooperativeLevel(dd, window, DDSCL_NORMAL));
  TRY(IDirectDraw7_QueryInterface(dd, &IID_IDirect3D7, (void **)&d3d));
  DDSURFACEDESC2 desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.dwSize = sizeof(desc);
  desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
  desc.dwWidth = desc.dwHeight = 128;
  desc.ddsCaps.dwCaps =
      DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE | DDSCAPS_SYSTEMMEMORY;
  desc.ddpfPixelFormat.dwSize = sizeof(desc.ddpfPixelFormat);
  desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
  desc.ddpfPixelFormat.dwRGBBitCount = 32;
  desc.ddpfPixelFormat.dwRBitMask = 0xff0000;
  desc.ddpfPixelFormat.dwGBitMask = 0xff00;
  desc.ddpfPixelFormat.dwBBitMask = 0xff;
  TRY(IDirectDraw7_CreateSurface(dd, &desc, &target, 0));
  DDSURFACEDESC2 zd;
  ZeroMemory(&zd, sizeof(zd));
  zd.dwSize = sizeof(zd);
  zd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
  zd.dwWidth = zd.dwHeight = 128;
  zd.ddsCaps.dwCaps = DDSCAPS_ZBUFFER | DDSCAPS_SYSTEMMEMORY;
  zd.ddpfPixelFormat.dwSize = sizeof(zd.ddpfPixelFormat);
  zd.ddpfPixelFormat.dwFlags = DDPF_ZBUFFER;
  zd.ddpfPixelFormat.dwZBufferBitDepth = 16;
  zd.ddpfPixelFormat.dwZBitMask = 0xffff;
  TRY(IDirectDraw7_CreateSurface(dd, &zd, &depth, 0));
  TRY(IDirectDrawSurface7_AddAttachedSurface(target, depth));
  TRY(IDirect3D7_CreateDevice(d3d, &IID_IDirect3DRGBDevice, target, &dev));
  D3DMATRIX ident;
  ZeroMemory(&ident, sizeof(ident));
  ident._11 = ident._22 = ident._33 = ident._44 = 1;
  TRY(IDirect3DDevice7_SetTransform(dev, D3DTRANSFORMSTATE_WORLD, &ident));
  TRY(IDirect3DDevice7_SetTransform(dev, D3DTRANSFORMSTATE_VIEW, &ident));
  TRY(IDirect3DDevice7_SetTransform(dev, D3DTRANSFORMSTATE_PROJECTION, &ident));
  D3DVIEWPORT7 vp = {7, 11, 100, 80, .25f, .75f};
  TRY(IDirect3DDevice7_SetViewport(dev, &vp));
  TRY(IDirect3DDevice7_SetRenderState(dev, D3DRENDERSTATE_LIGHTING, FALSE));
  D3DDEVICEDESC7 caps;
  TRY(IDirect3DDevice7_GetCaps(dev, &caps));
  DWORD gb[4];
  CopyMemory(gb, &caps.dvGuardBandLeft, 16);
  wsprintfA(t, "GUARD words=%08lx,%08lx,%08lx,%08lx", gb[0], gb[1], gb[2],
            gb[3]);
  line(t);
  static const float input[][3] = {{-.5f, -.5f, .25f},
                                   {0, 0, .5f},
                                   {.5f, .5f, .75f},
                                   {-.25f, .25f, .5f},
                                   {.25f, -.25f, .5f}};
  static const float planes[6][4] = {{1, 0, 0, 0},     {0, 1, 0, 0},
                                     {0, 0, 1, -.5f},  {-1, 0, 0, .25f},
                                     {0, -1, 0, .25f}, {0, 0, -1, .5f}};
  D3DVERTEXBUFFERDESC vb = {sizeof(vb), D3DVBCAPS_SYSTEMMEMORY, D3DFVF_XYZ, 5};
  TRY(IDirect3D7_CreateVertexBuffer(d3d, &vb, &src, 0));
  vb.dwFVF = D3DFVF_XYZRHW;
  TRY(IDirect3D7_CreateVertexBuffer(d3d, &vb, &dst, 0));
  void *data;
  TRY(IDirect3DVertexBuffer7_Lock(src, 0, &data, 0));
  CopyMemory(data, input, sizeof(input));
  TRY(IDirect3DVertexBuffer7_Unlock(src));
  for (unsigned transform = 0; transform < 3; ++transform) {
    D3DMATRIX world = ident, view = ident, projection = ident;
    if (transform) {
      world._41 = .25f;
      world._22 = 2;
    }
    if (transform == 2) {
      view._41 = -.25f;
      view._33 = 2;
      projection._44 = 2;
    }
    TRY(IDirect3DDevice7_SetTransform(dev, D3DTRANSFORMSTATE_WORLD, &world));
    TRY(IDirect3DDevice7_SetTransform(dev, D3DTRANSFORMSTATE_VIEW, &view));
    TRY(IDirect3DDevice7_SetTransform(dev, D3DTRANSFORMSTATE_PROJECTION,
                                      &projection));
    for (unsigned plane = 0; plane < 6; ++plane) {
      TRY(IDirect3DDevice7_SetClipPlane(dev, plane, (float *)planes[plane]));
      for (unsigned mode = 0; mode < 4; ++mode) {
        TRY(IDirect3DDevice7_SetRenderState(dev, D3DRENDERSTATE_CLIPPLANEENABLE,
                                            mode ? 1u << plane : 0));
        TRY(IDirect3DDevice7_SetRenderState(dev, D3DRENDERSTATE_CLIPPING,
                                            mode != 2));
        TRY(IDirect3DVertexBuffer7_ProcessVertices(
            dst, D3DVOP_TRANSFORM | (mode == 3 ? 0 : D3DVOP_CLIP), 0, 5, src, 0,
            dev, 0));
        TRY(IDirect3DVertexBuffer7_Lock(dst, DDLOCK_READONLY, &data, 0));
        for (unsigned i = 0; i < 5; ++i) {
          DWORD *v = (DWORD *)data + 4 * i;
          wsprintfA(t,
                    "PLANE transform=%u plane=%u mode=%u vertex=%u "
                    "words=%08lx,%08lx,%08lx,%08lx",
                    transform, plane, mode, i, v[0], v[1], v[2], v[3]);
          line(t);
        }
        TRY(IDirect3DVertexBuffer7_Unlock(dst));
      }
    }
  }
  IDirect3DVertexBuffer7_Release(src);
  IDirect3DVertexBuffer7_Release(dst);
  IDirect3DDevice7_Release(dev);
  IDirectDrawSurface7_Release(depth);
  IDirectDrawSurface7_Release(target);
  IDirect3D7_Release(d3d);
  IDirectDraw7_Release(dd);
  DestroyWindow(window);
  line("PASS native user-plane reference available");
done:
  line("PASS native user-plane reference finished");
  CloseHandle(logfile);
  ExitProcess(0);
}
