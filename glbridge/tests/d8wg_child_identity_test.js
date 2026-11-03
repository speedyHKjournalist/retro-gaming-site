'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d8wg-child-identity-')),source=fs.readFileSync(process.env.D8WG_IDENTITY_FRONTEND||root+'/d3d8proxy/d3d8_proxy.c','utf8');
function extract(name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let end=source.indexOf('{',m.index)+1,depth=1;for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}return source.slice(m.index,end);}
try{fs.writeFileSync(dir+'/identity.c',`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "d3d9proxy/d3d9_protocol.h"
#include "runtime/d9wg_private_data.h"
typedef uint32_t ULONG,UINT,DWORD;typedef int32_t LONG,HRESULT;typedef int BOOL,D3DCUBEMAP_FACES;typedef const int*REFIID;
#define WINAPI
#define D3D_OK 0
#define S_OK 0
#define D3DERR_INVALIDCALL (-1)
#define E_OUTOFMEMORY (-2)
#define E_NOINTERFACE (-3)
#define E_POINTER (-4)
#define D3DUSAGE_DEPTHSTENCIL 2
#define HEAP_ZERO_MEMORY 1
#define D8WG_TRACE_ERROR(v) (v)
typedef struct Interface Interface;typedef struct Vtable{HRESULT(*QueryInterface)(Interface*,REFIID,void**);ULONG(*AddRef)(Interface*);ULONG(*Release)(Interface*);}Vtable;
struct Interface{const Vtable*lpVtbl;};
typedef Interface IDirect3DTexture8,IDirect3DCubeTexture8,IDirect3DVolumeTexture8,IDirect3DSurface8,IDirect3DVolume8;
#define IDirect3DTexture8_AddRef(p) ((p)->lpVtbl->AddRef(p))
#define IDirect3DTexture8_Release(p) ((p)->lpVtbl->Release(p))
#define IDirect3DCubeTexture8_AddRef IDirect3DTexture8_AddRef
#define IDirect3DCubeTexture8_Release IDirect3DTexture8_Release
#define IDirect3DVolumeTexture8_AddRef IDirect3DTexture8_AddRef
#define IDirect3DVolumeTexture8_Release IDirect3DTexture8_Release
#define IDirect3DSurface8_AddRef IDirect3DTexture8_AddRef
#define IDirect3DVolume8_AddRef IDirect3DTexture8_AddRef
typedef struct Texture Texture;typedef struct Surface Surface;typedef struct Volume Volume;typedef struct{Texture*texture_resources,*cube_resources,*volume_resources;unsigned refs,implicit_surface_count;}Device;
typedef struct{D9WGPrivateStore private_data;void*shadow;Surface*level_surface;Volume*level_volume;}D8TextureLevel;
struct Texture{Interface iface;LONG refcount;Device*device;Texture*next_device_resource;UINT handle,level_count,usage;BOOL private_back_buffer;D9WGPrivateStore private_data;D8TextureLevel*levels;};
struct Surface{Interface iface;LONG refcount;Texture*texture,*cube;Device*device;UINT level,face;BOOL depth_stencil,reset_blocker,additional_back_buffer;void*shadow,*retained_private_data;D9WGPrivateStore private_data;};
struct Volume{Interface iface;LONG refcount;Texture*texture;UINT level;};
typedef Device D8Device;
typedef Texture D8Texture,D8CubeTexture,D8VolumeTexture;typedef Surface D8Surface;typedef Volume D8Volume;
static Texture*texture_from_iface(Interface*p){return (Texture*)p;}static Texture*cube_from_iface(Interface*p){return (Texture*)p;}static Texture*volume_texture_from_iface(Interface*p){return (Texture*)p;}static Surface*surface_from_iface(Interface*p){return (Surface*)p;}static Volume*volume_from_iface(Interface*p){return (Volume*)p;}
static Vtable g_surface_vtbl,g_volume_vtbl;static const int IID_IDirect3DResource8=3,IID_IDirect3DSurface8=1,IID_IDirect3DVolume8=2,unknown=0,bad_iid=99;
static int iid_is_unknown(REFIID p){return p&&!*p;}static int guid_equal(REFIID a,REFIID b){return a&&b&&*a==*b;}
static const D9WGPrivateOps d9wg_private_ops={0};static unsigned allocations,live,fail_at,destroys;static UINT expected_handle,expected_kind;
static void*GetProcessHeap(void){return NULL;}static void*HeapAlloc(void*h,UINT flags,size_t size){(void)h;(void)flags;allocations++;if(allocations==fail_at)return NULL;void*p=calloc(1,size);assert(p);live++;return p;}
static void HeapFree(void*h,UINT flags,void*p){(void)h;(void)flags;if(p){assert(live);live--;free(p);}}
static LONG InterlockedIncrement(LONG*p){return ++*p;}static LONG InterlockedDecrement(LONG*p){return --*p;}
static BOOL surface_is_implicit(Surface*s){(void)s;return 0;}
static void device_child_add_ref(Device*d){(void)d;assert(0);}
static LONG InterlockedCompareExchange(LONG*p,LONG value,LONG compare){LONG old=*p;if(old==compare)*p=value;return old;}
static void device_child_release(Device*d){assert(d->refs==2);d->refs--;}
static int emit_command(UINT op,const void*p,UINT size){const D9WGDestroyResource*c=p;assert(op==D9WG_OP_DESTROY_RESOURCE&&size==sizeof(*c)&&c->resource_handle==expected_handle&&c->resource_kind==expected_kind);destroys++;return 1;}
${['texture_add_ref','texture_release','cube_add_ref','cube_release','volume_texture_add_ref','volume_texture_release','texture_get_surface_level','cube_get_surface','volume_texture_get_volume_level','surface_query_interface','surface_add_ref','surface_release','volume_query_interface','volume_add_ref','volume_release'].map(extract).join('\n')}
static Vtable parents[3];
static HRESULT get_child(Texture*t,UINT kind,UINT index,Interface**out){return kind==0?texture_get_surface_level(&t->iface,index,out):kind==1?cube_get_surface(&t->iface,(int)(index/2),index%2,out):volume_texture_get_volume_level(&t->iface,index,out);}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"D8 child identity line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){unsigned cases=0;g_surface_vtbl=(Vtable){surface_query_interface,surface_add_ref,surface_release};g_volume_vtbl=(Vtable){volume_query_interface,volume_add_ref,volume_release};parents[0]=(Vtable){NULL,texture_add_ref,texture_release};parents[1]=(Vtable){NULL,cube_add_ref,cube_release};parents[2]=(Vtable){NULL,volume_texture_add_ref,volume_texture_release};
for(UINT kind=0;kind<3;kind++){Device device={.refs=2};Texture*t=HeapAlloc(NULL,0,sizeof(*t));t->iface.lpVtbl=&parents[kind];t->refcount=1;t->device=&device;t->handle=expected_handle=100+kind;expected_kind=3+kind;t->level_count=2;t->usage=kind?0:2;UINT count=kind==1?12:2;t->levels=HeapAlloc(NULL,0,count*sizeof(*t->levels));if(!kind)device.texture_resources=t;else if(kind==1)device.cube_resources=t;else device.volume_resources=t;for(UINT i=0;i<count;i++)t->levels[i].shadow=HeapAlloc(NULL,0,4);
Interface*out=(void*)(uintptr_t)1;unsigned before=allocations;CHECK(get_child(t,kind,0,NULL)==-1&&allocations==before&&t->refcount==1);CHECK(get_child(t,kind,count,&out)==-1&&!out&&allocations==before&&t->refcount==1);cases++;
fail_at=allocations+1;out=(void*)(uintptr_t)1;CHECK(get_child(t,kind,0,&out)==-2&&!out&&t->refcount==1&&!t->levels[0].level_surface&&!t->levels[0].level_volume);fail_at=0;cases++;
Interface*saved[12]={0};for(UINT i=0;i<count;i++){Interface*a=NULL,*b=NULL;CHECK(!get_child(t,kind,i,&a)&&a&&t->refcount==2);saved[i]=a;before=allocations;CHECK(!get_child(t,kind,i,&b)&&a==b&&t->refcount==3&&allocations==before);cases++;
CHECK(a->lpVtbl->AddRef(a)==4&&t->refcount==4);CHECK(b->lpVtbl->Release(b)==3&&t->refcount==3);CHECK(a->lpVtbl->Release(a)==2&&t->refcount==2);cases++;
void*q=NULL;CHECK(!a->lpVtbl->QueryInterface(a,&unknown,&q)&&q==a&&t->refcount==3);CHECK(((Interface*)q)->lpVtbl->Release(q)==2);q=(void*)(uintptr_t)1;CHECK(a->lpVtbl->QueryInterface(a,&bad_iid,&q)==-3&&!q&&t->refcount==2);cases++;
if(kind==2){Volume*v=(Volume*)a;CHECK(v->texture==t&&v->level==i);}else{Surface*v=(Surface*)a;CHECK(v->device==&device&&v->level==i%2&&v->face==(kind==1?i/2:0)&&v->depth_stencil==(kind==0));}cases++;
CHECK(a->lpVtbl->Release(a)==1&&t->refcount==1);fail_at=allocations+1;CHECK(!get_child(t,kind,i,&a)&&a==saved[i]&&allocations+1==fail_at&&t->refcount==2);fail_at=0;CHECK(a->lpVtbl->Release(a)==1);cases++;
for(UINT j=0;j<i;j++)CHECK(saved[i]!=saved[j]);cases++;
}
CHECK(device.refs==2);CHECK(!get_child(t,kind,0,&out)&&out==saved[0]&&t->refcount==2);CHECK(t->iface.lpVtbl->Release(&t->iface)==1);CHECK(out->lpVtbl->AddRef(out)==2);CHECK(out->lpVtbl->Release(out)==1);CHECK(out->lpVtbl->Release(out)==0&&!live&&device.refs==1&&destroys==kind+1);cases++;
}printf("PASS actual D3D8 contained identity: %u observations, 16 mip/face/volume views, shared parent/child COM counts, stable live/reacquired/IUnknown identity, failed allocation atomicity, last-child teardown and no cached-view leaks\\n",cases);return 0;}
`);const b=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-I',root,dir+'/identity.c',root+'/runtime/d9wg_private_data.c','-o',dir+'/identity'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+'/identity',[],{encoding:'utf8',timeout:10000});assert.equal(r.status,0,r.stderr+' signal='+r.signal);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
