static const IUnknownVtbl wrong_backend_vtbl={backend_query,backend_add,backend_release};
typedef struct BufferBackend{Backend core;UINT calls[10],offset,bytes;DWORD priority,flags;void*output;const void*input;REFGUID guid;DWORD*size_out;HRESULT result;BYTE data[64];}BufferBackend;
static BufferBackend buffer_backend(UINT kind){BufferBackend b={0};b.core=new_backend(1);b.core.kind=kind;return b;}
static HRESULT bf_set(IUnknown*p,const GUID*g,const void*data,DWORD size,DWORD flags){BufferBackend*b=(BufferBackend*)p;b->calls[0]++;b->guid=g;b->input=data;b->bytes=size;b->flags=flags;return b->result;}
static HRESULT bf_get(IUnknown*p,const GUID*g,void*data,DWORD*size){BufferBackend*b=(BufferBackend*)p;b->calls[1]++;b->guid=g;b->output=data;b->size_out=size;return b->result;}
static HRESULT bf_free(IUnknown*p,const GUID*g){BufferBackend*b=(BufferBackend*)p;b->calls[2]++;b->guid=g;return b->result;}
static DWORD bf_priority_set(IUnknown*p,DWORD value){BufferBackend*b=(BufferBackend*)p;b->calls[3]++;DWORD old=b->priority;b->priority=value;return old;}
static DWORD bf_priority_get(IUnknown*p){BufferBackend*b=(BufferBackend*)p;b->calls[4]++;return b->priority;}
static void bf_preload(IUnknown*p){((BufferBackend*)p)->calls[5]++;}
static UINT bf_type(IUnknown*p){BufferBackend*b=(BufferBackend*)p;b->calls[6]++;return b->core.kind;}
static HRESULT bf_lock(IUnknown*p,UINT off,UINT size,void*out,DWORD flags){BufferBackend*b=(BufferBackend*)p;b->calls[7]++;b->offset=off;b->bytes=size;b->output=out;b->flags=flags;return b->result;}
static HRESULT bf_unlock(IUnknown*p){BufferBackend*b=(BufferBackend*)p;b->calls[8]++;return b->result;}
static HRESULT bf_desc(IUnknown*p,void*out,UINT bytes){BufferBackend*b=(BufferBackend*)p;b->calls[9]++;b->output=out;b->bytes=bytes;if(!out||bytes!=(b->core.kind==6?24:20))return D3DERR_INVALIDCALL;if(b->result)return b->result;for(UINT i=0;i<bytes/4;i++)((DWORD*)out)[i]=0x81230000u+b->core.kind*0x100u+i;return 0;}
static const D9WGBufferMethods8 buffer_methods={bf_set,bf_get,bf_free,bf_priority_set,bf_priority_get,bf_preload,bf_type,bf_lock,bf_unlock,bf_desc};
static const D9WGBufferMethods8*d9wg_d3d9_core_buffer_methods8(IUnknown*p,UINT expected){Backend*b=(Backend*)p;return p&&p->lpVtbl==&backend_vtbl&&b->profile&&b->kind==expected&&(expected==6||expected==7)?&buffer_methods:NULL;}
#define BUFFER_CASES8(Name,Type,Desc,BackendType,Wrap,Getter,Kind,ClassID,OtherID) \
static int Name(UINT*cases){ \
 for(UINT round=0;round<64;round++)for(UINT order=0;order<4;order++){ \
  BufferBackend b=buffer_backend(Kind);Parent8 d=new_parent();Type*x=NULL; \
  CHECK(!Wrap((BackendType*)&b.core.iface,(IDirect3DDevice8*)&d.iface,&x)&&x&&live==2&&b.core.refs==1&&d.refs==2&&Getter(x)==(BackendType*)&b.core.iface); \
  CHECK(!Getter(NULL)&&!Getter((Type*)&d.iface)); \
  CHECK(Kind==6u?!d8wg_core_index_buffer_backend8((IDirect3DIndexBuffer8*)x):!d8wg_core_vertex_buffer_backend8((IDirect3DVertexBuffer8*)x)); \
  const GUID*ids[]={&IID_IUnknown,&IID_IDirect3DResource8,&ClassID};void*out=(void*)99; \
  CHECK(x->lpVtbl->QueryInterface(x,NULL,&out)==E_NOINTERFACE&&!out&&x->lpVtbl->QueryInterface(x,&OtherID,&out)==E_NOINTERFACE&&!out&&x->lpVtbl->QueryInterface(x,&IID_IUnknown,NULL)==E_POINTER); \
  for(UINT i=0;i<3;i++)CHECK(!x->lpVtbl->QueryInterface(x,ids[i],&out)&&out==x&&x->lpVtbl->Release((Type*)out)==1&&b.core.refs==1&&d.refs==2); \
  for(UINT i=0;i<16;i++){GUID bad=ClassID;((uint8_t*)&bad)[i]^=0x80;out=(void*)99;CHECK(x->lpVtbl->QueryInterface(x,&bad,&out)==E_NOINTERFACE&&!out);} \
  IDirect3DDevice8*device=NULL;CHECK(x->lpVtbl->GetDevice(x,NULL)==D3DERR_INVALIDCALL&&!x->lpVtbl->GetDevice(x,&device)&&(IUnknown*)device==&d.iface);IUnknown_Release((IUnknown*)device); \
  CHECK(!x->lpVtbl->SetPriority(x,0x81234567u)&&x->lpVtbl->GetPriority(x)==0x81234567u&&b.calls[3]==1&&b.calls[4]==1);x->lpVtbl->PreLoad(x);CHECK(b.calls[5]==1&&x->lpVtbl->GetType(x)==Kind&&b.calls[6]==1); \
  Desc desc;CHECK(!x->lpVtbl->GetDesc(x,&desc)&&b.bytes==sizeof(desc)&&b.calls[9]==1);for(UINT i=0;i<sizeof(desc)/4;i++)CHECK(desc.words[i]==0x81230000u+Kind*0x100u+i);CHECK(x->lpVtbl->GetDesc(x,NULL)==D3DERR_INVALIDCALL); \
  DWORD words[4]={1,2,3,4},size=16;BYTE*locked=NULL; \
  for(UINT failure=0;failure<2;failure++){b.result=failure?-77:0; \
   CHECK(x->lpVtbl->SetPrivateData(x,&bad_iid,words,13,0x80000001u)==b.result&&b.guid==&bad_iid&&b.input==words&&b.bytes==13&&b.flags==0x80000001u); \
   CHECK(x->lpVtbl->GetPrivateData(x,&bad_iid,words,&size)==b.result&&b.guid==&bad_iid&&b.output==words&&b.size_out==&size); \
   CHECK(x->lpVtbl->FreePrivateData(x,&bad_iid)==b.result&&b.guid==&bad_iid); \
   CHECK(x->lpVtbl->Lock(x,13,29,&locked,0xffffffffu)==b.result&&b.offset==13&&b.bytes==29&&b.output==&locked&&b.flags==0xffffffffu); \
   CHECK(x->lpVtbl->Unlock(x)==b.result); \
   if(failure)CHECK(x->lpVtbl->GetDesc(x,&desc)==b.result); \
  }b.result=0; \
  UINT count=allocations;fail_allocation=allocations+1;backend_add((IUnknown*)&b.core);Type*again=NULL;CHECK(!Wrap((BackendType*)&b.core.iface,(IDirect3DDevice8*)&d.iface,&again)&&again==x&&allocations==count&&again->lpVtbl->Release(again)==1&&b.core.refs==1);fail_allocation=0; \
  if(order==0){CHECK(!x->lpVtbl->Release(x)&&!live&&!b.core.alive&&d.refs==1&&!IUnknown_Release(&d.iface));} \
  else if(order==1){CHECK(IUnknown_Release(&d.iface)==1&&!x->lpVtbl->Release(x)&&!live&&!d.refs&&!b.core.alive);} \
  else{b.core.private_refs=1;CHECK(!x->lpVtbl->Release(x)&&!b.core.refs&&b.core.alive&&live==2&&d.refs==1); \
   if(order==2){backend_add((IUnknown*)&b.core);CHECK(!Wrap((BackendType*)&b.core.iface,(IDirect3DDevice8*)&d.iface,&again)&&again==x&&b.core.refs==1&&d.refs==2&&!again->lpVtbl->Release(again));} \
   else CHECK(x->lpVtbl->AddRef(x)==1&&b.core.refs==1&&d.refs==2&&!x->lpVtbl->Release(x)); \
   b.core.private_refs=0;backend_retire(&b.core);CHECK(!live&&!b.core.alive&&b.core.destroys==1&&!IUnknown_Release(&d.iface)); \
  }(*cases)++; \
 } \
 for(UINT failure=0;failure<9;failure++){BufferBackend b=buffer_backend(Kind);Parent8 d=new_parent();Type*out=(void*)99;HRESULT expected=D3DERR_INVALIDCALL; \
  if(failure==0)CHECK(Wrap((BackendType*)&b.core.iface,(IDirect3DDevice8*)&d.iface,NULL)==expected); \
  else{if(failure==1)b.core.profile=FALSE;if(failure==2)b.core.kind=Kind==6?7:6;if(failure==3)b.core.iface.lpVtbl=&wrong_backend_vtbl;if(failure==5)b.core.fail_get=TRUE;if(failure==6||failure==7){fail_allocation=allocations+(failure==6?1:2);expected=E_OUTOFMEMORY;}if(failure==5)expected=-77;if(failure==8)b.core.race=2; \
   CHECK(Wrap((BackendType*)&b.core.iface,failure==4?NULL:(IDirect3DDevice8*)&d.iface,&out)==expected&&!out); \
  }fail_allocation=0;b.core.iface.lpVtbl=&backend_vtbl;CHECK(!live&&!b.core.alive&&d.refs==1);(*cases)++; \
 } \
 {BufferBackend b=buffer_backend(Kind);Parent8 d=new_parent();b.core.race=1;Type*x=NULL;CHECK(!Wrap((BackendType*)&b.core.iface,(IDirect3DDevice8*)&d.iface,&x)&&x&&b.core.refs==2&&d.refs==2);CHECK(x->lpVtbl->AddRef(x)==2&&backend_release((IUnknown*)&b.core)==1&&x->lpVtbl->Release(x)==1&&!x->lpVtbl->Release(x)&&!live&&d.refs==1);(*cases)++;} \
 {BufferBackend b=buffer_backend(Kind);Parent8 d=new_parent(),other=new_parent();Type*x=NULL,*out=(void*)99;CHECK(!Wrap((BackendType*)&b.core.iface,(IDirect3DDevice8*)&d.iface,&x));backend_add((IUnknown*)&b.core);CHECK(Wrap((BackendType*)&b.core.iface,(IDirect3DDevice8*)&other.iface,&out)==D3DERR_INVALIDCALL&&!out&&b.core.refs==1&&d.refs==2&&other.refs==1); \
  const IUnknownVtbl*save=((D8CoreBuffer*)x)->iface.lpVtbl;((D8CoreBuffer*)x)->iface.lpVtbl=&parent_vtbl;backend_add((IUnknown*)&b.core);CHECK(Wrap((BackendType*)&b.core.iface,(IDirect3DDevice8*)&d.iface,&out)==D3DERR_INVALIDCALL&&!out);((D8CoreBuffer*)x)->iface.lpVtbl=save; \
  ((D8CoreBuffer*)x)->kind=99;backend_add((IUnknown*)&b.core);CHECK(Wrap((BackendType*)&b.core.iface,(IDirect3DDevice8*)&d.iface,&out)==D3DERR_INVALIDCALL&&!out);((D8CoreBuffer*)x)->kind=Kind;CHECK(!x->lpVtbl->Release(x)&&!live);(*cases)++;} \
 {Parent8 d=new_parent();Type*out=(void*)99;CHECK(Wrap(NULL,(IDirect3DDevice8*)&d.iface,&out)==D3DERR_INVALIDCALL&&!out&&!live&&d.refs==1);(*cases)++;}return 0;}
