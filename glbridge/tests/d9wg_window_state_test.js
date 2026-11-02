"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-window-state-"));
const extract=(source,name)=>{const match=source.match(new RegExp("static [^\\n]*\\b"+name+"\\([^;]*?\\)\\n\\{[\\s\\S]*?\\n}\\n"));assert(match,name);return match[0];};
try{for(const api of [8,9,7]){
 const folder=api===7?"ddraw":"d3d"+api,file=process.env.D9WG_WINDOW_MUTATION_API===String(api)?process.env.D9WG_WINDOW_FRONTEND_SOURCE:root+"/"+folder+"proxy/"+folder+"_proxy.c",source=fs.readFileSync(file,"utf8");
 const functions=(api===8?["capture_surface","emit_surface_update_and_flush"]:api===9?["emit_window_state"]:["emit_window_state_showing","emit_window_state_not_showing"]).map(name=>extract(source,name)).join("\n");
 const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "runtime/d9wg_window_state.h"
typedef int BOOL;typedef uint32_t UINT,DWORD;typedef uintptr_t HWND;typedef struct{int32_t left,top,right,bottom;}RECT;typedef struct{int32_t x,y;}POINT;
#define TRUE 1
#define FALSE 0
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory(p,q,n) memcpy(p,q,n)
#define DDSCL_FULLSCREEN 1u
#define DDWG_TRACE_CHECKPOINT(...) ((void)0)
#define V86_WINDOW_REGION_ALL 0
#define V86_WINDOW_REGION_NONE 1
#define V86_WINDOW_REGION_PART 2
typedef struct{uint32_t count;int32_t rects[32][4];}V86WindowRegion;
typedef struct{uint32_t device_handle,hwnd;int32_t x,y;uint32_t width,height;}D8ClientArea;
typedef struct{uint32_t handle;struct{BOOL Windowed;}present;D9WGWindowCache window_cache;HWND state_window;}D9Device;
typedef D9Device D8Device;
typedef struct{BOOL device_created;uint32_t device_handle;void *primary;HWND window;uint32_t cooperative_flags;BOOL guest_mode_changed;D9WGWindowCache window_cache;}DDrawObject;
static D9Device device;static DDrawObject object;static int g_transport_lock;
static HWND foreground=2;static int valid_window=1,visible=1,iconic,visibility;static int32_t x=20,y=30,cw=160,ch=100,ww=176,wh=138;
static V86WindowRegion region_value;static unsigned queued,submitted;static int fail_queue,fail_submit;static D9WGWindowStateWithRegion packet,pending;static uint32_t pending_bytes;
static void EnterCriticalSection(int *lock){++*lock;}static void LeaveCriticalSection(int *lock){--*lock;}
static HWND GetForegroundWindow(void){return foreground;}static BOOL IsWindow(HWND w){return w&&valid_window;}static BOOL IsWindowVisible(HWND w){(void)w;return visible;}static BOOL IsIconic(HWND w){(void)w;return iconic;}
static BOOL GetClientRect(HWND w,RECT *r){(void)w;*r=(RECT){0,0,cw,ch};return 1;}static BOOL GetWindowRect(HWND w,RECT *r){(void)w;*r=(RECT){x-8,y-30,x-8+ww,y-30+wh};return 1;}static BOOL ClientToScreen(HWND w,POINT *p){(void)w;p->x=x;p->y=y;return 1;}
static void SetRect(RECT *r,int32_t l,int32_t t,int32_t right,int32_t b){*r=(RECT){l,t,right,b};}
static int v86_window_visible_region(HWND w,V86WindowRegion *out){(void)w;*out=region_value;return visibility;}
static BOOL emit_command(uint32_t op,const void *bytes,uint32_t size){if(op!=D9WG_OP_WINDOW_STATE||size>sizeof(packet))return FALSE;if(fail_queue){fail_queue=0;return FALSE;}memcpy(&packet,bytes,size);queued++;return TRUE;}
static BOOL submit_batch_locked(BOOL present){if(present||g_transport_lock<1)return FALSE;submitted++;if(fail_submit){fail_submit=0;return FALSE;}return TRUE;}
static BOOL reserve_command_locked(uint32_t op,uint32_t bytes,uint32_t extra,void *header,uint8_t **payload,void *blob){(void)header;(void)blob;if(extra||op!=D9WG_OP_WINDOW_STATE||g_transport_lock<1)return FALSE;if(fail_queue){fail_queue=0;return FALSE;}pending_bytes=bytes;*payload=(uint8_t*)&pending;return TRUE;}
${functions}
static void emit(int force){${api===8?"(void)emit_surface_update_and_flush(&device,1,FALSE,force);if(submitted&&pending_bytes){packet=pending;queued++;pending_bytes=0;}":api===9?"(void)force;(void)emit_window_state(&device,1);":"(void)force;emit_window_state_showing(&object,TRUE);"}}
static D9WGWindowCache *cache(void){return ${api===7?"&object.window_cache":"&device.window_cache"};}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"window state line %u API=${api}: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){
 (void)emit_command;(void)submit_batch_locked;(void)reserve_command_locked;(void)SetRect;(void)GetWindowRect;(void)device;(void)object;
 device.handle=41;device.present.Windowed=1;object.device_created=1;object.device_handle=41;object.primary=(void*)1;object.window=1;
 emit(0);CHECK(queued==1&&packet.state.foreground_hwnd==2&&packet.state.window_x==20&&packet.state.client_width==160);CHECK(!g_transport_lock);
 emit(0);CHECK(queued==1);foreground=3;emit(0);CHECK(queued==2&&packet.state.foreground_hwnd==3);
 x=60;emit(0);CHECK(queued==3&&packet.state.window_x==60);cw=180;emit(0);CHECK(queued==4&&packet.state.client_width==180);ch=120;emit(0);CHECK(queued==5&&packet.state.client_height==120);
 ${api===8?"CHECK(packet.state.window_width==180);":"ww=200;emit(0);CHECK(queued==6&&packet.state.window_width==200);"}
 unsigned before=queued;foreground=4;fail_queue=1;D9WGWindowCache saved=*cache();emit(0);CHECK(queued==before&&!memcmp(cache(),&saved,sizeof(saved)));emit(0);CHECK(queued==before+1&&packet.state.foreground_hwnd==4);emit(0);CHECK(queued==before+1);
 visibility=2;region_value.count=2;region_value.rects[0][2]=80;region_value.rects[0][3]=120;region_value.rects[1][0]=100;region_value.rects[1][2]=180;region_value.rects[1][3]=120;emit(0);CHECK(packet.state.flags&D9WG_WINDOW_REGION);before=queued;region_value.rects[1][0]=110;emit(0);CHECK(queued==before+1&&packet.region.rects[1][0]==110);emit(0);CHECK(queued==before+1);
 ${api!==9?"before=queued;foreground=5;fail_submit=1;saved=*cache();emit(0);CHECK(!memcmp(cache(),&saved,sizeof(saved)));emit(0);CHECK(packet.state.foreground_hwnd==5&&queued==before+2);":""}
 ${api===7?"emit_window_state_not_showing(&object);CHECK(!cache()->valid&&(packet.state.flags&D9WG_WINDOW_NO_SURFACE));emit(0);CHECK(cache()->valid&&!(packet.state.flags&D9WG_WINDOW_NO_SURFACE));":""}
 struct{uint64_t lo;D9WGWindowCache c;uint64_t hi;}guard={.lo=0x123456789abcdef0ull,.hi=0xfedcba9876543210ull};D9WGWindowStateWithRegion a,b;memset(&a,0xa5,sizeof(a));memset(&b,0x5a,sizeof(b));memset(&a.state,0,sizeof(a.state));b.state=a.state;CHECK(!d9wg_window_cache_matches(&guard.c,&a));d9wg_window_cache_commit(&guard.c,&a);CHECK(d9wg_window_cache_matches(&guard.c,&b));
 for(unsigned i=0;i<sizeof(a.state)/4;++i){uint32_t *word=(uint32_t*)&b.state;word[i]^=0x10203040u;CHECK(!d9wg_window_cache_matches(&guard.c,&b));word[i]^=0x10203040u;}
 int32_t rects[32][4];for(unsigned i=0;i<32;++i)for(unsigned j=0;j<4;++j)rects[i][j]=(int32_t)(i*11+j*3)-40;
 for(unsigned count=0;count<=32;++count){a.state.flags=D9WG_WINDOW_REGION;d9wg_window_report_region(&a,count,rects);d9wg_window_cache_commit(&guard.c,&a);CHECK(d9wg_window_cache_matches(&guard.c,&a));b=a;if(count){b.region.rects[count-1][3]++;CHECK(!d9wg_window_cache_matches(&guard.c,&b));}if(count<32){b=a;b.region.rects[count][0]++;CHECK(d9wg_window_cache_matches(&guard.c,&b));}}
 CHECK(guard.lo==0x123456789abcdef0ull&&guard.hi==0xfedcba9876543210ull);d9wg_window_cache_invalidate(&guard.c);CHECK(!d9wg_window_cache_matches(&guard.c,&a));
 puts("PASS actual API ${api} window reports: complete geometry/foreground/region identity, queue/flush failure retry, bounded active-byte cache and zero unused-tail comparisons");return 0;}
`;
 const input=path.join(directory,folder+".c"),binary=path.join(directory,folder);fs.writeFileSync(input,code);
 const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-Wno-unused-function","-I",root,"-I",root+"/runtime",input,process.env.D9WG_WINDOW_CACHE_SOURCE||root+"/runtime/d9wg_window_state.c","-o",binary],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);
}}finally{fs.rmSync(directory,{recursive:true,force:true});}
