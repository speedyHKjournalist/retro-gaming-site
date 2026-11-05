#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>
#include <stdint.h>
#include "../d3d9proxy/d3d9_core.h"
static HANDLE log_file;
static UINT checks, images, visited[97];
static D3DGAMMARAMP gamma_ramp, gamma_copy;
static PALETTEENTRY palette[256], palette_copy[256];
static void report(const char *message) {
    DWORD written; WriteFile(log_file, message, lstrlenA(message), &written, NULL);
    WriteFile(log_file, "\r\n", 2, &written, NULL); FlushFileBuffers(log_file);
}
static void check(BOOL value, const char *message, UINT line) {
    ++checks;
    if (!value) { char text[300]; wsprintfA(text, "FAIL line=%u %s", line, message); report(text); CloseHandle(log_file); ExitProcess(1); }
}
#define CHECK(x) check((x), #x, __LINE__)
#define HR(x) CHECK(SUCCEEDED(x))
enum { slot_QueryInterface, slot_AddRef, slot_Release, slot_TestCooperativeLevel, slot_GetAvailableTextureMem, slot_ResourceManagerDiscardBytes, slot_GetDirect3D, slot_GetDeviceCaps, slot_GetDisplayMode, slot_GetCreationParameters, slot_SetCursorProperties, slot_SetCursorPosition, slot_ShowCursor, slot_CreateAdditionalSwapChain, slot_Reset, slot_Present, slot_GetBackBuffer, slot_GetRasterStatus, slot_SetGammaRamp, slot_GetGammaRamp, slot_CreateTexture, slot_CreateVolumeTexture, slot_CreateCubeTexture, slot_CreateVertexBuffer, slot_CreateIndexBuffer, slot_CreateRenderTarget, slot_CreateDepthStencilSurface, slot_CreateImageSurface, slot_CopyRects, slot_UpdateTexture, slot_GetFrontBuffer, slot_SetRenderTarget, slot_GetRenderTarget, slot_GetDepthStencilSurface, slot_BeginScene, slot_EndScene, slot_Clear, slot_SetTransform, slot_GetTransform, slot_MultiplyTransform, slot_SetViewport, slot_GetViewport, slot_SetMaterial, slot_GetMaterial, slot_SetLight, slot_GetLight, slot_LightEnable, slot_GetLightEnable, slot_SetClipPlane, slot_GetClipPlane, slot_SetRenderState, slot_GetRenderState, slot_BeginStateBlock, slot_EndStateBlock, slot_ApplyStateBlock, slot_CaptureStateBlock, slot_DeleteStateBlock, slot_CreateStateBlock, slot_SetClipStatus, slot_GetClipStatus, slot_GetTexture, slot_SetTexture, slot_GetTextureStageState, slot_SetTextureStageState, slot_ValidateDevice, slot_GetInfo, slot_SetPaletteEntries, slot_GetPaletteEntries, slot_SetCurrentTexturePalette, slot_GetCurrentTexturePalette, slot_DrawPrimitive, slot_DrawIndexedPrimitive, slot_DrawPrimitiveUP, slot_DrawIndexedPrimitiveUP, slot_ProcessVertices, slot_CreateVertexShader, slot_SetVertexShader, slot_GetVertexShader, slot_DeleteVertexShader, slot_SetVertexShaderConstant, slot_GetVertexShaderConstant, slot_GetVertexShaderDeclaration, slot_GetVertexShaderFunction, slot_SetStreamSource, slot_GetStreamSource, slot_SetIndices, slot_GetIndices, slot_CreatePixelShader, slot_SetPixelShader, slot_GetPixelShader, slot_DeletePixelShader, slot_SetPixelShaderConstant, slot_GetPixelShaderConstant, slot_GetPixelShaderFunction, slot_DrawRectPatch, slot_DrawTriPatch, slot_DeletePatch };
#define CALL(method, ...) (visited[slot_##method]++, d->lpVtbl->method(d, ##__VA_ARGS__))
static BOOL equal(const void *a, const void *b, UINT size) {
    for (UINT i = 0; i < size; ++i) if (((const BYTE *)a)[i] != ((const BYTE *)b)[i]) return FALSE;
    return TRUE;
}
static void read_image(IDirect3DDevice8 *d, DWORD color) {
    IDirect3DSurface8 *back = NULL, *cpu = NULL;
    HR(CALL(GetRenderTarget, &back));
    D3DSURFACE_DESC desc; HR(IDirect3DSurface8_GetDesc(back, &desc));
    HR(CALL(CreateImageSurface, desc.Width, desc.Height, D3DFMT_A8R8G8B8, &cpu));
    HR(CALL(CopyRects, back, NULL, 0, cpu, NULL));
    D3DLOCKED_RECT lock; HR(IDirect3DSurface8_LockRect(cpu, &lock, NULL, D3DLOCK_READONLY));
    for (UINT y = 0; y < desc.Height; ++y) for (UINT x = 0; x < desc.Width; ++x)
        CHECK(((DWORD *)((BYTE *)lock.pBits + y * lock.Pitch))[x] == color);
    HR(IDirect3DSurface8_UnlockRect(cpu));
    IDirect3DSurface8_Release(cpu); IDirect3DSurface8_Release(back); ++images;
}
static void draw_shader(IDirect3DDevice8 *d, DWORD color) {
    const float vertices[3][4] = {{-1,-1,0,1},{-1,3,0,1},{3,-1,0,1}};
    HR(CALL(SetRenderState, D3DRS_ZENABLE, FALSE)); HR(CALL(SetRenderState, D3DRS_LIGHTING, FALSE));
    HR(CALL(SetRenderState, D3DRS_CULLMODE, D3DCULL_NONE));
    HR(CALL(SetTextureStageState, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1));
    HR(CALL(SetTextureStageState, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE));
    HR(CALL(SetTextureStageState, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1));
    HR(CALL(SetTextureStageState, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE));
    HR(CALL(Clear, 0, NULL, D3DCLEAR_TARGET, 0xff000000u, 1.0f, 0));
    HR(CALL(BeginScene)); HR(CALL(DrawPrimitiveUP, D3DPT_TRIANGLELIST, 1, vertices, 16)); HR(CALL(EndScene));
    read_image(d, color);
}
static void exercise(IDirect3D8 *root, struct IDirect3D9 *backend_root,
        HRESULT (WINAPI *create)(IDirect3D8 *, struct IDirect3D9 *, UINT, D3DDEVTYPE,
            HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice8 **), HWND window, BOOL pure)
{
    D3DPRESENT_PARAMETERS pp; ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = pp.BackBufferHeight = 8; pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 0; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = TRUE;
    pp.hDeviceWindow = window; pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D16;
    IDirect3DDevice8 *d = NULL;
    HR(create(root, backend_root, 0, D3DDEVTYPE_HAL, window,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING | (pure ? D3DCREATE_PUREDEVICE : 0), &pp, &d));
    CHECK(d != NULL && pp.BackBufferCount == 1);
    IUnknown *identity = NULL; HR(CALL(QueryInterface, &IID_IUnknown, (void **)&identity));
    CHECK((void *)identity == d); IUnknown_Release(identity);
    CHECK(CALL(AddRef) == 2 && CALL(Release) == 1);
    HR(CALL(TestCooperativeLevel)); CHECK(CALL(GetAvailableTextureMem) == 128u * 1024u * 1024u);
    HR(CALL(ResourceManagerDiscardBytes, 0));
    IDirect3D8 *parent = NULL; HR(CALL(GetDirect3D, &parent)); CHECK(parent == root); IDirect3D8_Release(parent);
    D3DCAPS8 caps, root_caps; HR(CALL(GetDeviceCaps, &caps)); HR(IDirect3D8_GetDeviceCaps(root,0,D3DDEVTYPE_HAL,&root_caps)); CHECK(equal(&caps,&root_caps,sizeof(caps)) && caps.VertexShaderVersion == 0xfffe0101u);
    D3DDISPLAYMODE mode, desktop; HR(CALL(GetDisplayMode, &mode)); HR(IDirect3D8_GetAdapterDisplayMode(root,0,&desktop)); CHECK(equal(&mode,&desktop,sizeof(mode)));
    D3DDEVICE_CREATION_PARAMETERS creation; HR(CALL(GetCreationParameters, &creation)); CHECK(creation.hFocusWindow == window);
    CALL(SetCursorPosition, 0, 0, 0); CALL(ShowCursor, FALSE);
    D3DRASTER_STATUS raster; HR(CALL(GetRasterStatus, &raster));
    for (UINT i = 0; i < 256; ++i) gamma_ramp.red[i] = gamma_ramp.green[i] = gamma_ramp.blue[i] = i * 257;
    CALL(SetGammaRamp, 0, &gamma_ramp); CALL(GetGammaRamp, &gamma_copy); CHECK(equal(&gamma_ramp, &gamma_copy, sizeof(gamma_ramp)));
    D3DMATRIX matrix, matrix_copy; ZeroMemory(&matrix, sizeof(matrix));
    matrix._11 = matrix._22 = matrix._33 = matrix._44 = 1.0f;
    HR(CALL(SetTransform, D3DTS_WORLD, &matrix)); HR(CALL(MultiplyTransform, D3DTS_WORLD, &matrix));
    HR(CALL(GetTransform, D3DTS_WORLD, &matrix_copy)); CHECK(equal(&matrix, &matrix_copy, sizeof(matrix)));
    D3DVIEWPORT8 viewport = {0,0,8,8,0,1}, viewport_copy;
    HR(CALL(SetViewport, &viewport)); HR(CALL(GetViewport, &viewport_copy)); CHECK(equal(&viewport, &viewport_copy, sizeof(viewport)));
    D3DMATERIAL8 material, material_copy; ZeroMemory(&material, sizeof(material)); material.Diffuse.r = material.Diffuse.a = 1;
    HR(CALL(SetMaterial, &material)); HR(CALL(GetMaterial, &material_copy)); CHECK(equal(&material, &material_copy, sizeof(material)));
    D3DLIGHT8 light, light_copy; ZeroMemory(&light, sizeof(light)); light.Type = D3DLIGHT_DIRECTIONAL; light.Direction.z = 1;
    HR(CALL(SetLight, 37, &light)); HR(CALL(GetLight, 37, &light_copy)); CHECK(equal(&light, &light_copy, sizeof(light)));
    BOOL enabled; HR(CALL(LightEnable, 37, TRUE)); HR(CALL(GetLightEnable, 37, &enabled)); CHECK(enabled);
    float plane[4] = {1,0,0,0}, plane_copy[4]; HR(CALL(SetClipPlane, 0, plane)); HR(CALL(GetClipPlane, 0, plane_copy)); CHECK(equal(plane, plane_copy, 16));
    D3DCLIPSTATUS8 clip = {3,1}, clip_copy; HR(CALL(SetClipStatus, &clip)); HR(CALL(GetClipStatus, &clip_copy)); CHECK(equal(&clip, &clip_copy, sizeof(clip)));
    DWORD value; HR(CALL(SetRenderState, D3DRS_POSITIONORDER, 42)); HR(CALL(GetRenderState, D3DRS_POSITIONORDER, &value)); CHECK(value == 42);
    HR(CALL(SetTextureStageState, 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP)); HR(CALL(GetTextureStageState, 0, D3DTSS_ADDRESSU, &value)); CHECK(value == D3DTADDRESS_CLAMP);
    CHECK(CALL(SetTextureStageState, 0, (D3DTEXTURESTAGESTATETYPE)32, 0) == D3DERR_INVALIDCALL);
    DWORD passes; HR(CALL(ValidateDevice, &passes)); CHECK(passes > 0);
    CHECK(CALL(GetInfo, 0, NULL, 0) == E_FAIL);
    for (UINT i = 0; i < 256; ++i) { palette[i].peRed = i; palette[i].peGreen = 255-i; palette[i].peBlue = 7; palette[i].peFlags = 255; }
    HR(CALL(SetPaletteEntries, 2, palette)); HR(CALL(GetPaletteEntries, 2, palette_copy)); CHECK(equal(palette, palette_copy, sizeof(palette)));
    UINT selected; HR(CALL(SetCurrentTexturePalette, 2)); HR(CALL(GetCurrentTexturePalette, &selected)); CHECK(selected == 2);
    DWORD bits[4] = {0x80000000u,0x7fc01234u,0x3f800000u,0}, bits_copy[4];
    HR(CALL(SetVertexShaderConstant, 95, bits, 1)); HR(CALL(GetVertexShaderConstant, 95, bits_copy, 1)); CHECK(equal(bits, bits_copy, 16));
    HR(CALL(SetPixelShaderConstant, 7, bits, 1)); HR(CALL(GetPixelShaderConstant, 7, bits_copy, 1)); CHECK(equal(bits, bits_copy, 16));
    HR(CALL(SetVertexShaderConstant, 0xffffffffu, NULL, 0)); CHECK(CALL(SetPixelShaderConstant, 8, bits, 1) == D3DERR_INVALIDCALL);
    CHECK(CALL(DrawRectPatch, 0, NULL, NULL) == D3DERR_INVALIDCALL);
    CHECK(CALL(DrawTriPatch, 0, NULL, NULL) == D3DERR_INVALIDCALL); HR(CALL(DeletePatch, 99));

    IDirect3DTexture8 *texture = NULL, *system_texture = NULL;
    IDirect3DCubeTexture8 *cube = NULL; IDirect3DVolumeTexture8 *volume = NULL;
    HR(CALL(CreateTexture, 4,4,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture));
    HR(CALL(CreateTexture, 4,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&system_texture));
    D3DLOCKED_RECT lock; HR(IDirect3DTexture8_LockRect(system_texture,0,&lock,NULL,0));
    for (UINT y=0;y<4;++y) for (UINT x=0;x<4;++x) ((DWORD *)((BYTE *)lock.pBits+y*lock.Pitch))[x]=0xff336699u;
    HR(IDirect3DTexture8_UnlockRect(system_texture,0)); HR(CALL(UpdateTexture, (IDirect3DBaseTexture8 *)system_texture,(IDirect3DBaseTexture8 *)texture));
    HR(CALL(CreateCubeTexture,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&cube));
    HR(CALL(CreateVolumeTexture,4,4,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&volume));
    HR(CALL(SetTexture,0,(IDirect3DBaseTexture8 *)cube));
    IDirect3DBaseTexture8 *bound = NULL; HR(CALL(GetTexture,0,&bound)); CHECK(bound==(IDirect3DBaseTexture8 *)cube); IDirect3DBaseTexture8_Release(bound);
    HR(CALL(SetTexture,0,(IDirect3DBaseTexture8 *)volume)); HR(CALL(GetTexture,0,&bound)); CHECK(bound==(IDirect3DBaseTexture8 *)volume); IDirect3DBaseTexture8_Release(bound);
    HR(CALL(SetTexture,0,NULL));
    IDirect3DSurface8 *back=NULL,*depth=NULL,*target=NULL,*small_depth=NULL,*level=NULL,*got=NULL,*image=NULL;
    HR(CALL(GetBackBuffer,0,D3DBACKBUFFER_TYPE_MONO,&back)); HR(CALL(GetDepthStencilSurface,&depth));
    HR(CALL(CreateRenderTarget,4,4,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,FALSE,&target));
    HR(CALL(CreateDepthStencilSurface,4,4,D3DFMT_D16,D3DMULTISAMPLE_NONE,&small_depth));
    HR(IDirect3DTexture8_GetSurfaceLevel(texture,0,&level)); HR(CALL(SetRenderTarget,level,small_depth));
    HR(CALL(GetRenderTarget,&got)); CHECK(got==level); IDirect3DSurface8_Release(got);
    void *container=NULL; HR(IDirect3DSurface8_GetContainer(level,&IID_IDirect3DTexture8,&container)); CHECK(container==texture); IDirect3DTexture8_Release((IDirect3DTexture8 *)container);
    HR(CALL(Clear,0,NULL,D3DCLEAR_TARGET,0xff336699u,1.0f,0)); read_image(d,0xff336699u);
    HR(CALL(SetRenderTarget,target,small_depth)); HR(CALL(Clear,0,NULL,D3DCLEAR_TARGET,0xff995522u,1.0f,0)); read_image(d,0xff995522u);
    HR(CALL(SetRenderTarget,back,depth));
    HR(CALL(CreateImageSurface,8,8,D3DFMT_A8R8G8B8,&image));
    CHECK(CALL(SetCursorProperties,0,0,NULL)==D3DERR_INVALIDCALL);
    CHECK(CALL(GetFrontBuffer,NULL)==D3DERR_INVALIDCALL);
    IDirect3DSwapChain8 *chain=NULL; HR(CALL(CreateAdditionalSwapChain,&pp,&chain));
    IDirect3DSurface8 *chainback=NULL; HR(IDirect3DSwapChain8_GetBackBuffer(chain,0,D3DBACKBUFFER_TYPE_MONO,&chainback));
    IDirect3DDevice8 *owner=NULL; HR(IDirect3DSurface8_GetDevice(chainback,&owner)); CHECK(owner==d); IDirect3DDevice8_Release(owner);
    IDirect3DSurface8_Release(chainback); IDirect3DSwapChain8_Release(chain);
    IDirect3DSurface8_Release(image); IDirect3DSurface8_Release(level); IDirect3DSurface8_Release(target); IDirect3DSurface8_Release(small_depth);
    IDirect3DTexture8_Release(system_texture); IDirect3DTexture8_Release(texture); IDirect3DCubeTexture8_Release(cube); IDirect3DVolumeTexture8_Release(volume);
    IDirect3DSurface8_Release(back); IDirect3DSurface8_Release(depth);

    IDirect3DVertexBuffer8 *vb=NULL,*vb_out=NULL,*processed=NULL;
    IDirect3DIndexBuffer8 *ib=NULL,*ib_out=NULL;
    HR(CALL(CreateVertexBuffer,48,0,D3DFVF_XYZ|D3DFVF_DIFFUSE,D3DPOOL_MANAGED,&vb));
    HR(CALL(CreateVertexBuffer,60,0,D3DFVF_XYZRHW|D3DFVF_DIFFUSE,D3DPOOL_MANAGED,&processed));
    HR(CALL(CreateIndexBuffer,6,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib));
    BYTE *data=NULL; HR(IDirect3DVertexBuffer8_Lock(vb,0,0,&data,0));
    const DWORD vertices[12]={0,0,0,0xffff0000u,0x3f800000u,0,0,0xffff0000u,0,0x3f800000u,0,0xffff0000u};
    CopyMemory(data,vertices,sizeof(vertices)); HR(IDirect3DVertexBuffer8_Unlock(vb));
    HR(IDirect3DIndexBuffer8_Lock(ib,0,0,&data,0)); ((WORD*)data)[0]=0;((WORD*)data)[1]=1;((WORD*)data)[2]=2; HR(IDirect3DIndexBuffer8_Unlock(ib));
    HR(CALL(SetStreamSource,0,vb,16)); HR(CALL(SetIndices,ib,7));
    UINT stride=0,base=0; HR(CALL(GetStreamSource,0,&vb_out,&stride)); CHECK(vb_out==vb&&stride==16); IDirect3DVertexBuffer8_Release(vb_out);
    HR(CALL(GetIndices,&ib_out,&base)); CHECK(ib_out==ib&&base==7); IDirect3DIndexBuffer8_Release(ib_out);
    HR(CALL(SetVertexShader,D3DFVF_XYZ|D3DFVF_DIFFUSE)); HR(CALL(SetIndices,ib,0));
    HR(CALL(SetRenderState,D3DRS_LIGHTING,FALSE)); HR(CALL(SetRenderState,D3DRS_ZENABLE,FALSE));
    HR(CALL(ProcessVertices,0,0,3,processed,1 /* D3DPV_DONOTCOPYDATA, absent from this SDK8 header */));
    HR(CALL(BeginScene)); HR(CALL(DrawPrimitive,D3DPT_TRIANGLELIST,0,1));
    HR(CALL(DrawIndexedPrimitive,D3DPT_TRIANGLELIST,0,3,0,1));
    const WORD indices[3]={0,1,2};
    HR(CALL(DrawIndexedPrimitiveUP,D3DPT_TRIANGLELIST,0,3,1,indices,D3DFMT_INDEX16,vertices,16)); HR(CALL(EndScene));
    HR(CALL(SetStreamSource,0,NULL,0)); HR(CALL(SetIndices,NULL,0));
    IDirect3DVertexBuffer8_Release(vb); IDirect3DVertexBuffer8_Release(processed); IDirect3DIndexBuffer8_Release(ib);

    const DWORD declaration[]={0x20000000u,0x40030000u,0x82000002u,0x3f800000u,0,0,0x3f800000u,0x82000008u,0,0x3f800000u,0,0x3f800000u,0xffffffffu};
    const DWORD red[]={0xfffe0100u,1,0xc00f0000u,0x90e40000u,1,0xd00f0000u,0xa0e40002u,0xffffu};
    const DWORD green[]={0xfffe0101u,1,0xc00f0000u,0x90e40000u,1,0xd00f0000u,0xa0e40008u,0xffffu};
    const DWORD pixel[]={0xffff0101u,1,0x800f0000u,0x90e40000u,0xffffu};
    DWORD vs_red=0,vs_green=0,ps=0,bytes=0,query[32];
    HR(CALL(CreateVertexShader,declaration,red,&vs_red,0)); HR(CALL(CreateVertexShader,declaration,green,&vs_green,0));
    HR(CALL(CreatePixelShader,pixel,&ps));
    HR(CALL(GetVertexShaderDeclaration,vs_red,NULL,&bytes)); CHECK(bytes==sizeof(declaration));
    HR(CALL(GetVertexShaderDeclaration,vs_red,query,&bytes)); CHECK(equal(query,declaration,bytes));
    bytes=sizeof(query); HR(CALL(GetVertexShaderFunction,vs_red,query,&bytes)); CHECK(bytes==sizeof(red)&&equal(query,red,bytes));
    bytes=sizeof(query); HR(CALL(GetPixelShaderFunction,ps,query,&bytes)); CHECK(bytes==sizeof(pixel)&&equal(query,pixel,bytes));
    HR(CALL(SetVertexShader,vs_red)); HR(CALL(SetPixelShader,ps));
    HR(CALL(GetVertexShader,&value)); CHECK(value==vs_red); HR(CALL(GetPixelShader,&value)); CHECK(value==ps);
    draw_shader(d,0xffff0000u);
    DWORD block=0; HR(CALL(BeginStateBlock));
    CHECK(CALL(BeginStateBlock)==D3DERR_INVALIDCALL);
    HR(CALL(SetVertexShader,vs_green)); HR(CALL(SetRenderState,D3DRS_POSITIONORDER,79));
    HR(CALL(SetRenderState,D3DRS_ZBIAS,111)); HR(CALL(SetTextureStageState,0,D3DTSS_ADDRESSU,D3DTADDRESS_MIRROR));
    HR(CALL(SetIndices,NULL,0xfffffffdu));
    HR(CALL(GetVertexShader,&value)); CHECK(value==vs_red);
    CHECK(CALL(EndStateBlock,NULL)==D3DERR_INVALIDCALL); HR(CALL(EndStateBlock,&block));
    draw_shader(d,0xffff0000u); HR(CALL(ApplyStateBlock,block)); draw_shader(d,0xff00ff00u);
    HR(CALL(GetRenderState,D3DRS_POSITIONORDER,&value)); CHECK(value==79);
    HR(CALL(GetRenderState,D3DRS_ZBIAS,&value)); CHECK(value==111);
    HR(CALL(GetIndices,&ib_out,&base)); CHECK(!ib_out&&base==0xfffffffdu);
    HR(CALL(SetRenderState,D3DRS_POSITIONORDER,18)); HR(CALL(CaptureStateBlock,block));
    HR(CALL(SetRenderState,D3DRS_POSITIONORDER,19)); HR(CALL(ApplyStateBlock,block));
    HR(CALL(GetRenderState,D3DRS_POSITIONORDER,&value)); CHECK(value==18);
    /* Deleting a public handle must not invalidate a captured program. */
    HR(CALL(DeleteVertexShader,vs_green)); CHECK(CALL(SetVertexShader,vs_green)==D3DERR_INVALIDCALL);
    HR(CALL(ApplyStateBlock,block)); HR(CALL(GetVertexShader,&value)); CHECK(value==vs_green);
    draw_shader(d,0xff00ff00u);
    for (UINT type=1;type<=3;++type) {
        DWORD preset=0; HR(CALL(CreateStateBlock,(D3DSTATEBLOCKTYPE)type,&preset));
        HR(CALL(SetRenderState,D3DRS_POSITIONORDER,27));
        HR(CALL(ApplyStateBlock,preset)); HR(CALL(GetRenderState,D3DRS_POSITIONORDER,&value));
        CHECK(value==(type==D3DSBT_PIXELSTATE?27u:18u));
        HR(CALL(SetRenderState,D3DRS_POSITIONORDER,18));
        HR(CALL(CaptureStateBlock,preset)); HR(CALL(DeleteStateBlock,preset));
    }
    HR(CALL(BeginStateBlock)); HR(CALL(SetVertexShader,vs_red));
    pp.BackBufferCount=0; HR(CALL(Reset,&pp)); CHECK(pp.BackBufferCount==1); CHECK(CALL(EndStateBlock,&value)==D3DERR_INVALIDCALL);
    HR(CALL(GetVertexShader,&value)); CHECK(value==0);
    HR(CALL(GetVertexShaderConstant,95,bits_copy,1)); CHECK(equal(bits,bits_copy,16));
    HR(CALL(ApplyStateBlock,block)); HR(CALL(SetPixelShader,ps)); draw_shader(d,0xff00ff00u);
    HR(CALL(DeleteStateBlock,block)); CHECK(CALL(ApplyStateBlock,block)==D3DERR_INVALIDCALL);
    HR(CALL(DeletePixelShader,ps)); HR(CALL(GetPixelShader,&value)); CHECK(value==0);
    HR(CALL(DeleteVertexShader,vs_red)); HR(CALL(SetVertexShader,0));
    HR(CALL(Present,NULL,NULL,NULL,NULL));

    /* Child references retain the actual frontend. Last direct Release drops
     * owned blocks; a subsequent GetDevice revives the same usable interface. */
    HR(CALL(CreateVertexBuffer,16,0,0,D3DPOOL_MANAGED,&vb));
    HR(CALL(CreateStateBlock,D3DSBT_ALL,&block));
    CHECK(CALL(Release)==1);
    HR(IDirect3DVertexBuffer8_GetDevice(vb,&owner)); CHECK(owner==d);
    CHECK(CALL(ApplyStateBlock,block)==D3DERR_INVALIDCALL);
    HR(CALL(TestCooperativeLevel)); CHECK(CALL(Release)==1);
    CHECK(IDirect3DVertexBuffer8_Release(vb)==0);
    char line[200]; wsprintfA(line,"DEVICE8 CHECK pure=%u images=%u checks=%u reset=1 shaderDeletion=1 stateScopes=3 childRevival=1",pure,images,checks); report(line);
}
#ifdef D8WG_PUBLIC_DEVICE
static HRESULT WINAPI public_create(IDirect3D8 *root, struct IDirect3D9 *unused,
        UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior,
        D3DPRESENT_PARAMETERS *pp, IDirect3DDevice8 **out)
{
    (void)unused;
    return IDirect3D8_CreateDevice(root, adapter, type, focus, behavior, pp, out);
}
#endif
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH],path[MAX_PATH]; GetModuleFileNameA(NULL,directory,sizeof(directory));
    UINT end=lstrlenA(directory); while(end&&directory[end-1]!='\\')--end; directory[end]=0;
    lstrcpyA(path,directory); lstrcatA(path,"DEVICE8.TXT");
    log_file=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    CHECK(log_file!=INVALID_HANDLE_VALUE);
    lstrcpyA(path,directory); lstrcatA(path,"D3D8.DLL"); HMODULE public_module=LoadLibraryA(path); CHECK(public_module!=NULL);
    lstrcpyA(path,directory); lstrcatA(path,"CORE8.DLL"); HMODULE core_module=LoadLibraryA(path); CHECK(core_module!=NULL);
    IDirect3D8 *(WINAPI *factory)(UINT)=(void *)(uintptr_t)GetProcAddress(public_module,"Direct3DCreate8");
    struct IDirect3D9 *(WINAPI *core_factory)(void)=(void *)(uintptr_t)GetProcAddress(core_module,"Core8Factory");
    HRESULT (WINAPI *create)(IDirect3D8 *,struct IDirect3D9 *,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS *,IDirect3DDevice8 **)=(void *)(uintptr_t)GetProcAddress(core_module,"Core8Device");
    CHECK(factory&&core_factory&&create);
