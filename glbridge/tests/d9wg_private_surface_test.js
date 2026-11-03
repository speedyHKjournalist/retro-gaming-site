"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-private-entry-"));
function extract(source,name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);if(!m&&name==='surface_binding_add_ref')return 'static void surface_binding_add_ref(D8Surface*s){IDirect3DSurface8_AddRef(&s->iface);}';if(!m&&name==='surface_binding_release')return 'static void surface_binding_release(D8Surface*s){IDirect3DSurface8_Release(&s->iface);}';assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
const apis=process.env.D9WG_PRIVATE_API?[Number(process.env.D9WG_PRIVATE_API)]:[8,9];assert(apis.every(api=>api===8||api===9));
try{for(const api of apis){const source=fs.readFileSync(process.env['D'+api+'WG_PRIVATE_SURFACE_SOURCE']||root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8');fs.writeFileSync(dir+'/entry.c',`#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
#include "runtime/d9wg_private_data.h"
#include "runtime/d9wg_present_pacing.h"
typedef void*HWND;typedef struct{int left,top,right,bottom;}RECT;typedef struct{int x,y;}POINT;
typedef uint32_t DWORD,ULONG,UINT;typedef int32_t LONG,HRESULT;typedef uint8_t BYTE;typedef int BOOL,D3DFORMAT,D3DBACKBUFFER_TYPE;typedef const void*REFGUID;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define SUCCEEDED(hr) ((hr)>=0)
#define FAILED(hr) ((hr)<0)
#define ZeroMemory(p,n) memset(p,0,n)
#define D3DERR_DRIVERINTERNALERROR ((HRESULT)0x88760827u)
#define HEAP_ZERO_MEMORY 8
#define D3D_OK 0
#define D3DMULTISAMPLE_NONE 0
#define D3DERR_NOTFOUND ((HRESULT)0x88760866u)
#define D3DERR_MOREDATA ((HRESULT)0x88760867u)
#define E_OUTOFMEMORY ((HRESULT)0x8007000eu)
#define D3DERR_INVALIDCALL ((HRESULT)0x8876086cu)
#define D3DERR_NOTAVAILABLE ((HRESULT)0x8876086au)
#define D3DPOOL_DEFAULT 0
#define D3DUSAGE_RENDERTARGET 1
#define D3DUSAGE_DEPTHSTENCIL 2
#define D3DPRESENTFLAG_LOCKABLE_BACKBUFFER 1
#define D3DFMT_UNKNOWN 0
#define D3DBACKBUFFER_TYPE_MONO 0
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
#define TRACE_MARK_ENTER(...)
#define TRACE_MARK_EXIT(...)
#define TRACE_FLUSH(...)
#define TRACE_SURFACE_FREED(...)
typedef struct Texture Texture;typedef struct View View;typedef struct Device Device;typedef struct D9SwapChain D9SwapChain;typedef struct{void*lpVtbl;}Interface;
typedef struct{DWORD Width,Height;D3DFORMAT Format;}Mode;
typedef struct{HWND hDeviceWindow;BOOL Windowed;UINT FullScreen_PresentationInterval,MultiSampleType,BackBufferCount,BackBufferWidth,BackBufferHeight,SwapEffect,Flags;D3DFORMAT BackBufferFormat;}D3DPRESENT_PARAMETERS;
#include "runtime/d9wg_present_parameters.h"
typedef struct{Interface iface;}Parent;
struct Device{Interface iface;LONG refcount,child_parent_refs;BOOL releasing_owned_refs;D9WGPrivateStore back_buffer_private_data,depth_surface_private_data;UINT implicit_surface_count,additional_swap_chain_count;View*implicit_back_buffer,*implicit_depth_stencil;Parent*parent;void*vertex_shaders,*pixel_shaders,*extra_lights,*front_shadow;int palettes;uint32_t handle;Mode display_mode;D3DPRESENT_PARAMETERS present;struct{HWND hFocusWindow;}creation;Texture*texture_resources,*render_target_texture;View*render_target_surface,*render_target_surface_2,*depth_surface;D9SwapChain*additional_swap_chains;};
typedef struct{D9WGPrivateStore private_data;View*level_surface;void*shadow;}Level;typedef Level D8TextureLevel;
struct Texture{Interface iface;Level*levels;unsigned level_count,usage;LONG refcount,binding_refs;Device*device;Texture*next_device_resource;UINT handle;BOOL private_back_buffer;void*swap_chain;D9WGPrivateStore private_data;};
typedef struct D8PrivateDataOwner{LONG refs;D9WGPrivateStore store;}D8PrivateDataOwner;
struct View{Interface iface;LONG refcount,binding_refs;Device*device;Texture*texture,*cube,*cube_texture;unsigned level,face,cube_face;BOOL texture_child,reset_blocker,implicit,depth_stencil,additional_back_buffer,device_pin_transferred,device_container;D9SwapChain*swap_chain;D9WGPrivateStore private_data;D9WGPrivateStore*private_data_owner;D8PrivateDataOwner*retained_private_data;void*shadow,*dc,*dc_previous,*dib;struct{UINT MultiSampleType;}desc;};
typedef struct{Interface iface;LONG refcount;D8PrivateDataOwner*private_data_owner;Device*device;D3DPRESENT_PARAMETERS present;UINT handle;View*back_buffer;}D8SwapChain;
typedef Device D${api}Device,IDirect3DDevice${api};typedef Texture D${api}Texture,D${api}CubeTexture;typedef View D${api}Surface,IDirect3DSurface${api};typedef Interface IDirect3DSwapChain8,IDirect3DSwapChain9;
struct D9SwapChain{Interface iface;LONG refcount,binding_refs;Device*device;UINT handle;View*back_buffer;D9SwapChain*next_device_chain;};
static Device*device_from_iface(Device*d){return d;}static View*surface_from_iface(View*v){return v;}
${api===8?"static Device* target_device;static BOOL target_pin_guard;":""}
static unsigned allocated,held,allocation_calls,fail_nth,fail_create,parent_refs=1;static int g_transport_lock;
${api===8?`static void SetRect(RECT*r,int a,int b,int c,int d){*r=(RECT){a,b,c,d};}
static void GetClientRect(HWND h,RECT*r){(void)h;SetRect(r,0,0,64,64);}`:""}
static void*GetProcessHeap(void){return NULL;}
static void*HeapAlloc(void*h,unsigned flags,uint32_t size){(void)h;(void)flags;assert(!held);allocation_calls++;if(fail_nth&&allocation_calls==fail_nth)return NULL;void*p=calloc(1,size);assert(p);allocated++;return p;}
static void HeapFree(void*h,unsigned flags,void*p){(void)h;(void)flags;assert(!held);if(!p)return;assert(allocated);allocated--;free(p);}
static void EnterCriticalSection(int*lock){(void)lock;assert(!held);held=1;}
static void LeaveCriticalSection(int*lock){(void)lock;assert(held);held=0;}
static Device* callback_device;static unsigned callback_calls;
static ULONG device_add_ref(Device*);static ULONG device_release(Device*);
typedef struct{unsigned refs;}IUnknown;
static BOOL callback_keep_device;static D9WGPrivateStore* callback_readd_store;static IUnknown* callback_readd_object;
static void reenter_private_store(void);
static unsigned IUnknown_AddRef(IUnknown*o){assert(!held&&o->refs);return ++o->refs;}
static unsigned IUnknown_Release(IUnknown*o){assert(!held&&o->refs&&parent_refs==1);unsigned refs=--o->refs;if(!refs&&callback_device){unsigned before=(unsigned)callback_device->refcount;assert(before>0);assert(device_add_ref(callback_device)==before+1);assert(device_release(callback_device)==before);if(callback_keep_device){callback_keep_device=0;device_add_ref(callback_device);}reenter_private_store();callback_calls++;}return refs;}
static LONG InterlockedDecrement(LONG*p){return --*p;}static LONG InterlockedCompareExchange(LONG*p,LONG exchange,LONG compare){LONG old=*p;if(old==compare)*p=exchange;return old;}
static LONG InterlockedIncrement(LONG*p){return ++*p;}
static ULONG device_add_ref(Device*);static ULONG device_release(Device*);static void device_child_add_ref(Device*);static void device_child_release(Device*);
#define IDirect3DDevice${api}_AddRef(p) device_add_ref((Device*)(p))
#define IDirect3DDevice${api}_Release(p) device_release((Device*)(p))
static ULONG surface_add_ref(View*);static ULONG surface_release(View*);
static void surface_binding_add_ref(View*);static void surface_binding_release(View*);
${api===9?"static void surface_destroy_storage(View*);":""}
static ULONG texture_add_ref(Interface*);static ULONG texture_release(Interface*);typedef Interface IDirect3DTexture${api};
static Texture*texture_from_iface(Interface*p){return (Texture*)p;}
#define IDirect3DSurface${api}_AddRef(p) surface_add_ref((View*)(p))
#define IDirect3DTexture${api}_AddRef texture_add_ref
#define IDirect3DCubeTexture${api}_AddRef(p) ((void)(p),0u)
${api===8?`static void device_release_owned_references(Device*d){Texture*t=d->render_target_texture;d->render_target_texture=NULL;if(t)texture_release(&t->iface);View*v=d->depth_surface;d->depth_surface=NULL;if(v)surface_binding_release(v);}`:`static void device_release_owned_references(Device*d){View*v=d->render_target_surface;d->render_target_surface=NULL;if(v)surface_binding_release(v);v=d->render_target_surface_2;d->render_target_surface_2=NULL;if(v)surface_binding_release(v);}`}
static void parent_release(void*p){(void)p;assert(parent_refs==1);parent_refs=0;}
#define IDirect3D${api}_Release(p) parent_release(p)
#define IDirect3DTexture${api}_Release texture_release
#define IDirect3DSurface${api}_Release(p) surface_release((View*)(p))
#define IDirect3DCubeTexture${api}_Release(p) ((void)(p),0u)
#include "runtime/d9wg_private_data_bridge.h"
static void reenter_private_store(void){if(callback_readd_store){D9WGPrivateStore*store=callback_readd_store;IUnknown*object=callback_readd_object;callback_readd_store=NULL;callback_readd_object=NULL;const uint8_t tag[16]={3};assert(!d9wg_private_set(store,tag,object,sizeof(void*),1,&d9wg_private_ops));assert(IUnknown_Release(object)==1);}}

${['device_add_ref','device_child_add_ref','device_child_release'].map(n=>extract(source,n)).join('\n')}
${api===8?`static int g_swapchain_vtbl,g_surface_vtbl;
static UINT next_handle=0x9100;
static UINT allocate_handle(void){return next_handle++;}
static void ClientToScreen(HWND w,void*p){(void)w;int*point=p;point[0]=30;point[1]=40;}
static BOOL emit_command(UINT op,const void*p,UINT n){if(op==D9WG_OP_CREATE_SWAP_CHAIN){const D9WGCreateSwapChain*c=p;assert(n==sizeof(*c)&&c->device_handle==0x9000&&c->back_buffer_handle);return 1;}if(op==D9WG_OP_DESTROY_SWAP_CHAIN){const D9WGDestroySwapChain*c=p;assert(n==sizeof(*c)&&c->device_handle==0x9000);return 1;}const D9WGDestroyResource*c=p;if(target_pin_guard)assert(target_device&&target_device->refcount>0);assert(op==D9WG_OP_DESTROY_RESOURCE&&n==sizeof(*c)&&c->resource_kind==3);return 1;}

static BOOL supported_multisample_type(UINT v){return v==0||v==4;}
static BOOL supported_backbuffer_format(D3DFORMAT v){return v==21||v==22;}
static void detach_device_window(Device*d){(void)d;}static void restore_display_mode(Device*d){(void)d;}
static void emit_device_destroy_and_flush(uint32_t h){assert(h==0x9000);}
static void free_shader_list(void*p){(void)p;}static void free_extra_lights(void**p){(void)p;}
static HRESULT create_standalone_surface(Device*d,UINT width,UINT height,D3DFORMAT format,DWORD usage,int pool,BOOL lockable,BOOL backbuffer,BOOL depth,BOOL blocker,View**out){(void)lockable;(void)depth;assert(width==64&&height==64&&format==22&&usage==1&&pool==0&&backbuffer);*out=NULL;if(fail_create)return E_OUTOFMEMORY;View*v=HeapAlloc(NULL,0,sizeof(*v));if(!v)return E_OUTOFMEMORY;v->device=d;v->refcount=1;v->reset_blocker=blocker;if(blocker)d->implicit_surface_count++;device_child_add_ref(d);*out=v;return 0;}
${['swapchain_destroy_storage','texture_add_ref','texture_release','texture_get_surface_level'].map(n=>extract(source,n).replace(/\*surface_out = &surface->iface;/g,'*surface_out = surface;').replace(/IDirect3DSurface8_AddRef\(&surface->iface\)/g,'surface_add_ref(surface)')).join('\n')}
static HRESULT device_create_render_target(Device*d,UINT w,UINT h,D3DFORMAT f,UINT ms,BOOL lockable,View**out){assert(w==64&&h==64&&f==22&&!ms&&!lockable);*out=NULL;if(fail_create)return E_OUTOFMEMORY;Texture*t=HeapAlloc(NULL,0,sizeof(*t));if(!t)return E_OUTOFMEMORY;t->levels=HeapAlloc(NULL,0,sizeof(*t->levels));if(!t->levels){HeapFree(NULL,0,t);return E_OUTOFMEMORY;}t->refcount=1;t->level_count=1;t->usage=1;t->device=d;t->handle=allocate_handle();t->next_device_resource=d->texture_resources;d->texture_resources=t;device_child_add_ref(d);HRESULT hr=texture_get_surface_level(&t->iface,0,out);if(hr>=0){(*out)->device_container=1;(*out)->refcount=1;t->private_back_buffer=1;}texture_release(&t->iface);return hr;}
${['surface_is_implicit','free_implicit_surfaces','create_backbuffer_surface','device_get_backbuffer','device_create_swapchain','swapchain_get_backbuffer','swapchain_add_ref','swapchain_release'].map(n=>extract(source,n).replace(/&device->implicit_back_buffer->iface/g,'device->implicit_back_buffer')).join('\n')}`:`static D9SwapChain*swap_chain_from_iface(Interface*p){return (D9SwapChain*)p;}
static BOOL surface_is_implicit(View*v){return v->implicit;}
static void SelectObject(void*a,void*b){(void)a;(void)b;}static void DeleteDC(void*p){(void)p;}static void DeleteObject(void*p){(void)p;}
static void d9wg_light_free(void**p){(void)p;}static void d9wg_palette_free(void*p){(void)p;}static void unwatch_window_state(Device*d){(void)d;}static void restore_display_mode(Device*d){(void)d;}
static unsigned g_command_count;
static int emit_command(uint16_t op,const void*p,uint32_t size){if(op==D9WG_OP_DESTROY_SWAP_CHAIN){const D9WGDestroySwapChain*c=p;assert(size==sizeof(*c)&&c->device_handle==0x9000);return 1;}assert(op==D9WG_OP_DESTROY_RESOURCE&&size==sizeof(D9WGDestroyResource));const D9WGDestroyResource*c=p;assert(c->resource_kind==3||(c->resource_handle==0x9000&&!c->resource_kind));return 1;}
static BOOL submit_batch_locked(BOOL present){(void)present;return 1;}
${["texture_add_ref","texture_release","swap_chain_destroy_storage","swap_chain_add_ref","swap_chain_release"].map(n=>extract(source,n)).join("\n")}`}
${['surface_private_store','surface_set_private_data','surface_get_private_data','surface_free_private_data',...(api===9?['surface_uses_private_bindings']:[]),'surface_binding_add_ref','surface_binding_release','surface_add_ref',...(api===9?['surface_destroy_storage']:[]),'surface_release','device_release'].map(n=>extract(source,n)).join('\n')}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Private surface API ${api} line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){(void)fail_create;unsigned cases=0;Parent parent={0};Device*d=HeapAlloc(NULL,0,sizeof(*d));d->refcount=1;d->parent=&parent;d->handle=0x9000;d->display_mode=(Mode){64,64,22};d->present=(D3DPRESENT_PARAMETERS){.SwapEffect=1,.Windowed=1,.BackBufferWidth=64,.BackBufferHeight=64,.BackBufferFormat=22};const uint8_t key[16]={1},object_key[16]={2};DWORD data=0x10203040,size,out=0;IUnknown object={.refs=1};
for(unsigned i=0;i<3;i++){View*v=HeapAlloc(NULL,0,sizeof(*v));v->device=d;v->refcount=2;d->refcount++;d->child_parent_refs++;CHECK(!surface_set_private_data(v,key,&data,4,0));size=4;CHECK(!surface_get_private_data(v,key,&out,&size)&&out==data);CHECK(!surface_set_private_data(v,object_key,&object,sizeof(void*),1)&&object.refs==2);CHECK(surface_release(v)==1&&object.refs==2);CHECK(!surface_release(v)&&object.refs==1&&d->refcount==1);cases++;}
${api===8?`View*a=NULL,*b=NULL;CHECK(!device_get_backbuffer(d,0,0,&a));CHECK(!surface_set_private_data(a,key,&data,4,0));CHECK(!surface_set_private_data(a,object_key,&object,sizeof(void*),1)&&object.refs==2);CHECK(!surface_release(a)&&object.refs==2);CHECK(!device_get_backbuffer(d,0,0,&b));size=4;CHECK(!surface_get_private_data(b,key,&out,&size)&&out==data);CHECK(!surface_release(b)&&object.refs==2);cases++;
for(unsigned nth=1;nth<=4;nth++){IDirect3DSwapChain8*chain=(void*)1;unsigned before=allocated;allocation_calls=0;fail_nth=nth;CHECK(device_create_swapchain(d,&d->present,&chain)==E_OUTOFMEMORY&&!chain&&allocated==before&&d->refcount==1&&!d->additional_swap_chain_count&&!d->texture_resources);fail_nth=0;cases++;}
IDirect3DSwapChain8*chain_iface=(void*)1;fail_create=1;unsigned before=allocated;CHECK(device_create_swapchain(d,&d->present,&chain_iface)==E_OUTOFMEMORY&&!chain_iface&&allocated==before&&d->refcount==1);fail_create=0;cases++;
CHECK(!device_create_swapchain(d,&d->present,&chain_iface));D8SwapChain*chain=(D8SwapChain*)chain_iface;CHECK(chain->refcount==1&&chain->back_buffer&&chain->back_buffer->additional_back_buffer&&chain->back_buffer->texture->refcount==1&&d->additional_swap_chain_count==1);fail_create=1;
CHECK(!swapchain_get_backbuffer(chain_iface,0,0,&a)&&!swapchain_get_backbuffer(chain_iface,0,0,&b)&&a==b&&a==chain->back_buffer&&a->refcount==2&&a->texture->refcount==2&&d->refcount==3&&chain->refcount==1);fail_create=0;DWORD different=0x55667788;size=4;CHECK(surface_get_private_data(a,key,&out,&size)==D3DERR_NOTFOUND);CHECK(!surface_set_private_data(a,key,&different,4,0));CHECK(!surface_set_private_data(a,object_key,&object,sizeof(void*),1)&&object.refs==3);size=4;CHECK(!surface_get_private_data(b,key,&out,&size)&&out==different);CHECK(!swapchain_release(chain_iface)&&object.refs==3&&!d->additional_swap_chain_count);size=4;CHECK(!surface_get_private_data(b,key,&out,&size)&&out==different);CHECK(surface_release(a)==1&&object.refs==3);CHECK(!surface_release(b)&&object.refs==2&&d->refcount==1&&!d->implicit_surface_count&&!d->texture_resources);CHECK(!d9wg_private_set(&d->depth_surface_private_data,object_key,&object,sizeof(void*),1,&d9wg_private_ops)&&object.refs==3);cases++;
`:`for(unsigned i=0;i<2;i++){View*v=HeapAlloc(NULL,0,sizeof(*v));v->device=d;v->implicit=1;v->refcount=1;d->refcount++;d->child_parent_refs++;if(i)d->implicit_depth_stencil=v;else d->implicit_back_buffer=v;CHECK(!surface_set_private_data(v,key,&data,4,0));CHECK(!surface_set_private_data(v,object_key,&object,sizeof(void*),1)&&object.refs==2+i);CHECK(!surface_release(v)&&object.refs==2+i);size=4;CHECK(!surface_get_private_data(v,key,&out,&size)&&out==data);CHECK(surface_add_ref(v)==1);CHECK(!surface_release(v)&&object.refs==2+i);cases++;}`}
View empty={0};size=4;CHECK(surface_get_private_data(&empty,key,&out,&size)==D3DERR_NOTFOUND&&surface_free_private_data(&empty,key)==D3DERR_NOTFOUND);cases++;
CHECK(d->refcount==1&&!d->child_parent_refs);CHECK(!device_release(d)&&object.refs==1&&!allocated&&!parent_refs);cases++;
${api===8?`unsigned life=0;
#define LCHECK(v) do{CHECK(v);life++;}while(0)
for(unsigned path=0;path<4;path++){
 parent_refs=1;d=HeapAlloc(NULL,0,sizeof(*d));d->refcount=1;d->parent=&parent;d->handle=0x9000;d->display_mode=(Mode){64,64,22};d->present=(D3DPRESENT_PARAMETERS){.SwapEffect=1,.Windowed=1,.BackBufferWidth=64,.BackBufferHeight=64,.BackBufferFormat=22};
 IDirect3DSwapChain8*ci=NULL;LCHECK(!device_create_swapchain(d,&d->present,&ci));D8SwapChain*c=(D8SwapChain*)ci;View*v=c->back_buffer,*w=NULL;
 LCHECK(d->refcount==2&&d->child_parent_refs==1&&v->refcount==0&&v->texture->refcount==1&&v->texture->private_back_buffer);
 LCHECK(!swapchain_get_backbuffer(ci,0,0,&v)&&v==c->back_buffer&&v->refcount==1&&v->texture->refcount==2&&d->refcount==3&&d->child_parent_refs==2);
 LCHECK(!surface_set_private_data(v,object_key,&object,sizeof(void*),1)&&object.refs==2);
 if(!path){
  LCHECK(!surface_release(v)&&d->refcount==2&&v->texture->refcount==1&&object.refs==2);
  for(unsigned n=0;n<128;n++){
   LCHECK(!swapchain_get_backbuffer(ci,0,0,&w)&&w==v&&v->refcount==1&&v->texture->refcount==2&&d->refcount==3&&d->child_parent_refs==2);
   LCHECK(!swapchain_get_backbuffer(ci,0,2,&w)&&w==v&&v->refcount==2&&v->texture->refcount==2&&d->refcount==3);
   LCHECK(surface_add_ref(v)==3&&v->texture->refcount==2&&d->refcount==3);
   LCHECK(surface_release(w)==2&&d->refcount==3);
   LCHECK(surface_release(v)==1&&d->refcount==3);
   LCHECK(!surface_release(v)&&v->texture->refcount==1&&d->refcount==2&&d->child_parent_refs==1&&object.refs==2);
   LCHECK(!surface_release(v)&&!surface_release(v)&&d->refcount==2&&v->texture->refcount==1);
   LCHECK(surface_add_ref(v)==1&&v->texture->refcount==2&&d->refcount==3);
   LCHECK(!surface_release(v)&&d->refcount==2&&v->texture->refcount==1);
  }
  LCHECK(surface_add_ref(v)==1&&d->refcount==3);
  LCHECK(!swapchain_release(ci)&&d->refcount==2&&!c->refcount&&v->texture->refcount==1&&!d->additional_swap_chain_count&&object.refs==2);
  for(unsigned n=0;n<128;n++){
   LCHECK(swapchain_add_ref(ci)==1&&d->refcount==3&&v->texture->refcount==2&&d->additional_swap_chain_count==1);
   LCHECK(swapchain_add_ref(ci)==2&&d->refcount==3&&v->texture->refcount==2&&d->additional_swap_chain_count==1);
   LCHECK(swapchain_release(ci)==1&&d->refcount==3&&d->additional_swap_chain_count==1);
   LCHECK(!swapchain_release(ci)&&d->refcount==2&&v->texture->refcount==1&&!d->additional_swap_chain_count&&object.refs==2);
  }
  LCHECK(!surface_release(v)&&d->refcount==1&&!d->child_parent_refs&&!d->texture_resources&&object.refs==1);
  LCHECK(!device_release(d)&&!allocated&&!parent_refs);
 }else if(path==1){
  LCHECK(surface_add_ref(v)==2&&d->refcount==3&&v->texture->refcount==2);
  LCHECK(device_release(d)==2&&d->child_parent_refs==2);
  LCHECK(!swapchain_release(ci)&&d->refcount==1&&d->child_parent_refs==1&&v->texture->refcount==1&&object.refs==2);
  LCHECK(surface_release(v)==1&&d->refcount==1);
  LCHECK(!surface_release(v)&&!allocated&&!parent_refs&&object.refs==1);
 }else if(path==2){
  Texture*t=v->texture;LCHECK(texture_add_ref(&t->iface)==3);d->render_target_texture=t;
  LCHECK(!surface_release(v)&&d->refcount==2&&t->refcount==2);
  LCHECK(!swapchain_release(ci)&&d->refcount==1&&!d->child_parent_refs&&t->refcount==1&&object.refs==2);
  LCHECK(swapchain_add_ref(ci)==1&&d->refcount==2&&t->refcount==2&&d->additional_swap_chain_count==1);
  LCHECK(!swapchain_release(ci)&&d->refcount==1&&t->refcount==1&&!d->additional_swap_chain_count);
  LCHECK(!texture_get_surface_level(&t->iface,0,&w)&&w==v&&v->refcount==1&&d->refcount==2&&t->refcount==2);
  LCHECK(!surface_release(w)&&d->refcount==1&&t->refcount==1&&object.refs==2);
  LCHECK(!device_release(d)&&!allocated&&!parent_refs&&object.refs==1);
 }else{
  LCHECK(!surface_release(v)&&d->refcount==2&&v->texture->refcount==1&&object.refs==2);
  LCHECK(!swapchain_release(ci)&&d->refcount==1&&!d->texture_resources&&object.refs==1);
  LCHECK(!device_release(d)&&!allocated&&!parent_refs);
 }
}
printf("PASS actual D3D8 additional lifetime: %u checks; 128 surface and 128 raw-chain zero-count revivals, precise device/storage pins, physical chain/private retention, both final-owner orders and binding-only revival/cleanup\\n",life);
`:''}
${api===9?`unsigned life9=0;
#define LCHECK9(v) do{CHECK(v);life9++;}while(0)
for(unsigned path=0;path<3;path++){
 parent_refs=1;d=HeapAlloc(NULL,0,sizeof(*d));d->refcount=1;d->parent=&parent;d->handle=0x9000;
 Texture*t=HeapAlloc(NULL,0,sizeof(*t));t->levels=HeapAlloc(NULL,0,sizeof(*t->levels));t->level_count=1;t->refcount=1;t->device=d;t->handle=0x9900+path;t->private_back_buffer=1;d->texture_resources=t;
 View*v=HeapAlloc(NULL,0,sizeof(*v));v->device=d;v->texture=t;v->device_pin_transferred=1;
 D9SwapChain*c=HeapAlloc(NULL,0,sizeof(*c));c->refcount=1;c->device=d;c->handle=0xa000+path;c->back_buffer=v;d->additional_swap_chains=c;v->swap_chain=c;device_child_add_ref(d);
 LCHECK9(d->refcount==2&&d->child_parent_refs==1&&!v->refcount&&t->refcount==1);
 LCHECK9(surface_add_ref(v)==1&&d->refcount==3&&d->child_parent_refs==2&&t->refcount==1);
 LCHECK9(!surface_set_private_data(v,object_key,&object,sizeof(void*),1)&&object.refs==2);
 if(!path){
  LCHECK9(!surface_release(v)&&d->refcount==2&&object.refs==2);
  for(unsigned n=0;n<128;n++){
   LCHECK9(surface_add_ref(v)==1&&d->refcount==3&&v->swap_chain==c&&t->refcount==1);
   LCHECK9(surface_add_ref(v)==2&&d->refcount==3);
   LCHECK9(surface_release(v)==1&&d->refcount==3);
   LCHECK9(!surface_release(v)&&d->refcount==2&&d->child_parent_refs==1);
   LCHECK9(!surface_release(v)&&!surface_release(v)&&d->refcount==2&&t->refcount==1&&object.refs==2);
  }
  LCHECK9(surface_add_ref(v)==1&&d->refcount==3);
  LCHECK9(!swap_chain_release(&c->iface)&&d->refcount==2&&!c->refcount&&d->additional_swap_chains==c&&object.refs==2);
  for(unsigned n=0;n<128;n++){
   LCHECK9(swap_chain_add_ref(&c->iface)==1&&d->refcount==3&&v->refcount==1&&t->refcount==1);
   LCHECK9(swap_chain_add_ref(&c->iface)==2&&d->refcount==3);
   LCHECK9(swap_chain_release(&c->iface)==1&&d->refcount==3);
   LCHECK9(!swap_chain_release(&c->iface)&&d->refcount==2);
   LCHECK9(!swap_chain_release(&c->iface)&&!swap_chain_release(&c->iface)&&d->refcount==2&&v->swap_chain==c&&object.refs==2);
  }
  LCHECK9(!surface_release(v)&&d->refcount==1&&!d->child_parent_refs&&!d->additional_swap_chains&&!d->texture_resources&&object.refs==1);
  LCHECK9(!device_release(d)&&!allocated&&!parent_refs);
 }else if(path==1){
  LCHECK9(surface_add_ref(v)==2&&d->refcount==3);
  LCHECK9(device_release(d)==2&&d->child_parent_refs==2);
  LCHECK9(!swap_chain_release(&c->iface)&&d->refcount==1&&d->child_parent_refs==1&&v->refcount==2&&object.refs==2);
  LCHECK9(swap_chain_add_ref(&c->iface)==1&&d->refcount==2&&v->refcount==2);
  LCHECK9(!swap_chain_release(&c->iface)&&d->refcount==1);
  LCHECK9(surface_release(v)==1&&d->refcount==1);
  LCHECK9(!surface_release(v)&&!allocated&&!parent_refs&&object.refs==1);
 }else{
  surface_binding_add_ref(v);LCHECK9(v->refcount==1&&v->binding_refs==1&&d->refcount==3);d->render_target_surface=v;
  LCHECK9(!swap_chain_release(&c->iface)&&d->refcount==2);
  LCHECK9(!surface_release(v)&&d->refcount==1&&object.refs==2);
  LCHECK9(!device_release(d)&&!allocated&&!parent_refs&&object.refs==1);
 }
}
unsigned chain_binding_checks=0,chain_binding_revivals=0;
#define CB_CHECK(v) do{CHECK(v);chain_binding_checks++;}while(0)
for(unsigned order=0;order<5;order++){
 parent_refs=1;d=HeapAlloc(NULL,0,sizeof(*d));d->refcount=1;d->parent=&parent;d->handle=0x9000;
 Texture*t=HeapAlloc(NULL,0,sizeof(*t));t->levels=HeapAlloc(NULL,0,sizeof(*t->levels));t->level_count=1;t->refcount=1;t->device=d;t->handle=0x9a00+order;t->private_back_buffer=1;d->texture_resources=t;
 View*v=HeapAlloc(NULL,0,sizeof(*v));v->device=d;v->texture=t;v->device_pin_transferred=1;
 D9SwapChain*c=HeapAlloc(NULL,0,sizeof(*c));c->refcount=1;c->device=d;c->handle=0xa100+order;c->back_buffer=v;d->additional_swap_chains=c;v->swap_chain=c;device_child_add_ref(d);
 IUnknown retained={.refs=1};CB_CHECK(!surface_set_private_data(v,object_key,&retained,sizeof(void*),1)&&IUnknown_Release(&retained)==1);
 CB_CHECK(surface_add_ref(v)==1&&d->refcount==3);
 surface_binding_add_ref(v);surface_binding_add_ref(v);d->render_target_surface=v;d->render_target_surface_2=v;
 CB_CHECK(v->refcount==1&&v->binding_refs==2&&c->refcount==1&&d->refcount==3);
 CB_CHECK(!swap_chain_release(&c->iface)&&d->refcount==2&&d->additional_swap_chains==c);
 CB_CHECK(!surface_release(v)&&d->refcount==1&&!d->child_parent_refs&&v->binding_refs==2&&retained.refs==1);
 for(unsigned cycle=0;cycle<64;cycle++){
  CB_CHECK(surface_add_ref(v)==1&&d->refcount==2&&v->binding_refs==2);
  CB_CHECK(surface_add_ref(v)==2&&d->refcount==2);
  CB_CHECK(swap_chain_add_ref(&c->iface)==1&&d->refcount==3);
  CB_CHECK(swap_chain_add_ref(&c->iface)==2&&d->refcount==3);
  CB_CHECK(surface_release(v)==1&&!surface_release(v)&&d->refcount==2);
  CB_CHECK(swap_chain_release(&c->iface)==1&&!swap_chain_release(&c->iface)&&d->refcount==1&&!d->child_parent_refs);
  CB_CHECK(!surface_release(v)&&!swap_chain_release(&c->iface)&&v->binding_refs==2&&retained.refs==1&&d->texture_resources==t&&d->additional_swap_chains==c);
  surface_binding_add_ref(v);surface_binding_release(v);CB_CHECK(v->binding_refs==2&&!v->refcount&&!c->refcount&&d->refcount==1);chain_binding_revivals+=2;
 }
 callback_device=d;unsigned before_calls=callback_calls;
 if(order==0)CB_CHECK(!device_release(d));
 else if(order==1){device_release_owned_references(d);CB_CHECK(d->refcount==1&&!d->additional_swap_chains&&!d->texture_resources&&callback_calls==before_calls+1&&allocated==1);CB_CHECK(!device_release(d));}
 else if(order==2){CB_CHECK(swap_chain_add_ref(&c->iface)==1&&d->refcount==2);CB_CHECK(device_release(d)==1&&retained.refs==1&&!v->binding_refs&&c->refcount==1);CB_CHECK(!swap_chain_release(&c->iface));}
 else if(order==3){CB_CHECK(surface_add_ref(v)==1&&d->refcount==2);CB_CHECK(device_release(d)==1&&retained.refs==1&&!v->binding_refs&&v->refcount==1);CB_CHECK(!surface_release(v));}
 else{CB_CHECK(surface_add_ref(v)==1&&swap_chain_add_ref(&c->iface)==1&&d->refcount==3);CB_CHECK(device_release(d)==2&&retained.refs==1&&!v->binding_refs);CB_CHECK(!swap_chain_release(&c->iface)&&d->refcount==1&&retained.refs==1);CB_CHECK(!surface_release(v));}
 callback_device=NULL;CB_CHECK(callback_calls==before_calls+1&&!retained.refs&&!allocated&&!parent_refs);
}
printf("PASS actual D3D9 additional private bindings: %u checks, %u public-zero surface/chain revivals; two internal slots, one device pin per public object, borrowed rebinds, protected private callbacks and five final-owner orders\\n",chain_binding_checks,chain_binding_revivals);
printf("PASS actual D3D9 additional lifetime: %u checks; 128 surface and 128 chain zero-count revivals, independent public counts, exact device pins, retained physical chain/private store, last-view and binding-only device teardown\\n",life9);
`:''}
${api===8?`unsigned standalone_checks=0,standalone_revivals=0;
#define TCHECK(v) do{if(!(v)){fprintf(stderr,"D8 standalone lifetime line %u: %s\\n",__LINE__,#v);return 1;}standalone_checks++;}while(0)
for(unsigned depth=0;depth<2;depth++)for(unsigned order=0;order<4;order++){
 parent_refs=1;d=HeapAlloc(NULL,0,sizeof(*d));d->refcount=1;d->parent=&parent;d->handle=0x9000;target_device=d;target_pin_guard=1;
 View*v=NULL;TCHECK(!device_create_render_target(d,64,64,22,0,0,&v)&&v);Texture*t=v->texture;t->usage=depth?2:1;
 IUnknown retained={.refs=1};DWORD marker=0x28394050,value=0,n=4;
 TCHECK(v->device_container&&!v->additional_back_buffer&&v->refcount==1&&t->refcount==1&&t->private_back_buffer&&d->refcount==2&&d->child_parent_refs==1);
 TCHECK(!surface_set_private_data(v,key,&marker,4,0)&&!surface_set_private_data(v,object_key,&retained,sizeof(void*),1)&&retained.refs==2);
 if(depth){surface_binding_add_ref(v);d->depth_surface=v;}else{texture_add_ref(&t->iface);d->render_target_texture=t;}
 TCHECK(v->refcount==1&&t->refcount==2&&d->refcount==2);
 TCHECK(surface_add_ref(v)==2&&v->refcount==2&&t->refcount==2&&d->refcount==2&&surface_release(v)==1&&v->refcount==1&&t->refcount==2);
 TCHECK(!surface_release(v)&&v->refcount==0&&t->refcount==1&&d->refcount==1&&!d->child_parent_refs&&retained.refs==2);
 TCHECK(!surface_release(v)&&v->refcount==0&&t->refcount==1&&d->refcount==1);
 for(unsigned cycle=0;cycle<64;cycle++){
  View*w=NULL;TCHECK(!texture_get_surface_level(&t->iface,0,&w)&&w==v&&v->refcount==1&&t->refcount==2&&d->refcount==2&&d->child_parent_refs==1);
  TCHECK(surface_add_ref(w)==2&&t->refcount==2&&d->refcount==2&&surface_release(w)==1);
  n=4;TCHECK(!surface_get_private_data(w,key,&value,&n)&&value==marker&&retained.refs==2);
  TCHECK(!surface_release(w)&&v->refcount==0&&t->refcount==1&&d->refcount==1&&!d->child_parent_refs&&retained.refs==2);
  if(depth){surface_binding_add_ref(v);surface_binding_release(v);}else{texture_add_ref(&t->iface);texture_release(&t->iface);}
  TCHECK(v->refcount==0&&t->refcount==1&&d->refcount==1);standalone_revivals++;
 }
 if(order==0){if(depth){d->depth_surface=NULL;surface_binding_release(v);}else{d->render_target_texture=NULL;texture_release(&t->iface);}TCHECK(d->refcount==1&&!d->texture_resources&&retained.refs==1&&allocated==1);TCHECK(!device_release(d)&&!parent_refs&&!allocated);}
 if(order==1){TCHECK(!device_release(d)&&!parent_refs&&!allocated&&retained.refs==1);}
 if(order==2){View*w=NULL;TCHECK(!texture_get_surface_level(&t->iface,0,&w)&&w==v&&d->refcount==2);if(depth){d->depth_surface=NULL;surface_binding_release(v);}else{d->render_target_texture=NULL;texture_release(&t->iface);}TCHECK(v->refcount==1&&t->refcount==1&&retained.refs==2);TCHECK(device_release(d)==1);TCHECK(!surface_release(w)&&!parent_refs&&!allocated&&retained.refs==1);}
 if(order==3){View*w=NULL;TCHECK(!texture_get_surface_level(&t->iface,0,&w)&&w==v&&surface_add_ref(w)==2&&d->refcount==2);TCHECK(device_release(d)==1&&v->refcount==2&&t->refcount==1&&retained.refs==2);TCHECK(surface_release(w)==1);TCHECK(!surface_release(w)&&!parent_refs&&!allocated&&retained.refs==1);}
 target_pin_guard=0;target_device=NULL;
}
printf("PASS actual D3D8 standalone binding lifetime: %u checks, %u public-zero revivals; real public/storage/device methods, separate color/depth storage holds, private retention, cached revival and four final-owner orders\\n",standalone_checks,standalone_revivals);
`:""}
unsigned implicit_callbacks=0;
for(unsigned order=0;order<5;order++){
 parent_refs=1;d=HeapAlloc(NULL,0,sizeof(*d));d->refcount=1;d->parent=&parent;d->handle=0x9000;
 View*cached[2]={HeapAlloc(NULL,0,sizeof(View)),HeapAlloc(NULL,0,sizeof(View))};
 d->implicit_back_buffer=cached[0];d->implicit_depth_stencil=cached[1];
 IUnknown owners[3]={{1},{1},{1}};
 for(unsigned i=0;i<2;i++){
  cached[i]->device=d;cached[i]->implicit=1;
  ${api===8?'cached[i]->private_data_owner=i?&d->depth_surface_private_data:&d->back_buffer_private_data;':''}
  CHECK(!surface_set_private_data(cached[i],object_key,&owners[i],sizeof(void*),1)&&owners[i].refs==2);
  CHECK(IUnknown_Release(&owners[i])==1);
 }
 if(order==1){${api===8?'d->depth_surface=cached[1];surface_binding_add_ref(cached[1]);':'d->render_target_surface=cached[0];surface_binding_add_ref(cached[0]);'}}
 if(order==2)CHECK(surface_add_ref(cached[0])==1&&d->refcount==2);
 unsigned old_callbacks=callback_calls;callback_device=d;callback_keep_device=order==3;
 if(order==4){callback_readd_store=${api===8?'&d->back_buffer_private_data':'&cached[0]->private_data'};callback_readd_object=&owners[2];}
 if(order==2){CHECK(device_release(d)==1&&callback_calls==old_callbacks&&owners[0].refs==1&&owners[1].refs==1);CHECK(!surface_release(cached[0]));}
 else if(order==3){CHECK(device_release(d)==1&&callback_calls==old_callbacks+1&&!owners[0].refs&&owners[1].refs==1&&parent_refs==1);CHECK(!device_release(d));}
 else CHECK(!device_release(d));
 callback_device=NULL;unsigned expected_callbacks=order==4?3:2;CHECK(callback_calls==old_callbacks+expected_callbacks&&!owners[0].refs&&!owners[1].refs&&(order!=4||!owners[2].refs)&&!parent_refs&&!allocated);implicit_callbacks+=expected_callbacks;
}
printf("PASS actual API ${api} implicit private callbacks: %u exact finalizers; empty/owned-bound/last-public/retained-parent/reentrant-insertion orders, protected queries, deferred stores and complete cache cleanup\\n",implicit_callbacks);
printf("PASS actual API ${api} surface private data: %u standalone/implicit/store lifetime, final device cleanup${api===8?', cached chain surfaces, retained texture-level stores, early chain release and allocation failures':''} cases\\n",cases);return 0;}
`);const b=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-I',root,dir+'/entry.c',root+'/runtime/d9wg_private_data.c',root+'/runtime/d9wg_present_pacing.c','-o',dir+'/entry'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+'/entry',[],{encoding:'utf8',timeout:20000});assert.equal(r.status,0,r.stderr+' signal='+r.signal);process.stdout.write(r.stdout);}}finally{fs.rmSync(dir,{recursive:true,force:true});}
