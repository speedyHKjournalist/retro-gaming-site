"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-private-entry-"));
function extract(source,name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
try{for(const api of[8,9]){const source=fs.readFileSync(process.env['D'+api+'WG_PRIVATE_SOURCE']||root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8');fs.writeFileSync(dir+'/entry.c',`#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
#include "runtime/d9wg_private_data.h"
typedef uint32_t DWORD,ULONG;typedef int32_t HRESULT;typedef const void*REFGUID;
#define WINAPI
#define D3D_OK 0
#define D3DERR_NOTFOUND ((HRESULT)0x88760866u)
#define D3DERR_MOREDATA ((HRESULT)0x88760867u)
#define E_OUTOFMEMORY ((HRESULT)0x8007000eu)
#define D3DERR_INVALIDCALL ((HRESULT)0x8876086cu)
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE_REFUSE(v) (v)
typedef struct Buffer Buffer;typedef struct{Buffer*vertex_buffers,*index_buffers;unsigned refs;}Device;
struct Buffer{Device*device;Buffer*next_device_resource;int32_t refcount;uint32_t handle;void*shadow;int clips;D9WGPrivateStore private_data;};
typedef Buffer D${api}VertexBuffer,D${api}IndexBuffer,IDirect3DVertexBuffer${api},IDirect3DIndexBuffer${api};
static Buffer*vb_from_iface(Buffer*b){return b;}static Buffer*ib_from_iface(Buffer*b){return b;}
static unsigned allocated,destroys,clips_freed,held,fail;static int g_transport_lock;
static void*GetProcessHeap(void){return NULL;}
static void*HeapAlloc(void*h,unsigned flags,uint32_t size){(void)h;(void)flags;assert(!held);if(fail)return NULL;void*p=calloc(1,size);assert(p);allocated++;return p;}
static void HeapFree(void*h,unsigned flags,void*p){(void)h;(void)flags;assert(!held&&p&&allocated);allocated--;free(p);}
static void EnterCriticalSection(int*lock){(void)lock;assert(!held);held=1;}
static void LeaveCriticalSection(int*lock){(void)lock;assert(held);held=0;}
typedef struct{unsigned refs;Device*device;Buffer*buffer;int check_destroy;}IUnknown;
static unsigned IUnknown_AddRef(IUnknown*o){assert(!held&&o->refs);return ++o->refs;}
static unsigned IUnknown_Release(IUnknown*o){assert(!held&&o->refs);if(o->check_destroy){assert(o->device->refs==2);assert(o->device->vertex_buffers!=o->buffer&&o->device->index_buffers!=o->buffer);}return --o->refs;}
static int32_t InterlockedDecrement(int32_t*p){return --*p;}
static void device_child_release(Device*d){assert(d->refs==2);d->refs--;}
#define d9wg_clip_buffer_free(a,b,c) do{(void)(a);clips_freed++;}while(0)
static int emit_command(uint16_t op,const void*p,uint32_t size){assert(op==D9WG_OP_DESTROY_RESOURCE&&size==sizeof(D9WGDestroyResource));const D9WGDestroyResource*c=p;assert(c->resource_handle==0x7000+destroys&&c->resource_kind==(destroys%2?D9WG_RESOURCE_BUFFER_INDEX:D9WG_RESOURCE_BUFFER_VERTEX));destroys++;return 1;}
#include "runtime/d9wg_private_data_bridge.h"
${['vb_set_private_data','vb_get_private_data','vb_free_private_data','vb_release','ib_set_private_data','ib_get_private_data','ib_free_private_data','ib_release'].map(n=>extract(source,n)).join('\n')}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Private entry API ${api} line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){unsigned cases=0;Device d={.refs=1};const uint8_t key[16]={1},missing[16]={2};HRESULT(*set[2])(Buffer*,REFGUID,const void*,DWORD,DWORD)={vb_set_private_data,ib_set_private_data};HRESULT(*get[2])(Buffer*,REFGUID,void*,DWORD*)={vb_get_private_data,ib_get_private_data};HRESULT(*drop[2])(Buffer*,REFGUID)={vb_free_private_data,ib_free_private_data};ULONG(*release_buffer[2])(Buffer*)={vb_release,ib_release};
for(unsigned kind=0;kind<2;kind++){Buffer*b=HeapAlloc(NULL,0,sizeof(*b));b->device=&d;b->refcount=2;b->handle=0x7000+kind;b->shadow=HeapAlloc(NULL,0,64);d.refs++;if(kind)d.index_buffers=b;else d.vertex_buffers=b;
DWORD size=0xaabbccdd,out=0xdeadbeef,value=0x1234abcd;CHECK(get[kind](b,missing,&out,&size)==D3DERR_NOTFOUND&&size==0xaabbccdd&&out==0xdeadbeef);CHECK(get[kind](b,missing,NULL,NULL)==D3DERR_NOTFOUND&&drop[kind](b,missing)==D3DERR_NOTFOUND);cases++;
CHECK(!set[kind](b,key,&value,4,0));value=0;size=1;CHECK(!get[kind](b,key,NULL,&size)&&size==4);size=3;CHECK(get[kind](b,key,&out,&size)==D3DERR_MOREDATA&&size==4&&out==0xdeadbeef);size=4;CHECK(!get[kind](b,key,&out,&size)&&out==0x1234abcd);cases++;
fail=1;CHECK(set[kind](b,key,&value,4,0)==E_OUTOFMEMORY);fail=0;CHECK(!get[kind](b,key,&out,&size)&&out==0x1234abcd);CHECK(set[kind](b,key,NULL,4,0)==D3DERR_INVALIDCALL);CHECK(!drop[kind](b,key)&&drop[kind](b,key)==D3DERR_NOTFOUND);cases++;
IUnknown o={.refs=1,.device=&d,.buffer=b};CHECK(!set[kind](b,key,&o,sizeof(void*),1)&&o.refs==2);CHECK(set[kind](b,key,&o,0,1)==D3DERR_INVALIDCALL&&o.refs==2);size=1;CHECK(!get[kind](b,key,NULL,&size)&&size==sizeof(void*)&&o.refs==2);IUnknown*pointer=NULL;size=sizeof(pointer);CHECK(!get[kind](b,key,&pointer,&size)&&pointer==&o&&o.refs==3);IUnknown_Release(pointer);cases++;
CHECK(!set[kind](b,key,&o,sizeof(void*),1)&&o.refs==2);CHECK(!drop[kind](b,key)&&o.refs==1);CHECK(!set[kind](b,key,&o,sizeof(void*),1)&&o.refs==2);cases++;
o.check_destroy=1;CHECK(release_buffer[kind](b)==1&&o.refs==2&&destroys==kind);if(kind)CHECK(d.index_buffers==b);else CHECK(d.vertex_buffers==b);cases++;
CHECK(!release_buffer[kind](b)&&o.refs==1&&d.refs==1&&destroys==kind+1&&clips_freed==1&&!allocated&&!d.vertex_buffers&&!d.index_buffers);cases++;}
printf("PASS actual API ${api} private data: %u VB/IB wrapper, HRESULT, atomic failure, interface reference, final Release and unlink-before-callback cases\\n",cases);return 0;}
`);let b=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-I',root,dir+'/entry.c',process.env.D9WG_PRIVATE_STORE_SOURCE||root+'/runtime/d9wg_private_data.c','-o',dir+'/entry'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);let r=spawnSync(dir+'/entry',[],{encoding:'utf8',timeout:20000});assert.equal(r.status,0,r.stderr+' signal='+r.signal);process.stdout.write(r.stdout);}}finally{fs.rmSync(dir,{recursive:true,force:true});}
