"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-target-binding-"));
function extract(source, name) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source);
    // Old-source controls still execute the real legacy setters and public
    // methods; absent new helpers are unreachable compatibility declarations.
    const compatibility = {
        surface_destroy_storage: "static void surface_destroy_storage(D9Surface*s){(void)s;assert(0);}",
        surface_uses_private_bindings: "static BOOL surface_uses_private_bindings(const D9Surface*s){(void)s;return FALSE;}",
        surface_binding_add_ref: "static void surface_binding_add_ref(D9Surface*s){IDirect3DSurface9_AddRef(&s->iface);}",
        surface_binding_release: "static void surface_binding_release(D9Surface*s){IDirect3DSurface9_Release(&s->iface);}",
    };
    if (!m && compatibility[name]) return compatibility[name];
    if (!m && ["vb_destroy_storage", "vb_binding_add_ref", "vb_binding_release", "ib_destroy_storage", "ib_binding_add_ref", "ib_binding_release", "volume_texture_destroy_storage", "volume_texture_binding_add_ref", "volume_texture_binding_release", "cube_destroy_storage", "cube_binding_add_ref", "cube_binding_release", "texture_destroy_storage", "texture_binding_add_ref", "texture_binding_release"].includes(name))
        return extract(fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"), name);
    assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;}
    return source.slice(m.index, end);
}
const bufferBindings = process.env.D9WG_TEST_BUFFER_BINDINGS === "1";
const volumeBindings = bufferBindings || process.env.D9WG_TEST_VOLUME_BINDINGS === "1";
const cubeBindings = volumeBindings || process.env.D9WG_TEST_CUBE_BINDINGS === "1";
const textureBindings = cubeBindings || process.env.D9WG_TEST_TEXTURE_BINDINGS === "1";
try {
    const source = fs.readFileSync(process.env.D9WG_TARGET_BINDING_SOURCE || root + "/d3d9proxy/d3d9_proxy.c", "utf8");
    fs.writeFileSync(dir + "/target.c", `#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
#include "runtime/d9wg_private_data.h"
#include "runtime/d9wg_reset.h"
typedef uint32_t UINT,DWORD,ULONG,D3DMULTISAMPLE_TYPE;typedef int32_t LONG,HRESULT;typedef int BOOL,D3DFORMAT,D3DPOOL,D3DCUBEMAP_FACES;typedef uint8_t BYTE;
#define D9_MAX_RENDER_TARGETS 4
#define D9_MAX_STREAMS 16
#define D9_MAX_TEXTURE_BINDINGS 20
#define D3DERR_NOTFOUND (-4)
#define D3DBACKBUFFER_TYPE_MONO 0
#define SUCCEEDED(v) ((v)>=0)
#define FAILED(v) ((v)<0)
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define E_OUTOFMEMORY (-3)
#define D3DPOOL_DEFAULT 0
#define D3DMULTISAMPLE_NONE 0
#define D3DVERTEXTEXTURESAMPLER0 257
#define PURE_DEVICE_REFUSE(...)
#define D3DUSAGE_RENDERTARGET 1
#define D3DUSAGE_DEPTHSTENCIL 2
#define HEAP_ZERO_MEMORY 8
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
#define TRACE_MARK_ENTER(...) trace_enter(__VA_ARGS__)
#define TRACE_MARK_EXIT(...) trace_exit(__VA_ARGS__)
#define TRACE_REGISTER_RANGE(...)
#define TRACE_SURFACE_FREED(...)
#define TRACE_FLUSH(...)
typedef struct{const void*lpVtbl;}Interface;typedef Interface IDirect3DDevice9,IDirect3DSurface9,IDirect3DTexture9,IDirect3DBaseTexture9,IDirect3DCubeTexture9,IDirect3DVolumeTexture9,IDirect3DVolume9,IDirect3DVertexBuffer9,IDirect3DIndexBuffer9;
typedef struct Device D9Device;typedef struct Surface D9Surface,D9Volume;typedef struct Texture D9Texture,D9CubeTexture,D9VolumeTexture,D9VertexBuffer,D9IndexBuffer;typedef struct Chain{LONG refcount;UINT handle;struct Chain*next_device_chain;}D9SwapChain;
typedef struct{UINT width,height,row_pitch,row_count,byte_count;BYTE*shadow;BOOL shadow_valid,locked;D9WGPrivateStore private_data;D9Surface*level_surface,*level_volume;}D9TextureLevel;
struct Texture{Interface iface;LONG refcount,binding_refs;D9Device*device;UINT handle,width,height,level_count,usage,length,fvf;BYTE*shadow;void*clips;D3DFORMAT format;D3DPOOL pool;D3DMULTISAMPLE_TYPE multisample;DWORD multisample_quality;D9TextureLevel*levels;D9Texture*next_device_resource;BOOL private_back_buffer;D9WGPrivateStore private_data;BOOL locked;};
struct Surface{Interface iface;LONG refcount,binding_refs;D9Device*device;D9Texture*texture,*cube_texture;D9SwapChain*swap_chain;BOOL texture_child,auto_depth_stencil,backbuffer,device_pin_transferred,locked;UINT level,width,height,cube_face;D3DFORMAT format;D3DPOOL pool;D3DMULTISAMPLE_TYPE multisample;DWORD multisample_quality;BYTE*shadow;void*dc,*dc_previous,*dib;D9WGPrivateStore private_data;};
typedef struct{Interface iface;LONG refs;}Parent;
typedef struct{UINT X,Y,Width,Height;float MinZ,MaxZ;}D3DVIEWPORT9;
typedef struct{int left,top,right,bottom;}RECT;
typedef struct{Interface iface;}D9VertexDeclaration;
typedef struct{union{Interface vertex,pixel;}iface;}D9Shader;
typedef struct{D9Texture*textures[20],*cube_textures[20],*volume_textures[20],*index_buffer;struct{D9Texture*buffer;UINT offset,stride;}streams[16];BOOL has_texture[20],has_stream[16],has_indices;}D9StateBlock;
struct Device{Interface iface;D9StateBlock*recording_result;LONG refcount,child_parent_refs;BOOL releasing_owned_refs;UINT handle;Parent*parent;D9Texture*texture_resources;D9Surface*implicit_back_buffer,*implicit_depth_stencil,*render_target_surfaces[4],*depth_stencil_surface;BOOL depth_stencil_unbound;void*extra_lights;int palettes;D9Texture*vertex_buffers,*index_buffers,*cube_textures,*volume_textures,*textures[20],*cube_bindings[20],*volume_bindings[20],*index_buffer;struct{D9Texture*buffer;UINT stride,offset,frequency;}streams[16];D9VertexDeclaration*vertex_declaration;D9Shader*vertex_shader,*pixel_shader;UINT fvf;struct{UINT BackBufferWidth,BackBufferHeight,BackBufferFormat,MultiSampleType,MultiSampleQuality,AutoDepthStencilFormat;BOOL EnableAutoDepthStencil;}present;D9SwapChain*additional_swap_chains;};
static const D9WGPrivateOps d9wg_private_ops={0};static int g_texture_vtbl,g_surface_vtbl,g_transport_lock;
${textureBindings?"static int g_cube_vtbl,g_volume_texture_vtbl;":""}
${bufferBindings?"static int g_vb_vtbl,g_ib_vtbl;":""}static UINT g_command_count;
static unsigned release_enters,release_exits;
static void trace_enter(const char*m){if(!strcmp(m,"Surface.Release"))release_enters++;}
static void trace_exit(const char*m,HRESULT result,const void*out){(void)result;(void)out;if(!strcmp(m,"Surface.Release"))release_exits++;}
static D9Device*active_device;
static UINT live,allocations,fail_allocation,fail_emit,fail_layout,handles,creates,destroys,device_destroys,checks;
static D9Device*device_from_iface(Interface*p){return (D9Device*)p;}static D9Surface*surface_from_iface(Interface*p){return (D9Surface*)p;}static D9Texture*texture_from_iface(Interface*p){return (D9Texture*)p;}
${textureBindings?"static D9CubeTexture*cube_from_iface(Interface*p){return (D9CubeTexture*)p;}\nstatic D9VolumeTexture*volume_texture_from_iface(Interface*p){return (D9VolumeTexture*)p;}":""}
${volumeBindings?"static D9Volume*volume_from_iface(Interface*p){return (D9Volume*)p;}":""}
static void*GetProcessHeap(void){return NULL;}static void*HeapAlloc(void*h,UINT f,size_t n){(void)h;assert(f==8&&n);if(++allocations==fail_allocation)return NULL;void*p=calloc(1,n);assert(p);live++;return p;}
static void HeapFree(void*h,UINT f,void*p){(void)h;assert(!f);if(p){assert(live);live--;free(p);}}
static void d9wg_vertex_windows_clip_release(void*p,void*context){(void)context;HeapFree(NULL,0,p);}
static void d9wg_clip_buffer_free(void**clips,void(*release)(void*,void*),void*context){if(*clips){void*p=*clips;*clips=NULL;release(p,context);}}
${bufferBindings?"static D9VertexBuffer*vb_from_iface(Interface*p){return (D9VertexBuffer*)p;}\nstatic D9IndexBuffer*ib_from_iface(Interface*p){return (D9IndexBuffer*)p;}":""}
static LONG InterlockedIncrement(LONG*p){return ++*p;}static LONG InterlockedDecrement(LONG*p){assert(*p>0);return --*p;}static LONG InterlockedCompareExchange(LONG*p,LONG v,LONG c){LONG old=*p;if(old==c)*p=v;return old;}
static void vb_destroy_storage(D9VertexBuffer*);static void vb_binding_add_ref(D9VertexBuffer*);static void vb_binding_release(D9VertexBuffer*);static void ib_destroy_storage(D9IndexBuffer*);static void ib_binding_add_ref(D9IndexBuffer*);static void ib_binding_release(D9IndexBuffer*);static void volume_texture_destroy_storage(D9VolumeTexture*);static void volume_texture_binding_add_ref(D9VolumeTexture*);static void volume_texture_binding_release(D9VolumeTexture*);static void cube_destroy_storage(D9CubeTexture*);static void cube_binding_add_ref(D9CubeTexture*);static void cube_binding_release(D9CubeTexture*);static void texture_destroy_storage(D9Texture*);static void texture_binding_add_ref(D9Texture*);static void texture_binding_release(D9Texture*);static void surface_destroy_storage(D9Surface*);static void surface_binding_release(D9Surface*);
static ULONG device_add_ref(Interface*);static ULONG device_release(Interface*);static ULONG surface_add_ref(Interface*);static ULONG surface_release(Interface*);static ULONG texture_release(Interface*);
#define IDirect3DSurface9_AddRef surface_add_ref
#define IDirect3DSurface9_Release surface_release
#define IDirect3DDevice9_AddRef device_add_ref
#define IDirect3DDevice9_Release device_release
#define IDirect3DTexture9_Release texture_release
${textureBindings?"static ULONG texture_add_ref(Interface*);static ULONG cube_add_ref(Interface*);static ULONG cube_release(Interface*);static ULONG volume_texture_add_ref(Interface*);static ULONG volume_texture_release(Interface*);\nstatic ULONG base_texture_add_ref(Interface*p){return p->lpVtbl==&g_cube_vtbl?cube_add_ref(p):p->lpVtbl==&g_volume_texture_vtbl?volume_texture_add_ref(p):texture_add_ref(p);}\n#define IDirect3DTexture9_AddRef texture_add_ref\n#define IDirect3DBaseTexture9_AddRef base_texture_add_ref":"#define IDirect3DTexture9_AddRef(p) (assert(0),(void)(p),0u)"}
${textureBindings?"#define IDirect3DCubeTexture9_AddRef cube_add_ref\n#define IDirect3DCubeTexture9_Release cube_release":"#define IDirect3DCubeTexture9_AddRef(p) (assert(0),(void)(p),0u)\n#define IDirect3DCubeTexture9_Release(p) (assert(0),(void)(p),0u)"}
#define IDirect3D9_Release(p) (--((Parent*)(p))->refs)
${bufferBindings?"static ULONG vb_add_ref(Interface*);static ULONG vb_release(Interface*);static ULONG ib_add_ref(Interface*);static ULONG ib_release(Interface*);\n#define IDirect3DVertexBuffer9_AddRef vb_add_ref\n#define IDirect3DVertexBuffer9_Release vb_release\n#define IDirect3DIndexBuffer9_AddRef ib_add_ref\n#define IDirect3DIndexBuffer9_Release ib_release":"#define IDirect3DVertexBuffer9_Release(p) ((void)(p),assert(0),0u)\n#define IDirect3DIndexBuffer9_Release(p) ((void)(p),assert(0),0u)"}
${textureBindings?"#define IDirect3DVolumeTexture9_AddRef volume_texture_add_ref\n#define IDirect3DVolumeTexture9_Release volume_texture_release":"#define IDirect3DVolumeTexture9_AddRef(p) ((void)(p),assert(0),0u)\n#define IDirect3DVolumeTexture9_Release(p) ((void)(p),assert(0),0u)"}
${volumeBindings?"static ULONG volume_add_ref(Interface*);\n#define IDirect3DVolume9_AddRef volume_add_ref":""}
#define IDirect3DVertexDeclaration9_Release(p) ((void)(p),assert(0),0u)
#define IDirect3DVertexShader9_Release(p) ((void)(p),assert(0),0u)
#define IDirect3DPixelShader9_Release(p) ((void)(p),assert(0),0u)
static void device_release_owned_references(D9Device*);
static void device_cancel_recording(D9Device*d){(void)d;}
static void SetRect(RECT*r,int l,int t,int right,int b){*r=(RECT){l,t,right,b};}
static HRESULT IDirect3DDevice9_SetViewport(Interface*p,const D3DVIEWPORT9*v){(void)p;assert(v->Width==32&&(v->Height==24${cubeBindings?"||v->Height==32":""}));return 0;}
static HRESULT IDirect3DDevice9_SetScissorRect(Interface*p,const RECT*r){(void)p;assert(r->right==32&&(r->bottom==24${cubeBindings?"||r->bottom==32":""}));return 0;}
static HRESULT IDirect3DDevice9_GetBackBuffer(Interface*p,UINT a,UINT b,UINT c,Interface**out){(void)p;(void)a;(void)b;(void)c;(void)out;assert(0);return -1;}
static void swap_chain_destroy_storage(D9SwapChain*c){(void)c;assert(0);}
static void d9wg_light_free(void**p){(void)p;}static void d9wg_palette_free(void*p){(void)p;}static void unwatch_window_state(D9Device*d){(void)d;}static void restore_display_mode(D9Device*d){(void)d;}
static void SelectObject(void*a,void*b){(void)a;(void)b;}static void DeleteDC(void*p){(void)p;}static void DeleteObject(void*p){(void)p;}
static void EnterCriticalSection(int*p){(void)p;}static void LeaveCriticalSection(int*p){(void)p;}static BOOL submit_batch_locked(BOOL present){assert(!present);return 1;}
static UINT allocate_handle(void){return ++handles;}
static BOOL texture_level_layout(D3DFORMAT f,UINT w,UINT h,UINT*p,UINT*r,UINT*b){if(fail_layout||f!=22)return 0;*p=w*4;*r=h;*b=w*h*4;return 1;}
static BOOL emit_texture_create(D9Device*d,D9Texture*t){assert(t->device==d&&t->width&&t->height&&t->level_count==1&&t->multisample_quality==0);creates++;return !fail_emit;}
static BOOL emit_command(UINT op,const void*p,UINT n){assert(active_device&&(op==D9WG_OP_DESTROY_RESOURCE&&((const D9WGDestroyResource*)p)->resource_kind==0?active_device->refcount==0:active_device->refcount>0));if(op==D9WG_OP_SET_STREAM_SOURCE){assert(n==sizeof(D9WGSetStreamSource));return !fail_emit;}if(op==D9WG_OP_SET_INDICES){assert(n==sizeof(D9WGSetIndices));return !fail_emit;}if(op==D9WG_OP_SET_TEXTURE){assert(n==sizeof(D9WGSetTexture));return !fail_emit;}if(op==D9WG_OP_SET_RENDER_TARGET){assert(n==sizeof(D9WGSetRenderTarget));return 1;}if(op==D9WG_OP_SET_DEPTH_STENCIL_SURFACE_LEVEL){assert(n==sizeof(D9WGSetDepthStencilSurfaceLevel));return 1;}assert(op==D9WG_OP_DESTROY_RESOURCE&&n==sizeof(D9WGDestroyResource));const D9WGDestroyResource*c=p;if(c->resource_kind>=1&&c->resource_kind<=5)destroys++;else{assert(!c->resource_kind&&c->resource_handle==7&&!active_device->vertex_buffers&&!active_device->index_buffers&&!active_device->texture_resources&&!active_device->cube_textures&&!active_device->volume_textures&&!active_device->depth_stencil_surface&&!active_device->child_parent_refs);for(UINT i=0;i<4;i++)assert(!active_device->render_target_surfaces[i]);device_destroys++;}return 1;}

${["device_add_ref", "device_child_add_ref", "device_child_release", "device_release", "surface_is_implicit", "surface_uses_private_bindings", "reset_buffer_owned_refs", "reset_bound_surface", "reset_surface_owned_refs", "reset_texture_owned_refs", "device_has_reset_blockers", "device_clear_bindings", "device_release_owned_references", "vb_destroy_storage", "vb_binding_add_ref", "vb_binding_release", "ib_destroy_storage", "ib_binding_add_ref", "ib_binding_release", "volume_texture_destroy_storage", "volume_texture_binding_add_ref", "volume_texture_binding_release", "cube_destroy_storage", "cube_binding_add_ref", "cube_binding_release", "texture_destroy_storage", "texture_binding_add_ref", "texture_binding_release", "texture_release", "surface_binding_add_ref", "surface_binding_release", "surface_destroy_storage", "surface_add_ref", "surface_release", "create_target_texture", "surface_target_handle", "device_set_render_target", "device_get_render_target", "device_set_depth_stencil_surface", "device_get_depth_stencil_surface", ...(textureBindings?["texture_binding_slot","texture_add_ref","cube_add_ref","cube_release","volume_texture_add_ref","volume_texture_release","texture_get_surface_level","device_set_texture","device_get_texture",...(cubeBindings?["cube_get_cube_map_surface"]:[]),...(volumeBindings?["volume_add_ref","volume_release","volume_texture_get_volume_level"]:[]),...(bufferBindings?["vb_add_ref","vb_release","ib_add_ref","ib_release","device_set_stream_source","device_get_stream_source","device_set_indices","device_get_indices","clear_stream_zero_after_up","clear_indices_after_indexed_up"]:[])]:[])].map(n => extract(source, n)).join("\n")}
#define CHECK(v) do{if(!(v)||release_enters!=release_exits){fprintf(stderr,"Target binding line %u: %s release markers=%u/%u\\n",__LINE__,#v,release_enters,release_exits);return 1;}checks++;}while(0)
static D9Device*new_device(Parent*p){D9Device*d=HeapAlloc(NULL,8,sizeof(*d));d->refcount=1;d->handle=7;d->parent=p;p->refs=1;active_device=d;return d;}
static HRESULT get_bound(D9Device*d,BOOL depth,Interface**out){return depth?device_get_depth_stencil_surface(&d->iface,out):device_get_render_target(&d->iface,3,out);}
static HRESULT bind_target(D9Device*d,BOOL depth,D9Surface*s){return depth?device_set_depth_stencil_surface(&d->iface,s?&s->iface:NULL):device_set_render_target(&d->iface,3,s?&s->iface:NULL);}
${textureBindings?'#include "tests/native/d9wg_texture_binding_cases.h"':""}
${cubeBindings?'#include "tests/native/d9wg_cube_binding_cases.h"':""}
${volumeBindings?'#include "tests/native/d9wg_volume_binding_cases.h"':""}
${bufferBindings?'#include "tests/native/d9wg_buffer_binding_cases.h"':""}
int main(void){(void)&vb_binding_add_ref;(void)&vb_binding_release;(void)&ib_binding_add_ref;(void)&ib_binding_release;(void)&volume_texture_binding_add_ref;(void)&volume_texture_binding_release;(void)&cube_binding_add_ref;(void)&cube_binding_release;(void)&texture_binding_add_ref;(void)&texture_binding_release;${textureBindings?"CHECK(!texture_binding_cases());":""}${cubeBindings?"CHECK(!cube_binding_cases());":""}${volumeBindings?"CHECK(!volume_binding_cases());":""}${bufferBindings?"CHECK(!buffer_binding_cases());":""}(void)surface_destroy_storage;(void)surface_binding_add_ref;(void)surface_binding_release;unsigned cases=0,revivals=0;for(UINT profile=0;profile<4;profile++)for(UINT order=0;order<4;order++){
 Parent parent={0};D9Device*d=new_device(&parent);BOOL depth=profile>=2;UINT bindings=depth?1:order%2?1:2;Interface*out=NULL;UINT old_destroys=destroys;
 CHECK(!create_target_texture(d,32,24,depth?75:22,depth?2:1,profile%2?4:0,0,&out)&&out);D9Surface*s=(D9Surface*)out;
 CHECK(!bind_target(d,depth,s));if(bindings==2)CHECK(!device_set_render_target(&d->iface,2,out));
 CHECK(s->refcount==1&&s->binding_refs==(LONG)bindings&&d->refcount==2&&d->child_parent_refs==1&&device_has_reset_blockers(d));
 CHECK(!surface_release(out)&&s->refcount==0&&d->refcount==1&&!d->child_parent_refs&&s->texture->refcount==1&&!device_has_reset_blockers(d));
 CHECK(!surface_release(out)&&s->refcount==0&&d->refcount==1&&destroys==old_destroys);
 for(UINT cycle=0;cycle<64;cycle++){
  Interface*q=NULL;CHECK(!get_bound(d,depth,&q)&&q==out&&s->refcount==1&&d->refcount==2&&d->child_parent_refs==1);
  CHECK(surface_add_ref(q)==2&&d->refcount==2&&surface_release(q)==1&&d->refcount==2);
  CHECK(!surface_release(q)&&s->refcount==0&&d->refcount==1&&!d->child_parent_refs&&destroys==old_destroys);
  CHECK(!bind_target(d,depth,s)&&s->refcount==0&&s->binding_refs==(LONG)bindings&&d->refcount==1&&!device_has_reset_blockers(d));revivals++;
 }
 s->texture->levels[0].locked=1;CHECK(device_has_reset_blockers(d));s->texture->levels[0].locked=0;CHECK(!device_has_reset_blockers(d));
 if(order==0){if(bindings==2)CHECK(!device_set_render_target(&d->iface,2,NULL)&&s->binding_refs==1);CHECK(!bind_target(d,depth,NULL)&&destroys==old_destroys+1&&d->refcount==1&&!d->texture_resources&&live==1);CHECK(!device_release(&d->iface)&&!live&&!parent.refs);}
 if(order==1){CHECK(!device_release(&d->iface)&&!live&&!parent.refs&&destroys==old_destroys+1);}
 if(order==2){Interface*q=NULL;CHECK(!get_bound(d,depth,&q)&&q==out&&d->refcount==2);device_clear_bindings(d);CHECK(s->refcount==1&&!s->binding_refs&&d->refcount==2&&destroys==old_destroys);CHECK(device_release(&d->iface)==1&&d->child_parent_refs==1);CHECK(!surface_release(q)&&!live&&!parent.refs&&destroys==old_destroys+1);}
 if(order==3){device_clear_bindings(d);CHECK(d->refcount==1&&!d->texture_resources&&destroys==old_destroys+1&&live==1);CHECK(!device_release(&d->iface)&&!live&&!parent.refs);}
 cases++;
}
D9Surface s={0};D9Texture t={0};D9SwapChain chain={0};CHECK(surface_uses_private_bindings(&s));s.texture_child=1;CHECK(!surface_uses_private_bindings(&s));s.texture_child=0;s.cube_texture=&t;CHECK(!surface_uses_private_bindings(&s));s.cube_texture=NULL;s.swap_chain=&chain;CHECK(surface_uses_private_bindings(&s));chain.handle=1;CHECK(surface_uses_private_bindings(&s));s.swap_chain=NULL;s.auto_depth_stencil=1;CHECK(surface_uses_private_bindings(&s));
printf("PASS actual D3D9 target bindings: %u checks, %u lifecycle groups, %u public-zero revivals; actual RT/depth setters/getters, one/two internal slots, one public device pin, public-only Reset blockers, same-object rebinding and unbind/device-first/last-public cleanup\\n",checks,cases,revivals);return 0;}

`);
    const b = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", "-I", root, dir + "/target.c", root + "/runtime/d9wg_private_data.c", "-o", dir + "/target"], {encoding: "utf8"}); assert.equal(b.status, 0, b.stderr);
    const r = spawnSync(dir + "/target", [], {encoding: "utf8", timeout: 10000}); assert.equal(r.status, 0, r.stderr + " signal=" + r.signal); process.stdout.write(r.stdout);
} finally {fs.rmSync(dir, {recursive: true, force: true});}
