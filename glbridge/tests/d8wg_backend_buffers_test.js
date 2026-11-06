"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"backend8-buffers-")),source=readComparisonSource(process.env.D8WG_BACKEND_BUFFERS_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8");
function extract(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return s.slice(m.index,b);}
function sdk(api){const r=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",["-E","-P","-x","c","-"],{input:"#include <d3d"+api+".h>\n",encoding:"utf8",maxBuffer:16<<20});assert.equal(r.status,0,r.stderr);return r.stdout;}
try{const e=sdk(8),n=sdk(9),desc=(s,kind,name)=>{const m=new RegExp("typedef struct _D3D"+kind+"BUFFER_DESC\\s*\\{[\\s\\S]*?\\}\\s*D3D"+kind+"BUFFER_DESC\\s*;").exec(s);assert(m);return m[0].replace("struct _D3D"+kind+"BUFFER_DESC","struct").replace(new RegExp("\\bD3D"+kind+"BUFFER_DESC\\b","g"),name);};
for(const[name,value]of Object.entries({D3DFMT_VERTEXDATA:100,D3DFMT_INDEX16:101,D3DFMT_INDEX32:102,D3DRTYPE_VERTEXBUFFER:6,D3DRTYPE_INDEXBUFFER:7})){const pattern=new RegExp("\\b"+name+"\\s*=\\s*(\\d+)");assert.equal(Number(pattern.exec(e)?.[1]),value,name+" SDK8");assert.equal(Number(pattern.exec(n)?.[1]),value,name+" SDK9");}
const old8=readComparisonSource(root+"/tests/fixtures/d3d8_buffers_before_core.c","utf8"),old9=readComparisonSource(root+"/tests/fixtures/d3d9_buffers_before_core.c","utf8"),source8=readComparisonSource(root+"/d3d8proxy/d3d8_proxy.c","utf8");
const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>
#include "${root}/d3d9proxy/d3d9_protocol.h"
typedef uint32_t UINT,DWORD,D3DFORMAT,D3DRESOURCETYPE,D3DPOOL;typedef int32_t BOOL,LONG,HRESULT;typedef uint8_t BYTE;typedef void*HANDLE;typedef size_t SIZE_T;
${desc(e,'VERTEX','VB8Desc')}
${desc(n,'VERTEX','D3DVERTEXBUFFER_DESC')}
${desc(e,'INDEX','IB8Desc')}
${desc(n,'INDEX','D3DINDEXBUFFER_DESC')}
typedef struct{const void*lpVtbl;}Iface,IDirect3DVertexBuffer8,IDirect3DVertexBuffer9,IDirect3DIndexBuffer8,IDirect3DIndexBuffer9;
typedef struct Device Device,D8Device,D9Device,IDirect3DDevice8,IDirect3DDevice9;typedef struct Buffer Buffer,D8VertexBuffer,D9VertexBuffer,D8IndexBuffer,D9IndexBuffer;
struct Device{UINT api,pins,handle;Buffer*vertex_buffers,*index_buffers;};struct Buffer{Iface iface;LONG refcount;Device*device;UINT handle,length,usage,fvf,format,pool;BOOL locked;UINT lock_offset,lock_size,lock_flags,binding_refs;void*clips;BYTE*shadow,*shadow_allocation;Buffer*next_device_resource;};
#define WINAPI
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define E_OUTOFMEMORY (-3)
#define E_NOTIMPL (-4)
#define TRUE 1
#define FALSE 0
#define HEAP_ZERO_MEMORY 8
#define D3DPOOL_SCRATCH 3
#define D3DUSAGE_RENDERTARGET 1
#define D3DUSAGE_DEPTHSTENCIL 2
#define D3DFMT_VERTEXDATA 100
#define D3DFMT_INDEX16 101
#define D3DFMT_INDEX32 102
#define D3DRTYPE_VERTEXBUFFER 6
#define D3DRTYPE_INDEXBUFFER 7
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
#define HOSTLOG_FAILED(...)
#define TRACE_REGISTER_RANGE(...)
#define TRACE_MARK_ENTER(...)
#define TRACE_MARK_EXIT(...)
#define TRACE_BUFFER_CREATED(...)
#define TRACE_FLUSH(...)
#define ZeroMemory(p,n) memset(p,0,n)
#define GetProcessHeap() NULL
static int g_vb_vtbl,g_ib_vtbl;static UINT allocations,frees,live,fail_at,transport_fail,creates,handles,shared_seen;static D9WGCreateBuffer emitted;
static void*HeapAlloc(void*h,UINT flags,size_t bytes){(void)h;if(++allocations==fail_at||bytes>131072)return NULL;void*p=(BYTE*)malloc((bytes?bytes:1)+8)+8;assert(p);memset(p,flags&8?0:0xcd,bytes);live++;return p;}
static BOOL HeapFree(void*h,UINT flags,void*p){(void)h;(void)flags;assert(p&&live);frees++;live--;free((BYTE*)p-8);return TRUE;}
static Device*device_from_iface(Device*d){return d;}static Buffer*vb_from_iface(Iface*i){return(Buffer*)i;}static Buffer*ib_from_iface(Iface*i){return(Buffer*)i;}static UINT device_compatibility_api(Device*d){return d->api;}
static void device_child_add_ref(Device*d){d->pins++;}static void device_child_release(Device*d){assert(d->pins);d->pins--;}static UINT allocate_handle(void){return ++handles;}
static BOOL emit_command(UINT op,const void*p,UINT bytes){assert(op==D9WG_OP_CREATE_BUFFER&&bytes==sizeof(emitted));memcpy(&emitted,p,bytes);creates++;return !transport_fail;}
${['emit_vertex_buffer_create','emit_index_buffer_create'].map(x=>extract(source,x)).join('\n')}
${['emit_vertex_buffer_create','emit_index_buffer_create'].map(x=>extract(source8,x).replace(new RegExp('\\b'+x+'\\b','g'),'old8_'+x)).join('\n')}
${['device_create_vertex_buffer','device_create_index_buffer','vb_get_desc','ib_get_desc'].map(x=>extract(old8,x).replace(new RegExp('\\b'+x+'\\b','g'),'old8_'+x).replace(/\bemit_vertex_buffer_create\b/g,'old8_emit_vertex_buffer_create').replace(/\bemit_index_buffer_create\b/g,'old8_emit_index_buffer_create')).join('\n')}
${['device_create_vertex_buffer','device_create_index_buffer'].map(x=>extract(old9,x).replace(new RegExp('\\b'+x+'\\b','g'),'old9_'+x)).join('\n')}
${['device_create_vertex_buffer','device_create_index_buffer','vb_get_desc','ib_get_desc'].map(x=>extract(source,x)).join('\n')}
static HRESULT dispatch_vb(Device*d,UINT len,UINT usage,UINT fvf,UINT pool,Iface**out,HANDLE*shared){shared_seen=!!shared;return device_create_vertex_buffer(d,len,usage,fvf,pool,out,shared);}
static HRESULT dispatch_ib(Device*d,UINT len,UINT usage,UINT format,UINT pool,Iface**out,HANDLE*shared){shared_seen=!!shared;return device_create_index_buffer(d,len,usage,format,pool,out,shared);}
#define IDirect3DDevice9_CreateVertexBuffer dispatch_vb
#define IDirect3DDevice9_CreateIndexBuffer dispatch_ib
${['d9wg_d3d9_core_vertex_buffer8','d9wg_d3d9_core_index_buffer8'].map(x=>extract(source,x)).join('\n')}
static UINT checks,cases,api8cases,api9cases,zeroed;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Backend8 buffers case %u line %u: %s\\n",cases,__LINE__,#v);return 1;}checks++;}while(0)
static void cleanup(Iface*i,UINT kind){Buffer*b=(Buffer*)i;b->device->pins--;if(kind)b->device->index_buffers=NULL;else b->device->vertex_buffers=NULL;HeapFree(NULL,0,b->shadow_allocation?b->shadow_allocation:b->shadow);HeapFree(NULL,0,b);}
static BOOL zeros(const BYTE*p,UINT n){for(UINT i=0;i<n;i++)if(p[i])return FALSE;return TRUE;}
int main(void){(void)&device_compatibility_api;(void)&dispatch_vb;(void)&dispatch_ib;_Static_assert(sizeof(VB8Desc)==24&&sizeof(D3DVERTEXBUFFER_DESC)==24&&sizeof(IB8Desc)==20&&sizeof(D3DINDEXBUFFER_DESC)==20,"SDK buffer sizes");_Static_assert(offsetof(VB8Desc,FVF)==20&&offsetof(D3DVERTEXBUFFER_DESC,FVF)==20&&offsetof(IB8Desc,Size)==16&&offsetof(D3DINDEXBUFFER_DESC,Size)==16,"SDK buffer fields");const UINT lengths[]={0,1,2,3,4,12,24,80,65536,131088,UINT32_MAX},usages[]={0,8,512,520,UINT32_MAX,1,2},formats[]={0,44,324,101,102,UINT32_MAX};
for(UINT api=8;api<=9;api++)for(UINT kind=0;kind<2;kind++)for(UINT l=0;l<11;l++)for(UINT u=0;u<7;u++)for(UINT f=0;f<6;f++)for(UINT pool=0;pool<5;pool++)for(UINT fail=0;fail<4;fail++)for(UINT nullout=0;nullout<2;nullout++){
 Device before_device={.api=api,.handle=77},actual_device={.api=api,.handle=77};Iface*before=(void*)(uintptr_t)99,*actual=(void*)(uintptr_t)99;assert(!live);allocations=frees=creates=handles=shared_seen=0;memset(&emitted,0xcc,sizeof(emitted));fail_at=fail==3?0:fail;transport_fail=fail==3;UINT len=lengths[l],usage=usages[u],fmt=formats[f];/* Wine retail8 rejects scratch and target/depth buffer usage before allocation. */
 BOOL retail_refusal=pool==3||(usage&3);HRESULT wanted=retail_refusal?(nullout?D3DERR_INVALIDCALL:(before=NULL,D3DERR_INVALIDCALL)):api==8?(kind?old8_device_create_index_buffer(&before_device,len,usage,fmt,pool,nullout?NULL:&before):old8_device_create_vertex_buffer(&before_device,len,usage,fmt,pool,nullout?NULL:&before)):(kind?old9_device_create_index_buffer(&before_device,len,usage,fmt,pool,nullout?NULL:&before,NULL):old9_device_create_vertex_buffer(&before_device,len,usage,fmt,pool,nullout?NULL:&before,NULL));UINT a=allocations,fr=frees,c=creates,hs=handles;D9WGCreateBuffer expected_command=emitted;
 allocations=frees=creates=handles=shared_seen=0;memset(&emitted,0xcc,sizeof(emitted));HRESULT hr=api==8?(kind?d9wg_d3d9_core_index_buffer8(&actual_device,len,usage,fmt,pool,nullout?NULL:&actual):d9wg_d3d9_core_vertex_buffer8(&actual_device,len,usage,fmt,pool,nullout?NULL:&actual)):(kind?device_create_index_buffer(&actual_device,len,usage,fmt,pool,nullout?NULL:&actual,NULL):device_create_vertex_buffer(&actual_device,len,usage,fmt,pool,nullout?NULL:&actual,NULL));
 CHECK(hr==wanted&&allocations+(len==UINT32_MAX&&a==2?1u:0u)==a&&frees==fr&&creates==c&&handles==hs&&!shared_seen&&!memcmp(&emitted,&expected_command,sizeof(emitted)));CHECK(actual_device.pins==before_device.pins&&actual_device.pins==(!hr));
 if(!hr){Buffer*b=(Buffer*)before,*v=(Buffer*)actual;CHECK(!v->locked&&!v->lock_offset&&!v->lock_size&&!v->lock_flags&&!v->binding_refs&&!v->clips&&v->refcount==1&&v->device==&actual_device&&v->length==len&&v->usage==usage&&v->pool==pool&&(kind?v->format==fmt:v->fvf==fmt)&&(kind?actual_device.index_buffers:actual_device.vertex_buffers)==v);CHECK(!((uintptr_t)v->shadow&15u)&&v->shadow_allocation&&v->shadow>=v->shadow_allocation&&v->shadow-v->shadow_allocation<=15);CHECK(!memcmp(b->shadow,v->shadow,len)&&zeros(v->shadow,len));zeroed+=len;UINT desc_before[8],desc_after[8];memset(desc_before,0xcc,sizeof(desc_before));memset(desc_after,0xcc,sizeof(desc_after));HRESULT hb=kind?old8_ib_get_desc(before,(void*)desc_before):old8_vb_get_desc(before,(void*)desc_before),ha=kind?ib_get_desc(actual,(void*)desc_after):vb_get_desc(actual,(void*)desc_after);CHECK(!hb&&!ha&&!memcmp(desc_before,desc_after,sizeof(desc_before)));CHECK((kind?ib_get_desc(actual,NULL):vb_get_desc(actual,NULL))==-1);cleanup(before,kind);cleanup(actual,kind);}else CHECK(before==actual&&(!actual||actual==(void*)(uintptr_t)99)&&!before_device.vertex_buffers&&!before_device.index_buffers&&!actual_device.vertex_buffers&&!actual_device.index_buffers);CHECK(!live&&!before_device.pins&&!actual_device.pins);cases++;if(api==8)api8cases++;else api9cases++;
}
/* Shared-resource requests are unsupported on XP, including a pointer to a
 * NULL handle. Refuse before reading the handle, mutating outputs or allocating. */
for(UINT kind=0;kind<2;kind++)for(UINT pool=0;pool<4;pool++)for(UINT valid=0;valid<2;valid++){
 Device d={.api=9};Iface*out=(void*)(uintptr_t)99;HANDLE shared=valid?(void*)(uintptr_t)123:NULL;allocations=frees=creates=handles=0;
 HRESULT hr=kind?device_create_index_buffer(&d,16,0,valid?101:0,pool,&out,&shared):device_create_vertex_buffer(&d,16,0,0,pool,&out,&shared);
 CHECK(hr==E_NOTIMPL&&out==(void*)(uintptr_t)99&&!allocations&&!frees&&!creates&&!handles&&!d.pins&&!live);cases++;
}
Device good={.api=8},wrong={.api=9};for(UINT kind=0;kind<2;kind++)for(UINT bi=0;bi<3;bi++){UINT bad=bi==0?1:bi==1?0:2;Iface*out=(void*)(uintptr_t)99;allocations=0;CHECK((kind?d9wg_d3d9_core_index_buffer8(bad==0?NULL:bad==1?&wrong:&good,12,0,101,1,bad==2?NULL:&out):d9wg_d3d9_core_vertex_buffer8(bad==0?NULL:bad==1?&wrong:&good,80,0,44,1,bad==2?NULL:&out))==-1&&!allocations&&(bad==2||!out));cases++;}
printf("PASS actual backend8 VB/IB creation: %u checks,%u cases (%u API8/%u ordinary9),%u zeroed bytes;frozen8/9 allocation/descriptor oracle plus Wine pool/usage expectations and 16-byte buffer storage alignment,realSDK24/20-byte layouts,unaligned indices/pools/length/usage/FVF/output/OOM/transport/zero storage/list/pins,private sharedNULL dispatch\\n",checks,cases,api8cases,api9cases,zeroed);return 0;}
`;fs.writeFileSync(dir+"/buffers.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/buffers.c","-o",dir+"/buffers"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/buffers",[],{encoding:"utf8",timeout:180000});assert.equal(r.status,0,r.stdout+r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
