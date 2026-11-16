#define D8WG_NATIVE_VOLUME_ADAPTER 1
typedef UINT D3DRESOURCETYPE;
static const GUID IID_IDirect3DVolumeTexture8={8,0,0,{0}},IID_IDirect3DVolume8={9,0,0,{0}};
typedef struct D3DBOX{UINT Left,Top,Right,Bottom,Front,Back;}D3DBOX;
typedef struct D3DLOCKED_BOX{LONG RowPitch,SlicePitch;void*pBits;}D3DLOCKED_BOX;
typedef struct D3DVOLUME_DESC{DWORD words[8];}D3DVOLUME_DESC;
struct IDirect3DVolume9{const IUnknownVtbl*lpVtbl;};
typedef struct IDirect3DVolume8 IDirect3DVolume8;
typedef struct IDirect3DVolume8Vtbl{
 HRESULT(*QueryInterface)(IDirect3DVolume8*,REFIID,void**);ULONG(*AddRef)(IDirect3DVolume8*);ULONG(*Release)(IDirect3DVolume8*);
 HRESULT(*GetDevice)(IDirect3DVolume8*,IDirect3DDevice8**);
 HRESULT(*SetPrivateData)(IDirect3DVolume8*,REFGUID,const void*,DWORD,DWORD);HRESULT(*GetPrivateData)(IDirect3DVolume8*,REFGUID,void*,DWORD*);HRESULT(*FreePrivateData)(IDirect3DVolume8*,REFGUID);
 HRESULT(*GetContainer)(IDirect3DVolume8*,REFIID,void**);HRESULT(*GetDesc)(IDirect3DVolume8*,D3DVOLUME_DESC*);HRESULT(*LockBox)(IDirect3DVolume8*,D3DLOCKED_BOX*,const D3DBOX*,DWORD);HRESULT(*UnlockBox)(IDirect3DVolume8*);
}IDirect3DVolume8Vtbl;
struct IDirect3DVolume8{const IDirect3DVolume8Vtbl*lpVtbl;};

static const GUID IID_IDirect3DBaseTexture8={6,0,0,{0}};
typedef struct IDirect3DVolumeTexture8 IDirect3DVolumeTexture8;
typedef struct IDirect3DVolumeTexture8Vtbl {
 HRESULT(*QueryInterface)(IDirect3DVolumeTexture8*,REFIID,void**);ULONG(*AddRef)(IDirect3DVolumeTexture8*);ULONG(*Release)(IDirect3DVolumeTexture8*);
 HRESULT(*GetDevice)(IDirect3DVolumeTexture8*,IDirect3DDevice8**);
 HRESULT(*SetPrivateData)(IDirect3DVolumeTexture8*,REFGUID,const void*,DWORD,DWORD);HRESULT(*GetPrivateData)(IDirect3DVolumeTexture8*,REFGUID,void*,DWORD*);HRESULT(*FreePrivateData)(IDirect3DVolumeTexture8*,REFGUID);
 DWORD(*SetPriority)(IDirect3DVolumeTexture8*,DWORD);DWORD(*GetPriority)(IDirect3DVolumeTexture8*);void(*PreLoad)(IDirect3DVolumeTexture8*);D3DRESOURCETYPE(*GetType)(IDirect3DVolumeTexture8*);
 DWORD(*SetLOD)(IDirect3DVolumeTexture8*,DWORD);DWORD(*GetLOD)(IDirect3DVolumeTexture8*);DWORD(*GetLevelCount)(IDirect3DVolumeTexture8*);
 HRESULT(*GetLevelDesc)(IDirect3DVolumeTexture8*,UINT,D3DVOLUME_DESC*);HRESULT(*GetVolumeLevel)(IDirect3DVolumeTexture8*,UINT,IDirect3DVolume8**);
 HRESULT(*LockBox)(IDirect3DVolumeTexture8*,UINT,D3DLOCKED_BOX*,const D3DBOX*,DWORD);HRESULT(*UnlockBox)(IDirect3DVolumeTexture8*,UINT);HRESULT(*AddDirtyBox)(IDirect3DVolumeTexture8*,const D3DBOX*);
}IDirect3DVolumeTexture8Vtbl;
struct IDirect3DVolumeTexture8{const IDirect3DVolumeTexture8Vtbl*lpVtbl;};
static const D9WGVolumeTextureMethods8*d9wg_d3d9_core_volume_texture_methods8(struct IDirect3DVolumeTexture9*);
