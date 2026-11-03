"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-private-entry-"));
function extract(source,name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
try{for(const api of[8,9]){const source=fs.readFileSync(process.env['D'+api+'WG_PRIVATE_SURFACE_SOURCE']||root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8');fs.writeFileSync(dir+'/entry.c',`#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
#include "runtime/d9wg_private_data.h"
#include "runtime/d9wg_present_pacing.h"
typedef void*HWND;typedef struct{int left,top,right,bottom;}RECT;
typedef uint32_t DWORD,ULONG,UINT;typedef int32_t LONG,HRESULT;typedef uint8_t BYTE;typedef int BOOL,D3DFORMAT,D3DBACKBUFFER_TYPE;typedef const void*REFGUID;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define SUCCEEDED(hr) ((hr)>=0)
#define HEAP_ZERO_MEMORY 8
#define D3D_OK 0
#define D3DERR_NOTFOUND ((HRESULT)0x88760866u)
#define D3DERR_MOREDATA ((HRESULT)0x88760867u)
#define E_OUTOFMEMORY ((HRESULT)0x8007000eu)
#define D3DERR_INVALIDCALL ((HRESULT)0x8876086cu)
#define D3DERR_NOTAVAILABLE ((HRESULT)0x8876086au)
#define D3DPOOL_DEFAULT 0
#define D3DUSAGE_RENDERTARGET 1
#define D3DFMT_UNKNOWN 0
#define D3DBACKBUFFER_TYPE_MONO 0
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
#define TRACE_MARK_ENTER(...)
#define TRACE_MARK_EXIT(...)
#define TRACE_FLUSH(...)
#define TRACE_SURFACE_FREED(...)
typedef struct Texture Texture;typedef struct View View;typedef struct Device Device;typedef struct{void*lpVtbl;}Interface;
typedef struct{DWORD Width,Height;D3DFORMAT Format;}Mode;
typedef struct{HWND hDeviceWindow;BOOL Windowed;UINT FullScreen_PresentationInterval,MultiSampleType,BackBufferCount,BackBufferWidth,BackBufferHeight;D3DFORMAT BackBufferFormat;}D3DPRESENT_PARAMETERS;
#include "runtime/d9wg_present_parameters.h"
typedef struct{Interface iface;}Parent;
struct Device{Interface iface;LONG refcount,child_parent_refs;BOOL releasing_owned_refs;D9WGPrivateStore back_buffer_private_data,depth_surface_private_data;UINT implicit_surface_count,additional_swap_chain_count;View*implicit_back_buffer,*implicit_depth_stencil;Parent*parent;void*vertex_shaders,*pixel_shaders,*extra_lights,*front_shadow;int palettes;uint32_t handle;Mode display_mode;D3DPRESENT_PARAMETERS present;struct{HWND hFocusWindow;}creation;};
typedef struct{D9WGPrivateStore private_data;}Level;
struct Texture{Interface iface;Level*levels;unsigned level_count;};
typedef struct D8PrivateDataOwner{LONG refs;D9WGPrivateStore store;}D8PrivateDataOwner;
struct View{Interface iface;LONG refcount;Device*device;Texture*texture,*cube,*cube_texture;unsigned level,face,cube_face;BOOL texture_child,reset_blocker,implicit;D9WGPrivateStore private_data;D9WGPrivateStore*private_data_owner;D8PrivateDataOwner*retained_private_data;void*shadow,*dc,*dc_previous,*dib;struct{UINT MultiSampleType;}desc;};
typedef struct{Interface iface;LONG refcount;D8PrivateDataOwner*private_data_owner;Device*device;D3DPRESENT_PARAMETERS present;}D8SwapChain;
typedef Device D${api}Device,IDirect3DDevice${api};typedef Texture D${api}Texture,D${api}CubeTexture;typedef View D${api}Surface,IDirect3DSurface${api};typedef Interface IDirect3DSwapChain8;
static Device*device_from_iface(Device*d){return d;}static View*surface_from_iface(View*v){return v;}
static unsigned allocated,held,allocation_calls,fail_nth,fail_create,parent_refs=1;static int g_transport_lock;
${api===8?`static void SetRect(RECT*r,int a,int b,int c,int d){*r=(RECT){a,b,c,d};}
static void GetClientRect(HWND h,RECT*r){(void)h;SetRect(r,0,0,64,64);}`:""}
static void*GetProcessHeap(void){return NULL;}
static void*HeapAlloc(void*h,unsigned flags,uint32_t size){(void)h;(void)flags;assert(!held);allocation_calls++;if(fail_nth&&allocation_calls==fail_nth)return NULL;void*p=calloc(1,size);assert(p);allocated++;return p;}
static void HeapFree(void*h,unsigned flags,void*p){(void)h;(void)flags;assert(!held);if(!p)return;assert(allocated);allocated--;free(p);}
static void EnterCriticalSection(int*lock){(void)lock;assert(!held);held=1;}
static void LeaveCriticalSection(int*lock){(void)lock;assert(held);held=0;}
typedef struct{unsigned refs;}IUnknown;
static unsigned IUnknown_AddRef(IUnknown*o){assert(!held&&o->refs);return ++o->refs;}
static unsigned IUnknown_Release(IUnknown*o){assert(!held&&o->refs&&parent_refs==1);return --o->refs;}
static LONG InterlockedDecrement(LONG*p){return --*p;}static LONG InterlockedCompareExchange(LONG*p,LONG exchange,LONG compare){LONG old=*p;if(old==compare)*p=exchange;return old;}
static LONG InterlockedIncrement(LONG*p){return ++*p;}
static void device_child_add_ref(Device*d){d->refcount++;d->child_parent_refs++;}
static ULONG surface_add_ref(View*);
#define IDirect3DSurface${api}_AddRef(p) surface_add_ref(p)
#define IDirect3DTexture${api}_AddRef(p) ((void)(p),0u)
#define IDirect3DCubeTexture${api}_AddRef(p) ((void)(p),0u)
static void device_child_release(Device*d){assert(d->refcount>1&&d->child_parent_refs>0);d->refcount--;d->child_parent_refs--;}
static void device_release_owned_references(Device*d){(void)d;}
static void parent_release(void*p){(void)p;assert(parent_refs==1);parent_refs=0;}
#define IDirect3D${api}_Release(p) parent_release(p)
#define IDirect3DTexture${api}_Release(p) ((void)(p),0u)
#define IDirect3DCubeTexture${api}_Release(p) ((void)(p),0u)
#include "runtime/d9wg_private_data_bridge.h"
${api===8?`static int g_swapchain_vtbl;
static BOOL supported_multisample_type(UINT v){return v==0||v==4;}
static BOOL supported_backbuffer_format(D3DFORMAT v){return v==21||v==22;}
static void detach_device_window(Device*d){(void)d;}static void restore_display_mode(Device*d){(void)d;}
static void emit_device_destroy_and_flush(uint32_t h){assert(h==0x9000);}
static void free_shader_list(void*p){(void)p;}static void free_extra_lights(void**p){(void)p;}
static HRESULT create_standalone_surface(Device*d,UINT width,UINT height,D3DFORMAT format,DWORD usage,int pool,BOOL lockable,BOOL backbuffer,BOOL depth,BOOL blocker,View**out){(void)lockable;(void)depth;assert(width==64&&height==64&&format==22&&usage==1&&pool==0&&backbuffer);*out=NULL;if(fail_create)return E_OUTOFMEMORY;View*v=HeapAlloc(NULL,0,sizeof(*v));if(!v)return E_OUTOFMEMORY;v->device=d;v->refcount=1;v->reset_blocker=blocker;if(blocker)d->implicit_surface_count++;device_child_add_ref(d);*out=v;return 0;}
${['surface_is_implicit','free_implicit_surfaces','private_data_owner_release','create_backbuffer_surface','device_get_backbuffer','device_create_swapchain','swapchain_get_backbuffer','swapchain_release'].map(n=>extract(source,n).replace(/&device->implicit_back_buffer->iface/g,'device->implicit_back_buffer')).join('\n')}`:`static BOOL surface_is_implicit(View*v){return v->implicit;}
static void SelectObject(void*a,void*b){(void)a;(void)b;}static void DeleteDC(void*p){(void)p;}static void DeleteObject(void*p){(void)p;}
static void d9wg_light_free(void**p){(void)p;}static void d9wg_palette_free(void*p){(void)p;}static void unwatch_window_state(Device*d){(void)d;}static void restore_display_mode(Device*d){(void)d;}
static unsigned g_command_count;
static int emit_command(uint16_t op,const void*p,uint32_t size){assert(op==D9WG_OP_DESTROY_RESOURCE&&size==sizeof(D9WGDestroyResource));const D9WGDestroyResource*c=p;assert(c->resource_handle==0x9000&&!c->resource_kind);return 1;}
static BOOL submit_batch_locked(BOOL present){(void)present;return 1;}`}
${['surface_private_store','surface_set_private_data','surface_get_private_data','surface_free_private_data','surface_add_ref','surface_release','device_release'].map(n=>extract(source,n)).join('\n')}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Private surface API ${api} line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){(void)fail_create;unsigned cases=0;Parent parent={0};Device*d=HeapAlloc(NULL,0,sizeof(*d));d->refcount=1;d->parent=&parent;d->handle=0x9000;d->display_mode=(Mode){64,64,22};d->present=(D3DPRESENT_PARAMETERS){.Windowed=1,.BackBufferWidth=64,.BackBufferHeight=64,.BackBufferFormat=22};const uint8_t key[16]={1},object_key[16]={2};DWORD data=0x10203040,size,out=0;IUnknown object={.refs=1};
for(unsigned i=0;i<3;i++){View*v=HeapAlloc(NULL,0,sizeof(*v));v->device=d;v->refcount=2;d->refcount++;d->child_parent_refs++;CHECK(!surface_set_private_data(v,key,&data,4,0));size=4;CHECK(!surface_get_private_data(v,key,&out,&size)&&out==data);CHECK(!surface_set_private_data(v,object_key,&object,sizeof(void*),1)&&object.refs==2);CHECK(surface_release(v)==1&&object.refs==2);CHECK(!surface_release(v)&&object.refs==1&&d->refcount==1);cases++;}
${api===8?`View*a=NULL,*b=NULL;CHECK(!device_get_backbuffer(d,0,0,&a));CHECK(!surface_set_private_data(a,key,&data,4,0));CHECK(!surface_set_private_data(a,object_key,&object,sizeof(void*),1)&&object.refs==2);CHECK(!surface_release(a)&&object.refs==2);CHECK(!device_get_backbuffer(d,0,0,&b));size=4;CHECK(!surface_get_private_data(b,key,&out,&size)&&out==data);CHECK(!surface_release(b)&&object.refs==2);cases++;
for(unsigned nth=1;nth<=2;nth++){IDirect3DSwapChain8*chain=(void*)1;unsigned before=allocated;allocation_calls=0;fail_nth=nth;CHECK(device_create_swapchain(d,&d->present,&chain)==E_OUTOFMEMORY&&!chain&&allocated==before&&d->refcount==1&&!d->additional_swap_chain_count);fail_nth=0;cases++;}
IDirect3DSwapChain8*chain_iface=NULL;CHECK(!device_create_swapchain(d,&d->present,&chain_iface));D8SwapChain*chain=(D8SwapChain*)chain_iface;CHECK(chain->refcount==1&&chain->private_data_owner->refs==1&&d->additional_swap_chain_count==1);fail_create=1;b=(void*)1;CHECK(swapchain_get_backbuffer(chain_iface,0,0,&b)==E_OUTOFMEMORY&&!b&&chain->private_data_owner->refs==1);fail_create=0;cases++;
CHECK(!swapchain_get_backbuffer(chain_iface,0,0,&a)&&!swapchain_get_backbuffer(chain_iface,0,0,&b)&&chain->refcount==1&&chain->private_data_owner->refs==3);DWORD different=0x55667788;size=4;CHECK(surface_get_private_data(a,key,&out,&size)==D3DERR_NOTFOUND);CHECK(!surface_set_private_data(a,key,&different,4,0));CHECK(!surface_set_private_data(a,object_key,&object,sizeof(void*),1)&&object.refs==3);size=4;CHECK(!surface_get_private_data(b,key,&out,&size)&&out==different);CHECK(!swapchain_release(chain_iface)&&object.refs==3&&!d->additional_swap_chain_count);size=4;CHECK(!surface_get_private_data(b,key,&out,&size)&&out==different);CHECK(!surface_release(a)&&object.refs==3);CHECK(!surface_release(b)&&object.refs==2&&d->refcount==1&&!d->implicit_surface_count);CHECK(!d9wg_private_set(&d->depth_surface_private_data,object_key,&object,sizeof(void*),1,&d9wg_private_ops)&&object.refs==3);cases++;`:`for(unsigned i=0;i<2;i++){View*v=HeapAlloc(NULL,0,sizeof(*v));v->device=d;v->implicit=1;v->refcount=1;d->refcount++;d->child_parent_refs++;if(i)d->implicit_depth_stencil=v;else d->implicit_back_buffer=v;CHECK(!surface_set_private_data(v,key,&data,4,0));CHECK(!surface_set_private_data(v,object_key,&object,sizeof(void*),1)&&object.refs==2+i);CHECK(!surface_release(v)&&object.refs==2+i);size=4;CHECK(!surface_get_private_data(v,key,&out,&size)&&out==data);CHECK(surface_add_ref(v)==1);CHECK(!surface_release(v)&&object.refs==2+i);cases++;}`}
View empty={0};size=4;CHECK(surface_get_private_data(&empty,key,&out,&size)==D3DERR_NOTFOUND&&surface_free_private_data(&empty,key)==D3DERR_NOTFOUND);cases++;
CHECK(d->refcount==1&&!d->child_parent_refs);CHECK(!device_release(d)&&object.refs==1&&!allocated&&!parent_refs);cases++;
printf("PASS actual API ${api} surface private data: %u standalone/implicit/store lifetime, final device cleanup${api===8?', chain-view pins with unchanged COM refs, early chain release and allocation failures':''} cases\\n",cases);return 0;}
`);const b=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-I',root,dir+'/entry.c',root+'/runtime/d9wg_private_data.c',root+'/runtime/d9wg_present_pacing.c','-o',dir+'/entry'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+'/entry',[],{encoding:'utf8',timeout:20000});assert.equal(r.status,0,r.stderr+' signal='+r.signal);process.stdout.write(r.stdout);}}finally{fs.rmSync(dir,{recursive:true,force:true});}
