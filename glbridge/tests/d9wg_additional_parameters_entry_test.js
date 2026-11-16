"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-additional-parameters-"));
function extract(s, name) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name);
    let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    return s.slice(m.index, end);
}
try {
    for (const api of [8, 9]) {
        const source = readComparisonSource(process.env["D" + api + "WG_ADDITIONAL_PARAMETERS_SOURCE"] || root + "/d3d" + api + "proxy/d3d" + api + "_proxy.c", "utf8");
        fs.writeFileSync(dir + "/additional.c", `#include <assert.h>
#include <stdint.h>
/* No frontend views are attached in this isolated lifetime fixture. */
#define core_view_retire8(object) ((void)(object))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
#include "runtime/d9wg_present_pacing.h"
typedef uint32_t UINT,DWORD,ULONG;typedef int32_t LONG,HRESULT;typedef int BOOL,D3DFORMAT;typedef void*HWND;typedef struct{int left,top,right,bottom;}RECT;typedef struct{int x,y;}POINT;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define ZeroMemory(p,n) memset(p,0,n)
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_NOTAVAILABLE (-2)
#define E_OUTOFMEMORY (-3)
#define D3DERR_DRIVERINTERNALERROR (-4)
#define D3DFMT_UNKNOWN 0
#define D3DMULTISAMPLE_NONE 0
#define D3DMULTISAMPLE_4_SAMPLES 4
#define D3DUSAGE_RENDERTARGET 1
#define HEAP_ZERO_MEMORY 1
#define D3DPRESENTFLAG_LOCKABLE_BACKBUFFER 1
#define SUCCEEDED(v) ((v)>=0)
#define FAILED(v) ((v)<0)
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
#define TRACE_MARK_ENTER(...)
#define TRACE_MARK_EXIT(...)
#define HOSTLOG_REFUSED(...)
#define HOSTLOG_FAILED(...)
typedef struct{UINT BackBufferWidth,BackBufferHeight,BackBufferCount,MultiSampleType,MultiSampleQuality,BackBufferFormat;BOOL Windowed;DWORD FullScreen_PresentationInterval,PresentationInterval,SwapEffect,Flags;HWND hDeviceWindow;}D3DPRESENT_PARAMETERS;
#include "runtime/d9wg_present_parameters.h"
/* Logical PP provider;real packed SDK8/9 bridge is independently tested. */
${api===8?'static unsigned core_parameter_calls;static void d9wg_d3d9_core_parameters_effective8(void*out,const void*in,UINT w,UINT h,UINT f,HWND window){core_parameter_calls++;d9wg_present_parameters_effective(out,in,w,h,f,window);}':''}
typedef struct{void*lpVtbl;}Interface;typedef Interface IDirect3DDevice${api},IDirect3DSwapChain${api},IDirect3DSurface${api};
typedef struct Device Device;typedef struct Chain Chain;typedef struct{Interface iface;UINT handle;BOOL private_back_buffer;Chain*swap_chain;}Texture;typedef struct{Interface iface;Device*device;Texture*texture;BOOL additional_back_buffer,lockable_render_target;LONG refcount,binding_refs;Chain*swap_chain;UINT width,height;}Surface;typedef struct{LONG refs;unsigned store;}Owner;
struct Chain{Interface iface;LONG refcount;Device*device;D3DPRESENT_PARAMETERS present;Owner*private_data_owner;UINT handle;HWND window;Interface*back_buffer;Surface*cached_buffers[4];Chain*next_device_chain;};
struct Device{Interface iface;unsigned refs,pins;UINT handle;D3DPRESENT_PARAMETERS present;struct{UINT Width,Height,Format;}display_mode;struct{HWND hFocusWindow;}creation;UINT additional_swap_chain_count;Chain*additional_swap_chains;};
typedef Chain D${api}SwapChain;typedef Device D${api}Device;typedef Surface D${api}Surface;
static unsigned allocations,live,fail_at,fail_emit,emits,destroys;static UINT next_handle=100;static int g_chain_vtbl;static HWND observed_window;static Texture backing_texture={.handle=99};static Surface*latest_surface;static UINT last_texture_format,last_ms;static BOOL last_lockable;
#define g_swapchain_vtbl g_chain_vtbl
#define g_swap_chain_vtbl g_chain_vtbl
static Device*device_from_iface(Interface*p){return (Device*)p;}static Chain*swap_chain_from_iface(Interface*p){return (Chain*)p;}static Surface*surface_from_iface(Interface*p){return (Surface*)p;}
static void*GetProcessHeap(void){return NULL;}static void*HeapAlloc(void*h,UINT f,size_t n){(void)h;assert(f==1&&n);allocations++;if(allocations==fail_at)return NULL;void*p=calloc(1,n);assert(p);live++;return p;}
static void HeapFree(void*h,UINT f,void*p){(void)h;(void)f;assert(p&&live);live--;free(p);}
static LONG InterlockedDecrement(LONG*p){assert(*p>0);return --*p;}static LONG InterlockedCompareExchange(LONG*p,LONG v,LONG c){LONG old=*p;if(old==c)*p=v;return old;}
static void device_child_add_ref(Device*d){d->refs++;d->pins++;}static void device_child_release(Device*d){assert(d->pins&&d->refs>1);d->pins--;d->refs--;}
static void private_data_owner_release(Owner*p){assert(p->refs==1);HeapFree(NULL,0,p);}
static void surface_release(Interface*p){Surface*s=(Surface*)p;Device*d=s->device;UINT pins=${api===9?'1':'!s->texture->private_back_buffer'};HeapFree(NULL,0,s);while(pins--)device_child_release(d);}
${api===9?'static void surface_destroy_storage(Surface*s){HeapFree(NULL,0,s);}':''}
#define IDirect3DSurface9_Release surface_release
#define IDirect3DSurface8_Release surface_release
${api===8?`static void swapchain_destroy_storage(Chain*);
static void texture_release(Interface*p){assert(p==&backing_texture.iface);Chain*c=backing_texture.swap_chain;backing_texture.swap_chain=NULL;if(c)swapchain_destroy_storage(c);surface_release(&latest_surface->iface);}
#define IDirect3DTexture8_Release texture_release`:""}
${api===9?'static UINT desktop_format=22,desktop_queries;static UINT d9wg_display_mode_backbuffer_format(UINT f){if(f)return f;desktop_queries++;return desktop_format;}':''}
${api===9?'static UINT device_compatibility_api(const Device*d){(void)d;return 9;}static BOOL supported_render_target_format8(D3DFORMAT f){return f>=21&&f<=26;}':''}
static BOOL supported_multisample_type(UINT t){return t==0||t==4;}static BOOL supported_backbuffer_format(D3DFORMAT f){return f==22;}static BOOL supported_multisample(D3DFORMAT f,UINT ms){return (f==21||f==22||f==23)&&(!ms||ms==4);}
static void SetRect(RECT*r,int a,int b,int c,int d){*r=(RECT){a,b,c,d};}
static void GetClientRect(HWND w,RECT*r){observed_window=w;assert(w==(void*)(uintptr_t)0x44||w==(void*)(uintptr_t)0x55);SetRect(r,0,0,w==(void*)(uintptr_t)0x55?96:128,w==(void*)(uintptr_t)0x55?80:96);}
static void probe_window_rect(HWND w,int fw,int fh,RECT*r,POINT*p){assert(fw==128&&fh==96);GetClientRect(w,r);*p=(POINT){30,40};}
static HRESULT create_target_texture(Device*d,UINT w,UINT h,D3DFORMAT f,UINT usage,UINT ms,UINT quality,Interface**out){assert((f==21||f==22||f==23)&&usage==1&&(!ms||ms==4)&&!quality);last_ms=ms;last_texture_format=f;backing_texture.private_back_buffer=${api===9?1:0};backing_texture.swap_chain=NULL;*out=NULL;Surface*s=HeapAlloc(NULL,1,sizeof(*s));if(!s)return E_OUTOFMEMORY;*s=(Surface){.device=d,.texture=&backing_texture,.width=w,.height=h};device_child_add_ref(d);latest_surface=s;*out=&s->iface;return D3D_OK;}
static HRESULT device_create_render_target(Interface*p,UINT w,UINT h,D3DFORMAT f,UINT ms,BOOL lockable,Interface**out){last_ms=ms;last_lockable=lockable;if(lockable&&ms)return D3DERR_INVALIDCALL;return create_target_texture((Device*)p,w,h,f,1,ms,0,out);}
static void ClientToScreen(HWND w,POINT*p){assert(w==observed_window);*p=(POINT){30,40};}
static UINT allocate_handle(void){return next_handle++;}
static BOOL emit_command(UINT op,const void*p,UINT n){if(op==D9WG_OP_CREATE_SWAP_CHAIN){const D9WGCreateSwapChain*c=p;assert(n==sizeof(*c)&&c->device_handle==77&&c->back_buffer_handle==99&&c->hwnd==(uint32_t)(uintptr_t)observed_window&&c->width==latest_surface->width&&c->height==latest_surface->height);emits++;return !fail_emit;}const D9WGDestroySwapChain*c=p;assert(op==D9WG_OP_DESTROY_SWAP_CHAIN&&n==sizeof(*c)&&c->device_handle==77);destroys++;return 1;}
${api===8&&!source.includes("static void swapchain_destroy_storage(")?"static void swapchain_destroy_storage(Chain*c){(void)c;assert(0);}":""}
${[...(api===8&&source.includes("static void swapchain_destroy_storage(")?["swapchain_destroy_storage"]:[]),api===8?"device_create_swapchain":"device_create_additional_swap_chain",...(api===9&&source.includes("static void swap_chain_destroy_storage(")?["swap_chain_buffers_referenced","swap_chain_destroy_cached_buffers","swap_chain_destroy_storage"]:[]),api===8?"swapchain_release":"swap_chain_release"].map(n=>extract(source,n)).join("\n")}
#define CREATE(d,p,o) ${api===8?"device_create_swapchain(&(d)->iface,p,o)":"device_create_additional_swap_chain(&(d)->iface,p,o)"}
#define RELEASE(c) ${api===8?"swapchain_release(c)":"swap_chain_release(c)"}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Additional parameters API ${api} line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){unsigned checks=0,cases=0;const UINT counts[]={0,1,3};Device d={.refs=1,.handle=77,.present={.SwapEffect=1,.Windowed=1},.display_mode={128,96,22},.creation={.hFocusWindow=(void*)(uintptr_t)0x44}};
for(unsigned window=0;window<2;window++)for(unsigned w=0;w<2;w++)for(unsigned h=0;h<2;h++)for(unsigned n=0;n<3;n++){
 D3DPRESENT_PARAMETERS p={.SwapEffect=1,.Windowed=1,.BackBufferWidth=w?80:0,.BackBufferHeight=h?72:0,.BackBufferFormat=22,.BackBufferCount=counts[n],.Flags=n&1,.hDeviceWindow=window?(void*)(uintptr_t)0x55:NULL},input=p;UINT width=w?80:window?96:128,height=h?72:window?80:96;Interface*out=(void*)(uintptr_t)1;
 ${api===8?'unsigned before_core=core_parameter_calls;':''}CHECK(CREATE(&d,&p,&out)==D3D_OK&&out${api===8?'&&core_parameter_calls==before_core+1':''});Chain*c=(Chain*)out;${api===9?'CHECK(((Surface*)c->back_buffer)->lockable_render_target==!!(p.Flags&1));':''}
 CHECK(c->present.BackBufferWidth==width&&c->present.BackBufferHeight==height&&c->present.BackBufferCount==(counts[n]?counts[n]:1)&&c->present.BackBufferFormat==22&&c->present.hDeviceWindow==(window?(void*)(uintptr_t)0x55:d.creation.hFocusWindow));
 input.BackBufferCount=counts[n]?counts[n]:1;${api===9?'input.BackBufferWidth=width;input.BackBufferHeight=height;':''}CHECK(!memcmp(&p,&input,sizeof(p)));
 CHECK(d.refs==2&&d.pins==1&&${api===8?'d.additional_swap_chain_count==1&&((Surface*)c->back_buffer)->additional_back_buffer&&((Surface*)c->back_buffer)->texture->swap_chain==c':'d.additional_swap_chains==c&&((Surface*)c->back_buffer)->swap_chain==c&&!((Surface*)c->back_buffer)->refcount'});
 CHECK(RELEASE(out)==0&&!live&&d.refs==1&&!d.pins&&${api===8?'!d.additional_swap_chain_count':'!d.additional_swap_chains'});cases++;
}
${api===8?`{
 D3DPRESENT_PARAMETERS p={.SwapEffect=1,.Windowed=1,.BackBufferWidth=32,.BackBufferHeight=24,.BackBufferFormat=22,.BackBufferCount=1,.MultiSampleType=4,.Flags=1},input=p;Interface*out=NULL;
 CHECK(!CREATE(&d,&p,&out)&&out&&last_ms==4&&!last_lockable);Chain*c=(Chain*)out;${api===9?'CHECK(((Surface*)c->back_buffer)->lockable_render_target==!!(p.Flags&1));':''}
 CHECK(c->present.MultiSampleType==4&&c->present.Flags==1&&!memcmp(&p,&input,sizeof(p)));
 CHECK(d.refs==2&&d.pins==1&&d.additional_swap_chain_count==1);
 CHECK(!RELEASE(out)&&!live&&d.refs==1&&!d.pins&&!d.additional_swap_chain_count);cases++;
}`:''}
for(unsigned failure=0;failure<3;failure++){
 fail_at=failure<2?allocations+failure+1:0;fail_emit=failure==2;D3DPRESENT_PARAMETERS p={.SwapEffect=1,.Windowed=1,.BackBufferFormat=22},input=p;Interface*out=(void*)(uintptr_t)1;
 CHECK(CREATE(&d,&p,&out)==(failure<2?E_OUTOFMEMORY:D3DERR_DRIVERINTERNALERROR)&&!out&&!live&&d.refs==1&&!d.pins&&!memcmp(&p,&input,sizeof(p)));
}
fail_at=fail_emit=0;D3DPRESENT_PARAMETERS p={.SwapEffect=1,.Windowed=1,.BackBufferFormat=22,.BackBufferCount=4},input=p;Interface*out=(void*)(uintptr_t)1;unsigned before=allocations;
CHECK(CREATE(&d,&p,&out)==D3DERR_INVALIDCALL&&!out&&allocations==before&&!memcmp(&p,&input,sizeof(p))&&d.refs==1);
${api===9?`{
 D3DPRESENT_PARAMETERS p={.SwapEffect=1,.Windowed=1,.BackBufferWidth=32,.BackBufferHeight=24,.BackBufferFormat=22,.BackBufferCount=1,.MultiSampleType=4},input=p;Interface*out=NULL;
 CHECK(!CREATE(&d,&p,&out)&&out&&last_ms==4);Chain*c=(Chain*)out;${api===9?'CHECK(((Surface*)c->back_buffer)->lockable_render_target==!!(p.Flags&1));':''}
 CHECK(c->present.MultiSampleType==4&&!c->present.MultiSampleQuality&&!memcmp(&p,&input,sizeof(p)));
 CHECK(d.refs==2&&d.pins==1&&d.additional_swap_chains==c);
 CHECK(!RELEASE(out)&&!live&&d.refs==1&&!d.pins&&!d.additional_swap_chains);cases++;
 const UINT unsupported[]={1,2,16,0xffffffffu};for(UINT i=0;i<4;i++){p=input;p.MultiSampleType=unsupported[i];input=p;out=(void*)(uintptr_t)1;UINT old_allocations=allocations;CHECK(CREATE(&d,&p,&out)==D3DERR_INVALIDCALL&&!out&&old_allocations==allocations&&!memcmp(&p,&input,sizeof(p))&&d.refs==1&&!d.pins);}
}
 d.display_mode.Format=21;
for(UINT profile=0;profile<3;profile++){
 desktop_format=profile?23:22;UINT requested_format=profile==2?21:0,wanted_format=profile==2?21:desktop_format,old_queries=desktop_queries;
 D3DPRESENT_PARAMETERS p={.SwapEffect=1,.Windowed=1,.BackBufferWidth=64,.BackBufferHeight=48,.BackBufferFormat=requested_format};Interface*out=NULL;
 CHECK(!CREATE(&d,&p,&out)&&out);Chain*c=(Chain*)out;${api===9?'CHECK(((Surface*)c->back_buffer)->lockable_render_target==!!(p.Flags&1));':''}
 CHECK(p.BackBufferFormat==wanted_format&&c->present.BackBufferFormat==wanted_format&&last_texture_format==wanted_format&&desktop_queries==old_queries+(requested_format==0));
 CHECK(!RELEASE(out)&&!live&&d.refs==1&&!d.pins);cases++;
}`:''}
printf("PASS actual API ${api} additional parameters: %u checks, %u constructors; separate/fallback windows, independent zero dimensions, count bounds, version outputs and failure cleanup\\n",checks,cases);return 0;}
`);
        const build = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable", "-Wno-unused-function", "-I", root, dir + "/additional.c", root + "/runtime/d9wg_present_pacing.c", "-o", dir + "/additional"], {encoding: "utf8"}); assert.equal(build.status, 0, build.stderr);
        const run = spawnSync(dir + "/additional", [], {encoding: "utf8"}); assert.equal(run.status, 0, run.stdout + run.stderr + " signal=" + run.signal); process.stdout.write(run.stdout);
    }
} finally {fs.rmSync(dir, {recursive: true, force: true});}
