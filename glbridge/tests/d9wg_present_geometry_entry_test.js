"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-present-geometry-"));
function extract(s, name) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name);
    let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    return s.slice(m.index, end);
}
try {
    for (const api of [8, 9]) {
        const source = fs.readFileSync(process.env["D" + api + "WG_GEOMETRY_SOURCE"] || root + "/d3d" + api + "proxy/d3d" + api + "_proxy.c", "utf8");
        fs.writeFileSync(dir + "/geometry.c", `#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
#include "runtime/d9wg_present_pacing.h"
typedef uint32_t UINT,DWORD,ULONG;typedef int32_t HRESULT;typedef int BOOL,D3DFORMAT,D3DDEVTYPE;typedef uint8_t BYTE;typedef void*HWND;
typedef struct{int left,top,right,bottom;}RECT;typedef struct{int x,y;}POINT;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_NOTAVAILABLE (-2)
#define E_OUTOFMEMORY (-3)
#define D3DERR_DRIVERINTERNALERROR (-4)
#define D3DFMT_UNKNOWN 0
#define D3DFMT_X8R8G8B8 22
#define D3DDEVTYPE_HAL 1
#define D3DCREATE_FPU_PRESERVE 2
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING 0x20
#define D3DCREATE_HARDWARE_VERTEXPROCESSING 0x40
#define D3DCREATE_MIXED_VERTEXPROCESSING 0x80
#define D3DCREATE_PUREDEVICE 0x10
#define HEAP_ZERO_MEMORY 1
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
#define HOSTLOG_FAILED(...)
#define ZeroMemory(p,n) memset(p,0,n)
typedef struct{UINT BackBufferWidth,BackBufferHeight,BackBufferCount,MultiSampleType,MultiSampleQuality,AutoDepthStencilFormat,BackBufferFormat;DWORD FullScreen_PresentationInterval,PresentationInterval,SwapEffect,Flags,FullScreen_RefreshRateInHz;BOOL Windowed,EnableAutoDepthStencil;HWND hDeviceWindow;}D3DPRESENT_PARAMETERS;
#include "${process.env.D9WG_GEOMETRY_POLICY || root + "/runtime/d9wg_present_parameters.h"}"
typedef struct{void*lpVtbl;}Interface;typedef struct{Interface iface;unsigned refs;}Factory;
typedef struct{UINT Width,Height,Format,RefreshRate;}Mode;
typedef struct Device{Interface iface;unsigned refcount;Factory*parent;UINT handle;D3DPRESENT_PARAMETERS present;struct{UINT AdapterOrdinal;D3DDEVTYPE DeviceType;HWND hFocusWindow;DWORD BehaviorFlags;}creation;Mode display_mode;struct{UINT X,Y,Width,Height;float MinZ,MaxZ;}viewport;RECT scissor_rect;BOOL depth_surface_enabled;BYTE*front_shadow;UINT front_shadow_pitch;int palettes,material,render_states[4];struct{Interface iface;struct Device*device;}implicit_swap_chain;}Device;
typedef Factory D${api}Direct3D;typedef Device D${api}Device;typedef Interface IDirect3D${api},IDirect3DDevice${api};
static int g_device_vtbl,g_swap_chain_vtbl;static Device*active;static unsigned live,allocations,fail_at,parent_calls,front_bytes,emits,fail_emit,fail_states,fail_mode;static int client_width,client_height;static HWND observed_window;
static Factory*d3d_from_iface(Interface*p){return (Factory*)p;}static void*GetProcessHeap(void){return NULL;}
static void*HeapAlloc(void*h,UINT f,size_t n){(void)h;assert(f==1&&n);allocations++;if(allocations==fail_at)return NULL;void*p=calloc(1,n);assert(p);live++;if(!active)active=p;else front_bytes=(unsigned)n;return p;}
static void HeapFree(void*h,UINT f,void*p){(void)h;(void)f;if(!p)return;assert(live);live--;if(p==active)active=NULL;free(p);}
static ULONG factory_add_ref(Interface*p){parent_calls++;return ++((Factory*)p)->refs;}static ULONG factory_release(Interface*p){assert(((Factory*)p)->refs>1);return --((Factory*)p)->refs;}
#define IDirect3D${api}_AddRef factory_add_ref
#define IDirect3D${api}_Release factory_release
static ULONG device_release(Interface*p){Device*d=(Device*)p;factory_release(&d->parent->iface);HeapFree(NULL,0,d->front_shadow);HeapFree(NULL,0,d);return 0;}
#define IDirect3DDevice${api}_Release device_release
static void d9wg_fpu_setup(BOOL p){assert(p);}
${api===9?'static UINT desktop_format=22,desktop_queries;static UINT d9wg_display_mode_backbuffer_format(UINT f){if(f)return f;desktop_queries++;return desktop_format;}':''}
static BOOL supported_multisample_type(UINT t){return t==0;}static BOOL supported_multisample(D3DFORMAT f,UINT t){return (f==21||f==22||f==23||f==75)&&!t;}
static BOOL supported_depth_stencil_format(D3DFORMAT f){return f==75;}static BOOL supported_backbuffer_format(D3DFORMAT f){return f==21||f==22||f==23;}
static UINT allocate_handle(void){return 77;}
static void SetRect(RECT*r,int a,int b,int c,int d){*r=(RECT){a,b,c,d};}
static void GetClientRect(HWND h,RECT*r){assert(h);observed_window=h;SetRect(r,0,0,client_width,client_height);}
static void ClientToScreen(HWND h,POINT*p){assert(h==observed_window);*p=(POINT){30,40};}
static void fill_display_mode(Mode*m,UINT w,UINT h,UINT f){*m=(Mode){w,h,f,60};}
static void device_init_states(Device*d){assert(d->present.BackBufferWidth==d->display_mode.Width&&d->present.BackBufferHeight==d->display_mode.Height&&d->viewport.Width==d->display_mode.Width);}
static BOOL texture_level_layout(D3DFORMAT f,UINT w,UINT h,UINT*p,UINT*r,UINT*b){assert(f==22);*p=w*4;*r=h;*b=w*h*4;return 1;}
static void d9wg_palette_free(int*p){(void)p;}
static BOOL emit_command(UINT op,const void*p,UINT size){const D9WGCreateDevice*c=p;assert(op==D9WG_OP_CREATE_DEVICE&&size==sizeof(*c)&&c->device_handle==active->handle&&c->width==active->display_mode.Width&&c->height==active->display_mode.Height);emits++;return !fail_emit;}
static BOOL emit_render_default_patches(UINT h,UINT a,BOOL depth,const int*p){assert(h==77&&a==${api}&&depth&&p==active->render_states);return !fail_states;}
static BOOL emit_object_default_patches(UINT h,const int*p){assert(h==77&&p==&active->material);return !fail_states;}
static BOOL reset_window_mode(Device*d,HWND w){assert(d==active&&w==observed_window);return !fail_mode;}
static void attach_device_window(Device*d,HWND w){assert(d==active&&w==observed_window);}static void watch_window_state(Device*d){assert(d==active);}
static BOOL emit_surface_update_and_flush(Device*d,HWND w,BOOL hidden,BOOL force){assert(d==active&&w==observed_window&&!hidden&&!force);return 1;}
static BOOL emit_window_state(Device*d,HWND w){assert(d==active&&w==observed_window);return 1;}
${extract(source, "d3d_create_device")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Present geometry API ${api} line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){unsigned checks=0,cases=0;Factory factory={.refs=1};const UINT widths[]={0,64,256},heights[]={0,48,128},counts[]={0,1,3};const int clients[][2]={{160,120},{257,93},{0,0}};const DWORD behavior=D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE;
for(unsigned w=0;w<3;w++)for(unsigned h=0;h<3;h++)for(unsigned c=0;c<3;c++)for(unsigned window=0;window<2;window++)for(unsigned count=0;count<3;count++){
 client_width=clients[c][0];client_height=clients[c][1];D3DPRESENT_PARAMETERS p={.SwapEffect=1,.Windowed=1,.BackBufferWidth=widths[w],.BackBufferHeight=heights[h],.BackBufferCount=counts[count],.BackBufferFormat=22,.EnableAutoDepthStencil=1,.AutoDepthStencilFormat=75,.hDeviceWindow=window?(void*)(uintptr_t)0x55:NULL};D3DPRESENT_PARAMETERS input=p;
 UINT ew=widths[w]?widths[w]:client_width?(UINT)client_width:640,eh=heights[h]?heights[h]:client_height?(UINT)client_height:480;HWND selected=window?p.hDeviceWindow:(void*)(uintptr_t)0x44;Interface*out=(void*)(uintptr_t)1;unsigned before_emits=emits;
 CHECK(!d3d_create_device(&factory.iface,0,1,(void*)(uintptr_t)0x44,behavior,&p,&out)&&out&&out==&active->iface);Device*d=(Device*)out;
 CHECK(d->display_mode.Width==ew&&d->display_mode.Height==eh&&d->viewport.Width==ew&&d->viewport.Height==eh&&d->viewport.MinZ==0&&d->viewport.MaxZ==1);
 CHECK(d->present.BackBufferWidth==ew&&d->present.BackBufferHeight==eh&&d->present.BackBufferCount==(counts[count]?counts[count]:1)&&d->present.hDeviceWindow==selected&&observed_window==selected);
 CHECK(emits==before_emits+1&&factory.refs==2);
 ${api===8?'CHECK(d->front_shadow&&d->front_shadow_pitch==ew*4&&front_bytes==ew*eh*4);':'CHECK(d->scissor_rect.right==(int)ew&&d->scissor_rect.bottom==(int)eh&&d->implicit_swap_chain.device==d);'}
 input.BackBufferCount=counts[count]?counts[count]:1;${api===9?'input.BackBufferWidth=ew;input.BackBufferHeight=eh;':''}CHECK(!memcmp(&p,&input,sizeof(p)));
 CHECK(!device_release(out)&&!live&&!active&&factory.refs==1);cases++;
}
client_width=160;client_height=120;
for(unsigned failure=0;failure<5;failure++){
 if(${api}==9&&failure==1)continue;fail_emit=failure==2;fail_states=failure==3;fail_mode=failure==4;fail_at=allocations+(failure==0?1:failure==1?2:0);
 D3DPRESENT_PARAMETERS p={.SwapEffect=1,.Windowed=1,.BackBufferFormat=22,.EnableAutoDepthStencil=1,.AutoDepthStencilFormat=75},before=p;Interface*out=(void*)(uintptr_t)1;HRESULT expected=failure<=1?E_OUTOFMEMORY:failure==4?D3DERR_INVALIDCALL:D3DERR_DRIVERINTERNALERROR;
 CHECK(d3d_create_device(&factory.iface,0,1,(void*)(uintptr_t)0x44,behavior,&p,&out)==expected&&!out&&!live&&!active&&factory.refs==1&&!memcmp(&p,&before,sizeof(p)));
}
fail_emit=fail_states=fail_mode=fail_at=0;client_width=8193;D3DPRESENT_PARAMETERS p={.SwapEffect=1,.Windowed=1,.BackBufferFormat=22};Interface*out=(void*)(uintptr_t)1;unsigned before_allocations=allocations;
CHECK(d3d_create_device(&factory.iface,0,1,(void*)(uintptr_t)0x44,behavior,&p,&out)==D3DERR_INVALIDCALL&&!out&&!live&&allocations==before_allocations);
${api===9?`client_width=128;client_height=96;
for(UINT profile=0;profile<3;profile++){
 desktop_format=profile?23:22;UINT requested_format=profile==2?21:0,wanted_format=profile==2?21:desktop_format,old_queries=desktop_queries;
 D3DPRESENT_PARAMETERS p={.SwapEffect=1,.Windowed=1,.BackBufferWidth=64,.BackBufferHeight=48,.BackBufferFormat=requested_format,.EnableAutoDepthStencil=1,.AutoDepthStencilFormat=75};Interface*out=NULL;
 CHECK(!d3d_create_device(&factory.iface,0,1,(void*)(uintptr_t)0x44,behavior,&p,&out)&&out);
 CHECK(p.BackBufferFormat==wanted_format&&active->display_mode.Format==wanted_format&&active->present.BackBufferFormat==wanted_format&&desktop_queries==old_queries+(requested_format==0));
 CHECK(!device_release(out)&&!live&&factory.refs==1);cases++;
}
D3DPRESENT_PARAMETERS full={.SwapEffect=1,.BackBufferWidth=640,.BackBufferHeight=480},full_before=full;out=(void*)(uintptr_t)1;before_allocations=allocations;unsigned old_queries=desktop_queries;
CHECK(d3d_create_device(&factory.iface,0,1,(void*)(uintptr_t)0x44,behavior,&full,&out)==D3DERR_INVALIDCALL&&!out&&allocations==before_allocations&&desktop_queries==old_queries&&!memcmp(&full,&full_before,sizeof(full)));`:''}
printf("PASS actual API ${api} present geometry: %u checks, %u constructors; zero/explicit/client dimensions, fallback windows, count normalization, front storage/viewport, exact caller outputs and failure atomicity\\n",checks,cases);return 0;}
`);
        const build = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable", "-Wno-unused-function", "-I", root, "-I", root + "/runtime", dir + "/geometry.c", root + "/runtime/d9wg_present_pacing.c", "-o", dir + "/geometry"], {encoding: "utf8"}); assert.equal(build.status, 0, build.stderr);
        const run = spawnSync(dir + "/geometry", [], {encoding: "utf8"}); assert.equal(run.status, 0, run.stdout + run.stderr + " signal=" + run.signal); process.stdout.write(run.stdout);
    }
} finally {fs.rmSync(dir, {recursive: true, force: true});}
