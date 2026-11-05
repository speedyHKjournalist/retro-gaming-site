"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-target-lifetime-"));
function extract(source, name) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;}
    return source.slice(m.index, end);
}
try {
    const source = fs.readFileSync(process.env.D9WG_TARGET_LIFETIME_SOURCE || root + "/d3d9proxy/d3d9_proxy.c", "utf8");
    fs.writeFileSync(dir + "/target.c", `#include <assert.h>
#include <stdint.h>
/* No frontend views are attached in this isolated lifetime fixture. */
#define core_view_retire8(object) ((void)(object))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
#include "runtime/d9wg_private_data.h"
typedef uint32_t UINT,DWORD,ULONG,D3DMULTISAMPLE_TYPE;typedef int32_t LONG,HRESULT;typedef int BOOL,D3DFORMAT,D3DPOOL;typedef uint8_t BYTE;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define E_OUTOFMEMORY (-3)
#define D3DPOOL_DEFAULT 0
#define D3DUSAGE_RENDERTARGET 1
#define D3DUSAGE_DEPTHSTENCIL 2
#define HEAP_ZERO_MEMORY 8
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
#define TRACE_MARK_ENTER(...)
#define TRACE_MARK_EXIT(...)
#define TRACE_REGISTER_RANGE(...)
#define TRACE_SURFACE_FREED(...)
#define TRACE_FLUSH(...)
typedef struct{const void*lpVtbl;}Interface;typedef Interface IDirect3DDevice9,IDirect3DSurface9,IDirect3DTexture9;
typedef struct Device D9Device;typedef struct Surface D9Surface;typedef struct Texture D9Texture,D9CubeTexture;typedef struct{LONG refcount;UINT handle;D9Device*device;D9Surface*back_buffer,*cached_buffers[4];}D9SwapChain;
typedef struct{UINT width,height,row_pitch,row_count,byte_count;BYTE*shadow;BOOL shadow_valid;D9WGPrivateStore private_data;D9Surface*level_surface;}D9TextureLevel;
struct Texture{Interface iface;LONG refcount,binding_refs;D9Device*device;UINT handle,width,height,level_count,usage;D3DFORMAT format;D3DPOOL pool;D3DMULTISAMPLE_TYPE multisample;DWORD multisample_quality;D9TextureLevel*levels;D9Texture*next_device_resource;BOOL private_back_buffer;D9WGPrivateStore private_data;};
struct Surface{Interface iface;LONG refcount,binding_refs;D9Device*device;D9Texture*texture,*cube_texture;D9SwapChain*swap_chain;BOOL texture_child,auto_depth_stencil,backbuffer,device_pin_transferred;UINT level,width,height;D3DFORMAT format;D3DPOOL pool;D3DMULTISAMPLE_TYPE multisample;DWORD multisample_quality;BYTE*shadow;void*dc,*dc_previous,*dib;D9WGPrivateStore private_data;};
typedef struct{Interface iface;LONG refs;}Parent;
struct Device{Interface iface;D9SwapChain implicit_swap_chain;LONG refcount,child_parent_refs;BOOL releasing_owned_refs;UINT handle;Parent*parent;D9Texture*texture_resources,*cube_textures;D9Surface*render_target_surface,*implicit_back_buffer,*implicit_depth_stencil;void*extra_lights;int palettes;};
static const D9WGPrivateOps d9wg_private_ops={0};static int g_texture_vtbl,g_surface_vtbl,g_transport_lock;static UINT g_command_count;
static D9Device*active_device;
static UINT live,allocations,fail_allocation,fail_emit,fail_layout,handles,creates,destroys,device_destroys,checks;
static D9Device*device_from_iface(Interface*p){return (D9Device*)p;}static D9Surface*surface_from_iface(Interface*p){return (D9Surface*)p;}static D9Texture*texture_from_iface(Interface*p){return (D9Texture*)p;}
static void*GetProcessHeap(void){return NULL;}static void*HeapAlloc(void*h,UINT f,size_t n){(void)h;assert(f==8&&n);if(++allocations==fail_allocation)return NULL;void*p=calloc(1,n);assert(p);live++;return p;}
static void HeapFree(void*h,UINT f,void*p){(void)h;assert(!f);if(p){assert(live);live--;free(p);}}
static LONG InterlockedIncrement(LONG*p){return ++*p;}static LONG InterlockedDecrement(LONG*p){assert(*p>0);return --*p;}static LONG InterlockedCompareExchange(LONG*p,LONG v,LONG c){LONG old=*p;if(old==c)*p=v;return old;}
static void cube_destroy_storage(D9CubeTexture*);static void cube_binding_add_ref(D9CubeTexture*);static void cube_binding_release(D9CubeTexture*);static void texture_destroy_storage(D9Texture*);static void texture_binding_add_ref(D9Texture*);static void texture_binding_release(D9Texture*);static void surface_destroy_storage(D9Surface*);static void surface_binding_release(D9Surface*);
static ULONG device_add_ref(Interface*);static ULONG device_release(Interface*);static ULONG surface_add_ref(Interface*);static ULONG surface_release(Interface*);static ULONG texture_release(Interface*);
#define IDirect3DSurface9_AddRef surface_add_ref
#define IDirect3DSurface9_Release surface_release
#define IDirect3DDevice9_AddRef device_add_ref
#define IDirect3DDevice9_Release device_release
#define IDirect3DTexture9_Release texture_release
#define IDirect3DTexture9_AddRef(p) (assert(0),(void)(p),0u)
#define IDirect3DCubeTexture9_AddRef(p) (assert(0),(void)(p),0u)
#define IDirect3DCubeTexture9_Release(p) (assert(0),(void)(p),0u)
#define IDirect3D9_Release(p) (--((Parent*)(p))->refs)
static void device_release_owned_references(D9Device*d){D9Surface*s=d->render_target_surface;d->render_target_surface=NULL;if(s)surface_binding_release(s);}
static void swap_chain_destroy_storage(D9SwapChain*c){(void)c;assert(0);}
static void d9wg_light_free(void**p){(void)p;}static void d9wg_palette_free(void*p){(void)p;}static void unwatch_window_state(D9Device*d){(void)d;}static void restore_display_mode(D9Device*d){(void)d;}
static void SelectObject(void*a,void*b){(void)a;(void)b;}static void DeleteDC(void*p){(void)p;}static void DeleteObject(void*p){(void)p;}
static void EnterCriticalSection(int*p){(void)p;}static void LeaveCriticalSection(int*p){(void)p;}static BOOL submit_batch_locked(BOOL present){assert(!present);return 1;}
static UINT allocate_handle(void){return ++handles;}
static BOOL texture_level_layout(D3DFORMAT f,UINT w,UINT h,UINT*p,UINT*r,UINT*b){if(fail_layout||f!=22)return 0;*p=w*4;*r=h;*b=w*h*4;return 1;}
static BOOL emit_texture_create(D9Device*d,D9Texture*t){assert(t->device==d&&t->width&&t->height&&t->level_count==1&&t->multisample_quality==0);creates++;return !fail_emit;}
static BOOL emit_command(UINT op,const void*p,UINT n){assert(op==D9WG_OP_DESTROY_RESOURCE&&n==sizeof(D9WGDestroyResource));const D9WGDestroyResource*c=p;if(c->resource_kind==3)destroys++;else{assert(!c->resource_kind&&c->resource_handle==7&&active_device&&!active_device->texture_resources&&!active_device->render_target_surface&&!active_device->child_parent_refs);device_destroys++;}return 1;}
${["swap_chain_buffers_referenced","swap_chain_destroy_cached_buffers","swap_chain_cached_private_data","device_add_ref", "device_child_add_ref", "device_child_release", "device_release", "surface_is_implicit", "cube_destroy_storage", "cube_binding_add_ref", "cube_binding_release", "texture_destroy_storage", "texture_binding_add_ref", "texture_binding_release", "texture_release", "surface_uses_private_bindings", "surface_binding_add_ref", "surface_binding_release", "surface_destroy_storage", "surface_add_ref", "surface_release", "create_target_texture"].map(n => extract(source, n)).join("\n")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Target lifetime line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
static D9Device*new_device(Parent*p){D9Device*d=HeapAlloc(NULL,8,sizeof(*d));d->refcount=1;d->handle=7;d->parent=p;p->refs=1;active_device=d;return d;}
int main(void){unsigned cases=0;const UINT usages[]={1,1,2,2,0},samples[]={0,4,0,4,0};
for(UINT profile=0;profile<5;profile++){
 Parent parent={0};D9Device*d=new_device(&parent);UINT usage=usages[profile],sample=samples[profile];D3DFORMAT format=usage==2?75:22;
 for(UINT cycle=0;cycle<64;cycle++){
  Interface*out=(void*)(uintptr_t)1;UINT before=creates,old_destroys=destroys;
  CHECK(!create_target_texture(d,32,24,format,usage,sample,0,&out)&&out);D9Surface*s=(D9Surface*)out;
  CHECK(d->refcount==2&&d->child_parent_refs==1&&s->refcount==1&&s->texture->refcount==1&&s->texture->private_back_buffer);
  CHECK(s->multisample==sample&&s->texture->multisample==sample&&s->format==format&&s->texture->usage==usage&&s->width==32&&s->height==24&&creates==before+1);
  CHECK(surface_add_ref(out)==2&&d->refcount==2&&d->child_parent_refs==1&&s->texture->refcount==1);
  CHECK(surface_release(out)==1&&d->refcount==2&&destroys==old_destroys);
  CHECK(!surface_release(out)&&d->refcount==1&&!d->child_parent_refs&&!d->texture_resources&&live==1&&destroys==old_destroys+1);cases++;
 }
 for(UINT fail=1;fail<=(usage?3u:4u);fail++){
  Interface*out=(void*)(uintptr_t)1;fail_allocation=allocations+fail;CHECK(create_target_texture(d,32,24,format,usage,sample,0,&out)==E_OUTOFMEMORY&&!out&&d->refcount==1&&!d->child_parent_refs&&!d->texture_resources&&live==1);fail_allocation=0;cases++;
 }
 Interface*out=(void*)(uintptr_t)1;fail_emit=1;CHECK(create_target_texture(d,32,24,format,usage,sample,0,&out)==D3DERR_DRIVERINTERNALERROR&&!out&&d->refcount==1&&!d->child_parent_refs&&!d->texture_resources&&live==1);fail_emit=0;cases++;
 CHECK(!device_release(&d->iface)&&!live&&!parent.refs);cases++;
 for(UINT order=0;order<3;order++){
  d=new_device(&parent);out=NULL;CHECK(!create_target_texture(d,32,24,format,usage,sample,0,&out)&&out);D9Surface*s=(D9Surface*)out;
  if(order==0){CHECK(device_release(&d->iface)==1&&d->child_parent_refs==1);CHECK(!surface_release(out)&&!live&&!parent.refs);}
  if(order==1){d->render_target_surface=s;surface_binding_add_ref(s);CHECK(s->refcount==1&&s->binding_refs==1&&surface_release(out)==0&&d->refcount==1);CHECK(!device_release(&d->iface)&&!live&&!parent.refs);}
  if(order==2){CHECK(device_add_ref(&d->iface)==3&&device_release(&d->iface)==2);CHECK(!surface_release(out)&&d->refcount==1&&!d->texture_resources);CHECK(!device_release(&d->iface)&&!live&&!parent.refs);}
  cases++;
 }
}
Parent parent={0};D9Device*d=new_device(&parent);Interface*out=(void*)(uintptr_t)1;CHECK(create_target_texture(d,1,1,22,1,0,0,NULL)==D3DERR_INVALIDCALL);CHECK(create_target_texture(d,0,1,22,1,0,0,&out)==D3DERR_INVALIDCALL&&!out&&live==1);fail_layout=1;CHECK(create_target_texture(d,1,1,22,1,0,0,&out)==D3DERR_INVALIDCALL&&!out&&live==1&&!d->child_parent_refs);fail_layout=0;CHECK(!device_release(&d->iface)&&!live&&!parent.refs);
printf("PASS actual D3D9 target lifetime: %u checks, %u lifetime/failure groups; five RT/depth/offscreen sample profiles, one public device pin, private storage without a pin, exact AddRef/Release, allocation/emit cleanup and device-first/binding-only teardown\\n",checks,cases);return 0;}
`);
    const b = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", "-I", root, dir + "/target.c", root + "/runtime/d9wg_private_data.c", "-o", dir + "/target"], {encoding: "utf8"}); assert.equal(b.status, 0, b.stderr);
    const r = spawnSync(dir + "/target", [], {encoding: "utf8", timeout: 10000}); assert.equal(r.status, 0, r.stderr + " signal=" + r.signal); process.stdout.write(r.stdout);
} finally {fs.rmSync(dir, {recursive: true, force: true});}
