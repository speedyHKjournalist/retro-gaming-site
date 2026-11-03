"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-private-entry-"));
function extract(source,name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
try{for(const api of[8,9]){const source=fs.readFileSync(process.env['D'+api+'WG_PRIVATE_TEXTURE_SOURCE']||root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8');fs.writeFileSync(dir+'/entry.c',`#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
#include "runtime/d9wg_private_data.h"
typedef uint32_t DWORD,ULONG,UINT;typedef int32_t HRESULT;typedef const void*REFGUID;
#define WINAPI
#define D3D_OK 0
#define D3DERR_NOTFOUND ((HRESULT)0x88760866u)
#define D3DERR_MOREDATA ((HRESULT)0x88760867u)
#define E_OUTOFMEMORY ((HRESULT)0x8007000eu)
#define D3DERR_INVALIDCALL ((HRESULT)0x8876086cu)
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE_REFUSE(v) (v)
#define TRACE_SURFACE_FREED(v) ((void)(v))
typedef struct Texture Texture;typedef struct View View;typedef struct{void*lpVtbl;}Interface;
typedef struct{Texture*texture_resources,*cube_resources,*cube_textures,*volume_resources,*volume_textures;unsigned refs;}Device;
typedef struct{D9WGPrivateStore private_data;View*level_surface;View*level_volume;void*shadow;}Level;
struct Texture{Interface iface;Device*device;Texture*next_device_resource;int32_t refcount;uint32_t handle,level_count;int private_back_buffer;D9WGPrivateStore private_data;Level*levels;};
struct View{Interface iface;Texture*texture,*cube,*cube_texture;unsigned level,face,cube_face;int texture_child;D9WGPrivateStore private_data;D9WGPrivateStore*private_data_owner;};
typedef Texture D${api}Texture,D${api}CubeTexture,D${api}VolumeTexture,IDirect3DTexture${api},IDirect3DCubeTexture${api},IDirect3DVolumeTexture${api};
typedef View D${api}Surface,D${api}Volume,IDirect3DSurface${api},IDirect3DVolume${api};
static Texture*texture_from_iface(Texture*t){return t;}static Texture*cube_from_iface(Texture*t){return t;}static Texture*volume_texture_from_iface(Texture*t){return t;}static View*surface_from_iface(View*v){return v;}static View*volume_from_iface(View*v){return v;}
static unsigned allocated,destroys,held;static int g_transport_lock;static Device device;
static void*GetProcessHeap(void){return NULL;}
static void*HeapAlloc(void*h,unsigned flags,uint32_t size){(void)h;(void)flags;assert(!held);void*p=calloc(1,size);assert(p);allocated++;return p;}
static void HeapFree(void*h,unsigned flags,void*p){(void)h;(void)flags;assert(!held);if(!p)return;assert(allocated);allocated--;free(p);}
static void EnterCriticalSection(int*lock){(void)lock;assert(!held);held=1;}
static void LeaveCriticalSection(int*lock){(void)lock;assert(held);held=0;}
typedef struct{unsigned refs;Texture*texture;int check_destroy;}IUnknown;
static unsigned IUnknown_AddRef(IUnknown*o){assert(!held&&o->refs);return ++o->refs;}
static unsigned IUnknown_Release(IUnknown*o){assert(!held&&o->refs);if(o->check_destroy){assert(device.refs==2);assert(device.texture_resources!=o->texture&&device.cube_${api===8?'resources':'textures'}!=o->texture&&device.volume_${api===8?'resources':'textures'}!=o->texture);}return --o->refs;}
static int32_t InterlockedDecrement(int32_t*p){return --*p;}
static void device_child_release(Device*d){assert(d->refs==2);d->refs--;}
static int emit_command(uint16_t op,const void*p,uint32_t size){assert(op==D9WG_OP_DESTROY_RESOURCE&&size==sizeof(D9WGDestroyResource));const D9WGDestroyResource*c=p;assert(c->resource_handle==0x8000+destroys&&c->resource_kind==3+destroys);destroys++;return 1;}
#include "runtime/d9wg_private_data_bridge.h"
${['surface_private_store',...['texture','cube','volume_texture','surface','volume'].flatMap(k=>['set','get','free'].map(m=>k+'_'+m+'_private_data')),'texture_release','cube_release','volume_texture_release'].map(n=>extract(source,n)).join('\n')}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Private texture API ${api} line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){unsigned cases=0;const uint8_t key[16]={1},object_key[16]={2};HRESULT(*set[3])(Texture*,REFGUID,const void*,DWORD,DWORD)={texture_set_private_data,cube_set_private_data,volume_texture_set_private_data};HRESULT(*get[3])(Texture*,REFGUID,void*,DWORD*)={texture_get_private_data,cube_get_private_data,volume_texture_get_private_data};HRESULT(*drop[3])(Texture*,REFGUID)={texture_free_private_data,cube_free_private_data,volume_texture_free_private_data};ULONG(*release_texture[3])(Texture*)={texture_release,cube_release,volume_texture_release};
for(unsigned kind=0;kind<3;kind++){Texture*t=HeapAlloc(NULL,0,sizeof(*t));t->device=&device;t->refcount=2;t->handle=0x8000+kind;t->level_count=2;unsigned count=kind==1?12:2;device.refs=2;if(!kind)device.texture_resources=t;else if(kind==1)device.cube_${api===8?'resources':'textures'}=t;else device.volume_${api===8?'resources':'textures'}=t;t->levels=HeapAlloc(NULL,0,count*sizeof(Level));for(unsigned i=0;i<count;i++){t->levels[i].shadow=HeapAlloc(NULL,0,4);if(kind==2)t->levels[i].level_volume=HeapAlloc(NULL,0,sizeof(View));else t->levels[i].level_surface=HeapAlloc(NULL,0,sizeof(View));}
IUnknown object={.refs=1,.texture=t};DWORD value=0x10203040,size=4,out=0;CHECK(!set[kind](t,key,&value,4,0));CHECK(!set[kind](t,object_key,&object,sizeof(void*),1)&&object.refs==2);CHECK(!get[kind](t,key,&out,&size)&&out==value);cases++;
for(unsigned i=0;i<count;i++){View v={.texture=kind==1?NULL:t,.cube=kind==1?t:NULL,.cube_texture=kind==1?t:NULL,.level=i%2,.face=i/2,.cube_face=i/2,.texture_child=kind==0};HRESULT(*cs)(View*,REFGUID,const void*,DWORD,DWORD)=kind==2?volume_set_private_data:surface_set_private_data;HRESULT(*cg)(View*,REFGUID,void*,DWORD*)=kind==2?volume_get_private_data:surface_get_private_data;HRESULT(*cf)(View*,REFGUID)=kind==2?volume_free_private_data:surface_free_private_data;
size=4;out=0xdeadbeef;CHECK(cg(&v,key,&out,&size)==D3DERR_NOTFOUND&&out==0xdeadbeef&&size==4);value=0x33445500+i;CHECK(!cs(&v,key,&value,4,0));CHECK(!cs(&v,object_key,&object,sizeof(void*),1)&&object.refs==3+i);View duplicate=v;size=1;CHECK(!cg(&duplicate,key,NULL,&size)&&size==4);size=3;CHECK(cg(&duplicate,key,&out,&size)==D3DERR_MOREDATA&&size==4&&out==0xdeadbeef);size=4;CHECK(!cg(&duplicate,key,&out,&size)&&out==value);IUnknown*pointer=NULL;size=sizeof(pointer);CHECK(!cg(&duplicate,object_key,&pointer,&size)&&pointer==&object&&object.refs==4+i);IUnknown_Release(pointer);CHECK(!cf(&v,key)&&cf(&v,key)==D3DERR_NOTFOUND);CHECK(!cs(&v,key,&value,4,0));cases++;}
size=4;CHECK(!get[kind](t,key,&out,&size)&&out==0x10203040);CHECK(!drop[kind](t,key)&&drop[kind](t,key)==D3DERR_NOTFOUND);for(unsigned i=0;i<count;i++){View v={.texture=kind==1?NULL:t,.cube=kind==1?t:NULL,.cube_texture=kind==1?t:NULL,.level=i%2,.face=i/2,.cube_face=i/2,.texture_child=kind==0};size=4;CHECK(!(kind==2?volume_get_private_data(&v,key,&out,&size):surface_get_private_data(&v,key,&out,&size))&&out==0x33445500+i);}cases++;
object.check_destroy=1;CHECK(release_texture[kind](t)==1&&object.refs==2+count&&destroys==kind);CHECK(!release_texture[kind](t)&&object.refs==1&&device.refs==1&&destroys==kind+1&&!allocated);cases++;}
View unsupported={0};DWORD size=4,out=0xdeadbeef;CHECK(!surface_set_private_data(&unsupported,key,&out,4,0)&&!surface_get_private_data(&unsupported,key,&out,&size)&&out==0xdeadbeef&&size==4&&!surface_free_private_data(&unsupported,key)&&!allocated);cases++;
printf("PASS actual API ${api} texture private data: %u parent/2D/cube-face-mip/volume-level isolation, duplicate views, refs and actual final teardown cases\\n",cases);return 0;}
`);const b=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-I',root,dir+'/entry.c',root+'/runtime/d9wg_private_data.c','-o',dir+'/entry'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+'/entry',[],{encoding:'utf8',timeout:20000});assert.equal(r.status,0,r.stderr+' signal='+r.signal);process.stdout.write(r.stdout);}}finally{fs.rmSync(dir,{recursive:true,force:true});}
