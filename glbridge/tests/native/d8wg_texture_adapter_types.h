#define D8WG_NATIVE_TEXTURE_ADAPTER 1
typedef UINT D3DRESOURCETYPE;
static const GUID IID_IDirect3DBaseTexture8={6,0,0,{0}};
typedef struct IDirect3DTexture8 IDirect3DTexture8;
typedef struct IDirect3DTexture8Vtbl {
 HRESULT(*QueryInterface)(IDirect3DTexture8*,REFIID,void**);ULONG(*AddRef)(IDirect3DTexture8*);ULONG(*Release)(IDirect3DTexture8*);
 HRESULT(*GetDevice)(IDirect3DTexture8*,IDirect3DDevice8**);
 HRESULT(*SetPrivateData)(IDirect3DTexture8*,REFGUID,const void*,DWORD,DWORD);HRESULT(*GetPrivateData)(IDirect3DTexture8*,REFGUID,void*,DWORD*);HRESULT(*FreePrivateData)(IDirect3DTexture8*,REFGUID);
 DWORD(*SetPriority)(IDirect3DTexture8*,DWORD);DWORD(*GetPriority)(IDirect3DTexture8*);void(*PreLoad)(IDirect3DTexture8*);D3DRESOURCETYPE(*GetType)(IDirect3DTexture8*);
 DWORD(*SetLOD)(IDirect3DTexture8*,DWORD);DWORD(*GetLOD)(IDirect3DTexture8*);DWORD(*GetLevelCount)(IDirect3DTexture8*);
 HRESULT(*GetLevelDesc)(IDirect3DTexture8*,UINT,D3DSURFACE_DESC*);HRESULT(*GetSurfaceLevel)(IDirect3DTexture8*,UINT,IDirect3DSurface8**);
 HRESULT(*LockRect)(IDirect3DTexture8*,UINT,D3DLOCKED_RECT*,const RECT*,DWORD);HRESULT(*UnlockRect)(IDirect3DTexture8*,UINT);HRESULT(*AddDirtyRect)(IDirect3DTexture8*,const RECT*);
}IDirect3DTexture8Vtbl;
struct IDirect3DTexture8{const IDirect3DTexture8Vtbl*lpVtbl;};
static const D9WGTextureMethods8*d9wg_d3d9_core_texture_methods8(struct IDirect3DTexture9*);