#ifdef D8WG_PUBLIC_DEVICE
    create = public_create;
    report("PUBLIC Direct3DCreate8/CreateDevice entry");
#endif
    HWND window=CreateWindowExA(0,"STATIC","Device8 frontend",WS_POPUP|WS_VISIBLE,0,0,8,8,NULL,NULL,GetModuleHandleA(NULL),NULL); CHECK(window!=NULL);
    for (UINT sdk=120;sdk<=220;sdk+=100) {
        IDirect3D8 *root=factory(sdk); struct IDirect3D9 *backend_root=core_factory(); CHECK(root&&backend_root);
        exercise(root,backend_root,create,window,FALSE); exercise(root,backend_root,create,window,TRUE);
        CHECK(IUnknown_Release((IUnknown *)backend_root)==0); CHECK(IDirect3D8_Release(root)==0);
    }
    for(UINT i=0;i<97;++i) CHECK(visited[i]>0);
    CHECK(FreeLibrary(core_module)); CHECK(FreeLibrary(public_module));
    for (UINT i=0;i<200&&(GetModuleHandleA("CORE8.DLL")||GetModuleHandleA("D3D8.DLL"));++i) Sleep(10);
    CHECK(!GetModuleHandleA("CORE8.DLL")&&!GetModuleHandleA("D3D8.DLL"));
    char line[200];wsprintfA(line,"PASS Device8 frontend methods=97 devices=4 images=%u checks=%u finalRefs=0",images,checks);report(line);
    DestroyWindow(window); CloseHandle(log_file); ExitProcess(0);
}
