"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-window-present-"));
function extract(source,name){const m=source.match(new RegExp("static [^\\n]*\\b"+name+"\\([^;]*?\\)\\n\\{[\\s\\S]*?\\n}\\n"));assert(m,name);return m[0];}
try{for(const api of (process.env.D9WG_WINDOW_PRESENT_ONLY_API?[Number(process.env.D9WG_WINDOW_PRESENT_ONLY_API)]:[8,9,7])){
 const folder=api===7?"ddraw":"d3d"+api,source=fs.readFileSync(process.env.D9WG_WINDOW_PRESENT_API===String(api)?process.env.D9WG_WINDOW_PRESENT_SOURCE:root+"/"+folder+"proxy/"+folder+"_proxy.c","utf8");
 const names=api===8?["capture_surface","emit_surface_update_and_flush","emit_present_and_flush"]:api===9?["emit_window_state_from_probe","maintain_fullscreen_foreground","emit_present_and_flush"]:["emit_window_state_showing_from_probe","emit_present_and_flush"];
 const code=`#include "window_mock.h"
#include "runtime/d9wg_window_probe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DDSCL_FULLSCREEN 1u
#define DDWG_TRACE_CHECKPOINT(...) ((void)0)
#define CopyMemory(p,q,n) memcpy(p,q,n)
typedef struct{uint32_t device_handle,hwnd;int32_t x,y;uint32_t width,height;}D8ClientArea;
typedef struct{uint32_t handle;struct{BOOL Windowed;HWND hDeviceWindow;}present;struct{HWND hFocusWindow;}creation;struct{uint32_t Width,Height;}display_mode;D9WGWindowCache window_cache;HWND state_window,tracked_window;void *app_cursor;BOOL cursor_visible;DWORD foreground_claims,last_foreground_claim;}D9Device;
typedef D9Device D8Device;
typedef struct DDSurface{struct DDSurface *next;BOOL overlay_visible,external_memory;struct{BOOL gpu_dirty;}*memory;}DDSurface;
typedef struct{BOOL device_created;uint32_t device_handle;DDSurface *primary,*surfaces;HWND window;uint32_t cooperative_flags,mode_width,mode_height;BOOL guest_mode_changed;D9WGWindowCache window_cache;}DDrawObject;
static CRITICAL_SECTION g_transport_lock;static D9WGPresent present_packet;static D9WGWindowStateWithRegion state_packet;static uint8_t payload[sizeof(state_packet)];static unsigned state_count,present_count,submitted,claims,restores,retries;static uint32_t pending_op,pending_bytes;static D9Device *active_device;static DDrawObject *active_object;static int inject_event,deferred_event;static int32_t last_placement_x,last_placement_y;static int fail_reserve,fail_submit,fail_emit;
static void deliver_event(void);
void EnterCriticalSection(CRITICAL_SECTION *l){l->held++;}void LeaveCriticalSection(CRITICAL_SECTION *l){l->held--;}
DWORD GetTickCount(void){return 1000;}
static void raise_window_to_foreground(HWND w){if(g_transport_lock.held){fputs("window present line: activation held transport lock",stderr);abort();}claims++;fixture.foreground=w;fixture.origin=(POINT){30,40};fixture.screen_rects[0]=(RECT){30,40,190,140};}
static void ShowWindow(HWND w,int action){(void)w;(void)action;restores++;fixture.iconic=0;}
static BOOL GetCursorPos(POINT *p){(void)p;return FALSE;}static BOOL ScreenToClient(HWND w,POINT *p){(void)w;(void)p;return FALSE;}
static void emit_cursor_position(D9Device*d,int x,int y,int f){(void)d;(void)x;(void)y;(void)f;}static void update_system_cursor(D9Device*d,HWND w){(void)d;(void)w;}
void d9wg_window_watch_retry(void){retries++;}
static BOOL ensure_device(DDrawObject *o){return o->device_created;}static void emit_surface_upload(DDSurface*s,void*r){(void)s;(void)r;}
static BOOL emit_command(uint32_t op,const void *p,uint32_t bytes){if(op==D9WG_OP_PRESENT&&fail_emit){fail_emit=0;return FALSE;}if(op==D9WG_OP_WINDOW_STATE){memcpy(&state_packet,p,bytes);state_count++;last_placement_x=state_packet.state.window_x;last_placement_y=state_packet.state.window_y;}else if(op==D9WG_OP_PRESENT){memcpy(&present_packet,p,bytes);present_count++;last_placement_x=present_packet.x;last_placement_y=present_packet.y;}else return FALSE;return TRUE;}
static BOOL reserve_command_locked(uint32_t op,uint32_t bytes,uint32_t extra,void *h,uint8_t **out,void *b){(void)h;(void)b;if(fail_reserve){fail_reserve=0;return FALSE;}if(!g_transport_lock.held||extra||bytes>sizeof(payload))return FALSE;pending_op=op;pending_bytes=bytes;*out=payload;return TRUE;}
static BOOL submit_batch_locked(BOOL frame){if(!g_transport_lock.held)return FALSE;submitted++;if(frame&&fail_submit){fail_submit=0;return FALSE;}if(pending_op){if(!emit_command(pending_op,payload,pending_bytes))return FALSE;pending_op=0;}return TRUE;}
static void test_probe_capture(uintptr_t w,uint32_t options,D9WGWindowProbe *p){d9wg_window_probe_capture(w,options,p);if(inject_event){inject_event=0;fixture.origin=(POINT){90,100};fixture.outer=(RECT){82,70,258,208};fixture.screen_rects[0]=(RECT){90,100,250,200};if(g_transport_lock.held)deferred_event=1;else deliver_event();}}
${names.map(n=>extract(source,n).replaceAll("d9wg_window_probe_capture(","test_probe_capture(")).join("\n")}
static void deliver_event(void){EnterCriticalSection(&g_transport_lock);${api===8?"emit_surface_update_and_flush(active_device,1,FALSE,FALSE);":`D9WGWindowProbe fresh;d9wg_window_probe_capture(1,7,&fresh);${api===9?"emit_window_state_from_probe(active_device,1,&fresh);submit_batch_locked(FALSE);":"emit_window_state_showing_from_probe(active_object,TRUE,&fresh);"}`}LeaveCriticalSection(&g_transport_lock);}

#define CHECK(v) do{if(!(v)){fprintf(stderr,"window present line %u API=${api}: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){
 window_mock_reset();D9Device d={.handle=41,.present={.Windowed=1,.hDeviceWindow=1},.creation={.hFocusWindow=1},.display_mode={640,480},.tracked_window=1};DDrawObject o={.device_created=1,.device_handle=41,.primary=(DDSurface*)1,.window=1,.mode_width=640,.mode_height=480};(void)o;(void)d;active_device=&d;active_object=&o;
 CHECK(emit_present_and_flush(${api===7?"&o":"&d"}${api===7?"":",0"}));CHECK(retries==1);CHECK(present_count==1&&submitted==1&&!g_transport_lock.held);CHECK(present_packet.device_handle==41&&present_packet.hwnd==1&&present_packet.x==20&&present_packet.y==30&&present_packet.width==160&&present_packet.height==100);
 CHECK(calls.client==1&&calls.origin==1&&calls.valid==1&&calls.iconic==1);CHECK(calls.foreground==${api===8?0:1}&&calls.outer==${api===8?0:1}&&calls.dc==${api===8?0:1});
 ${api===8?"CHECK(state_count==0);":"CHECK(state_count==1&&state_packet.state.window_x==present_packet.x&&state_packet.state.client_width==present_packet.width&&state_packet.state.window_height==138);"}
 CHECK(emit_present_and_flush(${api===7?"&o":"&d"}${api===7?"":",0"}));CHECK(retries==2);CHECK(calls.client==2&&calls.origin==2&&present_count==2&&state_count==${api===8?0:1});
 ${api===9?`window_mock_reset();d.present.Windowed=0;fixture.foreground=2;CHECK(emit_present_and_flush(&d,0));CHECK(claims==1&&calls.client==2&&calls.origin==2&&calls.outer==2&&calls.foreground==2);CHECK(present_packet.x==30&&present_packet.y==40&&state_packet.state.window_x==30&&state_packet.state.window_y==40&&state_packet.state.foreground_hwnd==1);
 window_mock_reset();d.foreground_claims=0;fixture.foreground=2;fixture.region_count=0;CHECK(emit_present_and_flush(&d,0));CHECK(claims==1&&calls.client==1&&calls.foreground==1&&(state_packet.state.flags&D9WG_WINDOW_OCCLUDED));
 window_mock_reset();d.foreground_claims=1;d.last_foreground_claim=800;fixture.foreground=2;CHECK(emit_present_and_flush(&d,0));CHECK(claims==1&&calls.client==1);
 window_mock_reset();d.foreground_claims=0;fixture.foreground=2;fixture.iconic=1;CHECK(emit_present_and_flush(&d,0));CHECK(claims==2&&restores==1&&calls.client==2&&!(state_packet.state.flags&D9WG_WINDOW_ICONIC));`:""}
 ${api===7?`window_mock_reset();o.cooperative_flags=DDSCL_FULLSCREEN;o.guest_mode_changed=1;CHECK(emit_present_and_flush(&o));CHECK(calls.client==1&&calls.origin==1&&present_packet.x==0&&present_packet.y==0&&present_packet.width==640&&present_packet.height==480&&state_packet.state.window_x==0&&state_packet.state.window_y==0);`:""}
 window_mock_reset();d.present.Windowed=1;o.cooperative_flags=0;o.guest_mode_changed=0;inject_event=1;CHECK(emit_present_and_flush(${api===7?"&o":"&d"}${api===7?"":",0"}));if(deferred_event){deferred_event=0;deliver_event();}CHECK(last_placement_x==90&&last_placement_y==100&&!g_transport_lock.held);
 ${api===7?"fail_emit=1;":"fail_reserve=1;"}CHECK(!emit_present_and_flush(${api===7?"&o":"&d"}${api===7?"":",0"})&&!g_transport_lock.held);fail_submit=1;CHECK(!emit_present_and_flush(${api===7?"&o":"&d"}${api===7?"":",0"})&&!g_transport_lock.held);
 puts("PASS actual API ${api} Present: single Win32 capture, shared geometry/state observations explicit fullscreen/reclaim policy, event/Present ordering and failure unlocks");return 0;}
`;
 const input=dir+"/"+folder+".c",bin=dir+"/"+folder;fs.writeFileSync(input,code);
 const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-Wno-unused-function","-I",root+"/tests/native-window","-I",root,input,root+"/tests/native-window/window_mock.c",root+"/runtime/d9wg_window_probe.c",root+"/runtime/d9wg_window_geometry.c",root+"/runtime/d9wg_window_state.c","-o",bin],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(bin,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);
}}finally{fs.rmSync(dir,{recursive:true,force:true});}
