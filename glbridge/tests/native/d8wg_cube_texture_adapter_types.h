#define D8WG_NATIVE_CUBE_ADAPTER 1
typedef UINT D3DRESOURCETYPE,D3DCUBEMAP_FACES;
static const GUID IID_IDirect3DCubeTexture8={7,0,0,{0}};
static const GUID IID_IDirect3DBaseTexture8={6,0,0,{0}};
typedef struct IDirect3DCubeTexture8 IDirect3DCubeTexture8;
typedef struct IDirect3DCubeTexture8Vtbl {
 HRESULT(*QueryInterface)(IDirect3DCubeTexture8*,REFIID,void**);ULONG(*AddRef)(IDirect3DCubeTexture8*);ULONG(*Release)(IDirect3DCubeTexture8*);
 HRESULT(*GetDevice)(IDirect3DCubeTexture8*,IDirect3DDevice8**);
 HRESULT(*SetPrivateData)(IDirect3DCubeTexture8*,REFGUID,const void*,DWORD,DWORD);HRESULT(*GetPrivateData)(IDirect3DCubeTexture8*,REFGUID,void*,DWORD*);HRESULT(*FreePrivateData)(IDirect3DCubeTexture8*,REFGUID);
 DWORD(*SetPriority)(IDirect3DCubeTexture8*,DWORD);DWORD(*GetPriority)(IDirect3DCubeTexture8*);void(*PreLoad)(IDirect3DCubeTexture8*);D3DRESOURCETYPE(*GetType)(IDirect3DCubeTexture8*);
 DWORD(*SetLOD)(IDirect3DCubeTexture8*,DWORD);DWORD(*GetLOD)(IDirect3DCubeTexture8*);DWORD(*GetLevelCount)(IDirect3DCubeTexture8*);
 HRESULT(*GetLevelDesc)(IDirect3DCubeTexture8*,UINT,D3DSURFACE_DESC*);HRESULT(*GetCubeMapSurface)(IDirect3DCubeTexture8*,D3DCUBEMAP_FACES,UINT,IDirect3DSurface8**);
 HRESULT(*LockRect)(IDirect3DCubeTexture8*,D3DCUBEMAP_FACES,UINT,D3DLOCKED_RECT*,const RECT*,DWORD);HRESULT(*UnlockRect)(IDirect3DCubeTexture8*,D3DCUBEMAP_FACES,UINT);HRESULT(*AddDirtyRect)(IDirect3DCubeTexture8*,D3DCUBEMAP_FACES,const RECT*);
}IDirect3DCubeTexture8Vtbl;
struct IDirect3DCubeTexture8{const IDirect3DCubeTexture8Vtbl*lpVtbl;};
static const D9WGCubeTextureMethods8*d9wg_d3d9_core_cube_texture_methods8(struct IDirect3DCubeTexture9*);
