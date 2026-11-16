"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-private-entry-"));
function extract(source,name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
try{for(const api of[8,9]){const source=readComparisonSource(process.env['D'+api+'WG_PACING_FRONTEND']||root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8'),bridge=process.env.D9WG_PACING_BRIDGE||root+'/runtime/d9wg_present_pacing_bridge.h';fs.writeFileSync(dir+'/entry.c',`#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "runtime/d9wg_present_pacing.h"
#include "runtime/d9wg_reset.h"
typedef uint32_t DWORD;typedef int32_t LONG,HRESULT;typedef int BOOL;typedef void*HWND;typedef struct{int unused;}RECT,RGNDATA;typedef struct{int64_t QuadPart;}LARGE_INTEGER;
#define WINAPI
#define FAILED(hr) ((hr)<0)
#define D3D_OK 0
#define D3DERR_DRIVERINTERNALERROR (-1)
#define D3DERR_INVALIDCALL (-2)
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
typedef struct{struct{HWND window;}implicit_swap_chain;D9WGResetState reset_state;D9WGPresentClock present_clock;D9WGPresentPacing present_pacing;struct{DWORD FullScreen_PresentationInterval,PresentationInterval,SwapEffect;BOOL Windowed;HWND hDeviceWindow;}present;struct{HWND hFocusWindow;}creation;struct{DWORD RefreshRate;}display_mode;}Device;
typedef Device D${api}Device,IDirect3DDevice${api};
static unsigned mode,locked,emits,fail_emit,env_reads,qpf_reads,qpc_reads,tick_reads,sleeps;static uint64_t time_us;
static LONG InterlockedCompareExchange(volatile LONG*p,LONG value,LONG compare){LONG old=*p;if(old==compare)*p=value;return old;}
static DWORD GetEnvironmentVariableA(const char*name,char*out,DWORD size){assert(!strcmp(name,"D9WG_PRESENT_NO_THROTTLE")&&size>=2&&!locked);env_reads++;out[0]=mode==1?'1':'0';out[1]=0;return 1;}
static BOOL QueryPerformanceFrequency(LARGE_INTEGER*out){assert(!locked);qpf_reads++;if(mode==2)return 0;out->QuadPart=mode==4?500:mode==5?LLONG_MAX:1000000;return 1;}
static BOOL QueryPerformanceCounter(LARGE_INTEGER*out){assert(!locked);qpc_reads++;if(mode==3&&qpc_reads>=4)return 0;out->QuadPart=mode==5?(INT64_C(1)<<62):mode==7?0:(int64_t)time_us;return 1;}
static DWORD GetTickCount(void){assert(!locked);tick_reads++;return (DWORD)(time_us/1000);}
static void Sleep(DWORD ms){assert(!locked&&ms&&ms<=201);sleeps++;if(mode==6)ms=ms/10*10;time_us+=(uint64_t)ms*1000;}
static Device*device_from_iface(Device*d){return d;}
static BOOL emit_present_and_flush(Device*d,HWND window){(void)d;assert(window==(void*)(uintptr_t)0x42&&!locked);locked=1;emits++;locked=0;return !fail_emit;}
#include "${bridge}"
${api===9?'static HRESULT swap_chain_prepare_buffers(void*p){(void)p;return 0;}static HRESULT swap_chain_advance_buffers(void*p){(void)p;return 0;}':''}
${api===9?'static unsigned device_compatibility_api(Device*d){(void)d;return 9;}static BOOL IsWindow(HWND w){return w!=NULL;}':''}
${extract(source,'device_present')}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Present API ${api} mode %u line %u: %s\\n",mode,__LINE__,#v);return 1;}}while(0)
int main(int argc,char**argv){CHECK(argc==2);mode=(unsigned)(argv[1][0]-'0');Device device={.display_mode={60}};unsigned cases=0;
if(mode==5){CHECK(!device_present(&device,NULL,NULL,(void*)(uintptr_t)0x42,NULL));CHECK(device.present_clock.hz==1048575&&device.present_clock.shift==43&&device.present_pacing.deadline==524288+1048575/60&&!sleeps&&qpf_reads==1&&qpc_reads==1);cases++;}
else{for(unsigned i=0;i<61;i++)CHECK(!device_present(&device,NULL,NULL,(void*)(uintptr_t)0x42,NULL));CHECK(emits==61&&env_reads==1&&!locked);cases++;
 if(mode==1)CHECK(!time_us&&!qpf_reads&&!qpc_reads&&!tick_reads&&!sleeps&&!device.present_pacing.ready);else if(mode==7)CHECK(device.present_clock.hz==1000&&device.present_clock.shift==255&&qpc_reads==10&&qpf_reads==1&&tick_reads>0&&time_us>=1000000);else if(mode==3)CHECK(device.present_clock.hz==1000&&device.present_clock.shift==255&&qpc_reads==4&&qpf_reads==1&&tick_reads>0&&time_us>=950000&&time_us<=1000000);else{CHECK(time_us>=999000&&time_us<=1000000&&device.present_pacing.ready&&qpf_reads==1);if(mode==2||mode==4)CHECK(!qpc_reads&&tick_reads&&device.present_clock.hz==1000);else CHECK(qpc_reads&&!tick_reads&&device.present_clock.hz==1000000);}cases++;
 unsigned before_qpc=qpc_reads,before_tick=tick_reads,before_sleeps=sleeps;device.present.FullScreen_PresentationInterval=device.present.PresentationInterval=0x80000000u;CHECK(!device_present(&device,NULL,NULL,(void*)(uintptr_t)0x42,NULL)&&!device.present_pacing.ready&&qpc_reads==before_qpc&&tick_reads==before_tick&&sleeps==before_sleeps);cases++;
 device.present.FullScreen_PresentationInterval=device.present.PresentationInterval=1;CHECK(!device_present(&device,NULL,NULL,(void*)(uintptr_t)0x42,NULL)&&sleeps==before_sleeps);cases++;
 D9WGPresentPacing before=device.present_pacing;before_qpc=qpc_reads;before_tick=tick_reads;fail_emit=1;CHECK(device_present(&device,NULL,NULL,(void*)(uintptr_t)0x42,NULL)==-1&&!memcmp(&before,&device.present_pacing,sizeof(before))&&qpc_reads==before_qpc&&tick_reads==before_tick&&sleeps==before_sleeps&&emits==64);cases++;

${api===8?`fail_emit=0;device.present.Windowed=1;device.present.SwapEffect=1;device.present.FullScreen_PresentationInterval=0;before_qpc=qpc_reads;before_tick=tick_reads;before_sleeps=sleeps;CHECK(!device_present(&device,NULL,NULL,(void*)(uintptr_t)0x42,NULL)&&!device.present_pacing.ready&&qpc_reads==before_qpc&&tick_reads==before_tick&&sleeps==before_sleeps);cases++;
device.present.SwapEffect=4;CHECK(!device_present(&device,NULL,NULL,(void*)(uintptr_t)0x42,NULL));uint64_t before_time=time_us;CHECK(!device_present(&device,NULL,NULL,(void*)(uintptr_t)0x42,NULL)&&(mode==1?time_us==before_time:time_us>before_time));cases++;
device.present.Windowed=0;device.present.FullScreen_PresentationInterval=0x80000000u;before_time=time_us;CHECK(!device_present(&device,NULL,NULL,(void*)(uintptr_t)0x42,NULL)&&(mode==1?time_us==before_time:time_us>before_time));cases++;
device.present.SwapEffect=3;before_sleeps=sleeps;CHECK(!device_present(&device,NULL,NULL,(void*)(uintptr_t)0x42,NULL)&&!device.present_pacing.ready&&sleeps==before_sleeps);cases++;`:''}
}printf("%u\\n",cases);return 0;}
`);const build=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-I',root,'-I',root+'/runtime',dir+'/entry.c',process.env.D9WG_PACING_RUNTIME_SOURCE||root+'/runtime/d9wg_present_pacing.c','-o',dir+'/entry'],{encoding:'utf8'});assert.equal(build.status,0,build.stderr);let cases=0;for(let mode=0;mode<8;mode++){const run=spawnSync(dir+'/entry',[String(mode)],{encoding:'utf8',timeout:10000});assert.equal(run.status,0,run.stderr+' signal='+run.signal);cases+=Number(run.stdout.trim());}console.log('PASS actual API '+api+' Present: '+cases+' QPC/coarse/failed/low-frequency/large-shift and disabled cases; exact virtual cadence, immediate bypass, failed submission atomicity, environment cache and no sleep under transport lock');}}finally{fs.rmSync(dir,{recursive:true,force:true});}
