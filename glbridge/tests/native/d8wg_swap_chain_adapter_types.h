#define D8WG_NATIVE_SWAP_CHAIN_ADAPTER 1
typedef void*HWND;typedef struct RGNDATA{DWORD words[4];}RGNDATA;typedef UINT D3DBACKBUFFER_TYPE;
static const GUID IID_IDirect3DSwapChain8={12,0,0,{0}};
struct IDirect3DSwapChain9{const IUnknownVtbl*lpVtbl;};
typedef struct IDirect3DSwapChain8 IDirect3DSwapChain8;
typedef struct IDirect3DSwapChain8Vtbl{
 HRESULT(*QueryInterface)(IDirect3DSwapChain8*,REFIID,void**);ULONG(*AddRef)(IDirect3DSwapChain8*);ULONG(*Release)(IDirect3DSwapChain8*);
 HRESULT(*Present)(IDirect3DSwapChain8*,const RECT*,const RECT*,HWND,const RGNDATA*);
 HRESULT(*GetBackBuffer)(IDirect3DSwapChain8*,UINT,D3DBACKBUFFER_TYPE,IDirect3DSurface8**);
}IDirect3DSwapChain8Vtbl;
struct IDirect3DSwapChain8{const IDirect3DSwapChain8Vtbl*lpVtbl;};
typedef struct D9WGSwapChainMethods8 D9WGSwapChainMethods8;
static const D9WGSwapChainMethods8*d9wg_d3d9_core_swap_chain_methods8(struct IDirect3DSwapChain9*);