BUFFER_CASES8(vertex_cases,IDirect3DVertexBuffer8,D3DVERTEXBUFFER_DESC,struct IDirect3DVertexBuffer9,d8wg_core_wrap_vertex_buffer8,d8wg_core_vertex_buffer_backend8,6u,IID_IDirect3DVertexBuffer8,IID_IDirect3DIndexBuffer8)
BUFFER_CASES8(index_cases,IDirect3DIndexBuffer8,D3DINDEXBUFFER_DESC,struct IDirect3DIndexBuffer9,d8wg_core_wrap_index_buffer8,d8wg_core_index_buffer_backend8,7u,IID_IDirect3DIndexBuffer8,IID_IDirect3DVertexBuffer8)
#undef BUFFER_CASES8
int main(void){CHECK(!retained_surface_main());UINT before=checks,cases=0;CHECK(!vertex_cases(&cases)&&!index_cases(&cases));printf("PASS actual SDK8 VB8/IB8 adapters: %u checks / %u cases; both14method typed vtables,one shared implementation,allGUIDbytes/Resource8/class/device identity,priority/preload/type/private/24-20byte descriptors/lock argument-status forwarding,weakcache/publiczero/refcoalescing/parent-first/device-first/race/OOM/ownedinput guards;backend explicit COM/metadata shim\n",checks-before,cases);return 0;}
