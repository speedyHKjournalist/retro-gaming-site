"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, "..");
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-reset-status-"));
function extract(source, name) {
    const match = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source);
    assert(match, name);
    let end = source.indexOf("{", match.index) + 1, depth = 1;
    for (; depth; end++) {
        if (source[end] === "{") depth++;
        if (source[end] === "}") depth--;
    }
    return source.slice(match.index, end);
}
try {
    for (const api of [8, 9]) {
        const source = fs.readFileSync(process.env["D" + api + "WG_RESET_STATUS_FRONTEND"] || root + "/d3d" + api + "proxy/d3d" + api + "_proxy.c", "utf8");
        const functions = ["device_reset", "device_begin_scene", "device_end_scene", "device_test_cooperative_level", "device_present", api === 8 ? "swapchain_present" : "swap_chain_present"];
        fs.writeFileSync(dir + "/status.c", `#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "d3d9proxy/d3d9_protocol.h"
#include "runtime/d9wg_present_pacing.h"
#include "${process.env.D9WG_RESET_STATUS_POLICY || root + "/runtime/d9wg_reset.h"}"
typedef uint32_t UINT,DWORD; typedef int32_t HRESULT; typedef int BOOL,D3DFORMAT; typedef uint8_t BYTE;
typedef void* HWND; typedef struct {int left,top,right,bottom;} RECT; typedef struct {int x,y;} POINT; typedef RECT RGNDATA;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DFMT_UNKNOWN 0
#define HR(v) ((int32_t)UINT32_C(v))
#define D3DERR_INVALIDCALL HR(0x8876086c)
#define D3DERR_NOTAVAILABLE HR(0x8876086a)
#define D3DERR_DEVICELOST HR(0x88760868)
#define D3DERR_DEVICENOTRESET HR(0x88760869)
#define D3DERR_DRIVERINTERNALERROR HR(0x88760827)
#define E_OUTOFMEMORY HR(0x8007000e)
#define D3DERR_WASSTILLDRAWING HR(0x8876021c)
#define D3DPRESENT_DONOTWAIT 1
#define HEAP_ZERO_MEMORY 1
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
#define TRACE_MARK_ENTER(...)
#define TRACE_MARK_EXIT(...)
#define SUCCEEDED(v) ((v)>=0)
#define ZeroMemory(p,n) memset(p,0,n)
typedef struct {
 UINT BackBufferWidth,BackBufferHeight,BackBufferCount,MultiSampleType,MultiSampleQuality,AutoDepthStencilFormat,BackBufferFormat;
 DWORD FullScreen_PresentationInterval,PresentationInterval,SwapEffect;
 BOOL Windowed,EnableAutoDepthStencil; HWND hDeviceWindow;
} D3DPRESENT_PARAMETERS;
typedef struct {UINT Width,Height,Format,RefreshRate;} Mode;
typedef struct {UINT Width,Height,Format,MultiSampleType,Size;} Desc;
typedef struct {Desc desc;UINT row_pitch,width,height,format,multisample,multisample_quality;} Surface;
typedef struct Device Device;
struct Device {
 int iface; D9WGResetState reset_state; D9WGPresentClock present_clock; D9WGPresentPacing present_pacing;
 UINT handle,reset_epoch; BOOL blocked,in_scene; struct {HWND hFocusWindow;UINT BehaviorFlags;} creation;
 D3DPRESENT_PARAMETERS present; Mode display_mode; Surface *implicit_back_buffer,*implicit_depth_stencil;
 BYTE*front_shadow;UINT front_shadow_pitch; HWND tracked_window;
 struct {UINT X,Y,Width,Height;float MinZ,MaxZ;} viewport;
 BOOL depth_surface_enabled; RECT scissor_rect; int window_cache;
};
typedef struct {Device*device;UINT handle;D9WGPresentClock present_clock;D9WGPresentPacing present_pacing;D3DPRESENT_PARAMETERS present;} Chain;
typedef Device D${api}Device,IDirect3DDevice${api}; typedef Surface D${api}Surface; typedef Chain D${api}SwapChain,IDirect3DSwapChain${api};
static unsigned fail_layout,fail_alloc,fail_reset_emit,fail_state_emit,fail_depth_ms,fail_window_mode,busy;
static unsigned allocations,frees,live,emits,replays,clears,initializations,frames,paces,prepares,windows,handles,mode_reports;
static Device*active;
static Device*device_from_iface(Device*d){return d;}
${api === 9 ? "static Chain*swap_chain_from_iface(Chain*c){return c;}" : ""}
static BOOL device_has_reset_blockers(Device*d){return d->blocked;}
${api === 8 ? "static BOOL supported_multisample_type(UINT ms){return ms==0||ms==4;}" : "static BOOL supported_multisample(D3DFORMAT f,UINT ms){return (ms==0||ms==4)&&!(f==75&&ms==4&&fail_depth_ms);}"}
static BOOL supported_depth_stencil_format(D3DFORMAT f){return f==75;}
static BOOL supported_backbuffer_format(D3DFORMAT f){return f==22;}
static void SetRect(RECT*r,int a,int b,int c,int d){*r=(RECT){a,b,c,d};}
static void GetClientRect(HWND h,RECT*r){assert(h);windows++;SetRect(r,0,0,64,64);}
static void ClientToScreen(HWND h,POINT*p){assert(h);windows++;p->x=30;p->y=40;}
static UINT allocate_handle(void){return ++handles+100;}
${api === 8 ? `static void*GetProcessHeap(void){return NULL;}
static void*HeapAlloc(void*h,UINT f,size_t n){(void)h;assert(f==1&&n);allocations++;if(fail_alloc)return NULL;live++;return calloc(1,n);}
static void HeapFree(void*h,UINT f,void*p){(void)h;(void)f;if(p){assert(live);live--;frees++;free(p);}}
static BOOL texture_level_layout(D3DFORMAT f,UINT w,UINT h,UINT*p,UINT*r,UINT*b){assert(f==22);*p=w*4;*r=h;*b=*p*h;return !fail_layout;}
static void detach_device_window(Device*d){d->tracked_window=NULL;}
static void attach_device_window(Device*d,HWND w){d->tracked_window=w;}` : "static void d9wg_window_cache_invalidate(int*p){*p=0;}"}
static BOOL reset_window_mode(Device*d,HWND w){(void)w;assert(d->reset_state.required);return !fail_window_mode;}
${api===8?'static BOOL emit_surface_update_and_flush(Device*d,HWND w,BOOL hidden,BOOL force){(void)w;(void)hidden;(void)force;assert(d->reset_state.required);mode_reports++;return 1;}':'static BOOL emit_window_state(Device*d,HWND w){(void)w;assert(d->reset_state.required);mode_reports++;return 1;}'}
static BOOL emit_command(UINT op,const void*p,UINT size){
 if(op==D9WG_OP_BEGIN_SCENE||op==D9WG_OP_END_SCENE){const D9WGDeviceOnly*c=p;assert(size==sizeof(*c)&&c->device_handle==active->handle);assert(active->in_scene==(op==D9WG_OP_BEGIN_SCENE));return 1;}
 const D9WGResetDevice*r=p;assert(active->reset_state.required&&op==D9WG_OP_RESET&&size==sizeof(*r));
 assert(r->old_device_handle==active->handle&&r->new_device_handle!=r->old_device_handle);emits++;return !fail_reset_emit;
}
static void device_clear_bindings(Device*d){assert(d==active&&d->reset_state.required);clears++;}
static void fill_display_mode(Mode*m,UINT w,UINT h,UINT f){*m=(Mode){w,h,f,60};}
static void device_init_states(Device*d){assert(d==active&&d->reset_state.required);initializations++;}
static BOOL emit_device_state_after_reset(Device*d){assert(d==active&&d->reset_state.required&&!d->in_scene);replays++;return !fail_state_emit;}
static BOOL emit_present_and_flush(Device*d,HWND w){(void)w;assert(d==active&&!d->reset_state.required);frames++;return 1;}
${api === 9 ? "static BOOL emit_present_swap_chain_and_flush(Chain*c,HWND w){return emit_present_and_flush(c->device,w);}" : ""}
static void d9wg_throttle_present(D9WGPresentClock*c,D9WGPresentPacing*p,UINT i,UINT hz){(void)c;(void)i;(void)hz;paces++;p->deadline++;}
${api === 9 ? "static BOOL d9wg_prepare_present(D9WGPresentClock*c,D9WGPresentPacing*p,UINT i,UINT hz){(void)c;(void)i;(void)hz;prepares++;p->deadline++;return !busy;}" : ""}
${functions.map(name => extract(source, name).replace(/&device->iface/g, "device")).join("\n")}
#define PRESENT(c,f) ${api === 8 ? "swapchain_present(c,NULL,NULL,NULL,NULL)" : "swap_chain_present(c,NULL,NULL,NULL,NULL,f)"}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Reset status API ${api} case %u line %u: %s\\n",kind,__LINE__,#v);return 1;}checks++;}while(0)
int main(void){unsigned checks=0,failures=0;D3DPRESENT_PARAMETERS base={.Windowed=1,.BackBufferWidth=64,.BackBufferHeight=64,.BackBufferCount=1,.BackBufferFormat=22,.AutoDepthStencilFormat=75,.EnableAutoDepthStencil=1,.hDeviceWindow=(void*)(uintptr_t)0x44};
for(unsigned kind=0;kind<15;kind++){
 if((${api}==8&&(kind==12||kind==13))||(${api}==9&&(kind==8||kind==9)))continue;
 Device d={.handle=77,.creation={.BehaviorFlags=0x22},.display_mode={80,72,22,60},.tracked_window=(void*)(uintptr_t)0x33,.window_cache=99};active=&d;
 Device untouched={0};CHECK(device_test_cooperative_level(&d)==D3D_OK);D3DPRESENT_PARAMETERS p=base;HRESULT expected=D3DERR_INVALIDCALL;
 fail_layout=fail_alloc=fail_reset_emit=fail_state_emit=fail_depth_ms=fail_window_mode=0;
 switch(kind){case 1:p.BackBufferCount=4;break;case 2:d.blocked=1;expected=${api === 8 ? "D3DERR_DEVICELOST" : "D3DERR_INVALIDCALL"};break;
 case 3:p.FullScreen_PresentationInterval=p.PresentationInterval=3;break;case 4:p.MultiSampleType=3;expected=${api === 8 ? "D3DERR_INVALIDCALL" : "D3DERR_NOTAVAILABLE"};break;
 case 5:p.AutoDepthStencilFormat=999;expected=D3DERR_NOTAVAILABLE;break;case 6:p.BackBufferFormat=999;expected=D3DERR_NOTAVAILABLE;break;
 case 7:p.BackBufferWidth=8193;break;case 8:fail_layout=1;expected=D3DERR_NOTAVAILABLE;break;case 9:fail_alloc=1;expected=E_OUTOFMEMORY;break;
 case 10:fail_reset_emit=1;expected=D3DERR_DRIVERINTERNALERROR;break;case 11:fail_state_emit=1;expected=D3DERR_DRIVERINTERNALERROR;break;
 case 12:p.MultiSampleQuality=1;expected=D3DERR_NOTAVAILABLE;break;case 13:fail_depth_ms=1;p.MultiSampleType=4;expected=D3DERR_NOTAVAILABLE;break;case 14:fail_window_mode=1;p.Windowed=0;break;}
 d.in_scene=1;Device before=d;D3DPRESENT_PARAMETERS input=p;unsigned old_emits=emits,old_clears=clears,old_reports=mode_reports;
 CHECK(device_reset(&d,kind?&p:NULL)==expected);failures++;CHECK(mode_reports==old_reports);CHECK(!memcmp(&p,&input,sizeof(p)));
 CHECK(d.reset_state.required);if(kind!=11&&kind!=14){before.reset_state.required=1;CHECK(!memcmp(&before,&d,sizeof(d))&&clears==old_clears);}
 CHECK(emits==old_emits+((kind>=10&&kind<=11)||kind==14));CHECK(clears==old_clears+(kind==11||kind==14));
 for(unsigned n=0;n<4;n++)CHECK(device_test_cooperative_level(&d)==D3DERR_DEVICENOTRESET);
 CHECK(device_test_cooperative_level(&untouched)==D3D_OK);
 unsigned old_frames=frames,old_paces=paces,old_prepares=prepares,old_windows=windows;Device lost=d;
 Chain implicit={.device=&d},additional={.device=&d,.handle=99};busy=1;
 CHECK(device_present(&d,NULL,NULL,NULL,NULL)==D3DERR_DEVICELOST);
 for(unsigned flags=0;flags<4;flags++){CHECK(PRESENT(&additional,flags)==D3DERR_DEVICELOST);CHECK(PRESENT(&implicit,flags)==D3DERR_DEVICELOST);}
 CHECK(frames==old_frames&&paces==old_paces&&prepares==old_prepares&&windows==old_windows&&!memcmp(&lost,&d,sizeof(d)));
 fail_layout=fail_alloc=fail_reset_emit=fail_state_emit=fail_depth_ms=fail_window_mode=0;d.blocked=0;busy=0;
 CHECK(device_test_cooperative_level(&d)==D3DERR_DEVICENOTRESET);
 p=base;CHECK(device_reset(&d,&p)==D3D_OK&&!d.reset_state.required);CHECK(device_test_cooperative_level(&d)==D3D_OK);
 CHECK(mode_reports==old_reports+1);CHECK(!d.in_scene&&device_end_scene(&d)==D3DERR_INVALIDCALL);CHECK(device_begin_scene(&d)==D3D_OK&&d.in_scene);CHECK(device_end_scene(&d)==D3D_OK&&!d.in_scene);CHECK(device_end_scene(&d)==D3DERR_INVALIDCALL);
 CHECK(d.viewport.Width==64&&d.viewport.Height==64&&d.display_mode.Width==64&&d.handle!=77);
 CHECK(device_present(&d,NULL,NULL,NULL,NULL)==D3D_OK&&PRESENT(&additional,0)==D3D_OK&&PRESENT(&implicit,1)==D3D_OK);
 CHECK(device_test_cooperative_level(&untouched)==D3D_OK);
 ${api === 8 ? "HeapFree(NULL,0,d.front_shadow);" : ""}
 CHECK(!live);
}
/* Retrying while the resource remains held must never recover the device. */
unsigned kind=99;Device d={.handle=77,.display_mode={64,64,22,60},.blocked=1};active=&d;
for(unsigned n=0;n<8;n++){D3DPRESENT_PARAMETERS p=base;CHECK(device_reset(&d,&p)==${api === 8 ? "D3DERR_DEVICELOST" : "D3DERR_INVALIDCALL"});CHECK(device_test_cooperative_level(&d)==D3DERR_DEVICENOTRESET);}
printf("PASS actual API ${api} Reset status: %u checks, %u failure paths; repeated queries/retries, device isolation, no lost Present commands/timing and complete recovery and native Scene reset boundaries\\n",checks,failures);return 0;}
`);
        const build = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable", "-Wno-unused-function", "-I", root, dir + "/status.c", root + "/runtime/d9wg_present_pacing.c", "-o", dir + "/status"], {encoding: "utf8"});
        assert.equal(build.status, 0, build.stderr);
        const run = spawnSync(dir + "/status", [], {encoding: "utf8"});
        assert.equal(run.status, 0, run.stdout + run.stderr + " signal=" + run.signal);
        process.stdout.write(run.stdout);
    }
} finally {
    fs.rmSync(dir, {recursive: true, force: true});
}
