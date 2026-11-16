#define D8WG_NATIVE_BUFFER_ADAPTER 1
typedef uint8_t BYTE;typedef UINT D3DRESOURCETYPE;
static const GUID IID_IDirect3DVertexBuffer8={10,0,0,{0}},IID_IDirect3DIndexBuffer8={11,0,0,{0}};
struct IDirect3DVertexBuffer9{const IUnknownVtbl*lpVtbl;};struct IDirect3DIndexBuffer9{const IUnknownVtbl*lpVtbl;};
typedef struct D3DVERTEXBUFFER_DESC{DWORD words[6];}D3DVERTEXBUFFER_DESC;
typedef struct D3DINDEXBUFFER_DESC{DWORD words[5];}D3DINDEXBUFFER_DESC;
#define NATIVE_BUFFER8(T,Desc) \
typedef struct T T;typedef struct T##Vtbl{ \
 HRESULT(*QueryInterface)(T*,REFIID,void**);ULONG(*AddRef)(T*);ULONG(*Release)(T*); \
 HRESULT(*GetDevice)(T*,IDirect3DDevice8**); \
 HRESULT(*SetPrivateData)(T*,REFGUID,const void*,DWORD,DWORD);HRESULT(*GetPrivateData)(T*,REFGUID,void*,DWORD*);HRESULT(*FreePrivateData)(T*,REFGUID); \
 DWORD(*SetPriority)(T*,DWORD);DWORD(*GetPriority)(T*);void(*PreLoad)(T*);D3DRESOURCETYPE(*GetType)(T*); \
 HRESULT(*Lock)(T*,UINT,UINT,BYTE**,DWORD);HRESULT(*Unlock)(T*);HRESULT(*GetDesc)(T*,Desc*); \
}T##Vtbl;struct T{const T##Vtbl*lpVtbl;};
NATIVE_BUFFER8(IDirect3DVertexBuffer8,D3DVERTEXBUFFER_DESC)
NATIVE_BUFFER8(IDirect3DIndexBuffer8,D3DINDEXBUFFER_DESC)
#undef NATIVE_BUFFER8
static const D9WGBufferMethods8*d9wg_d3d9_core_buffer_methods8(IUnknown*,UINT);
