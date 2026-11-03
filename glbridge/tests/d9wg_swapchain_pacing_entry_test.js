'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-chain-pacing-'));
function extract(source,name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let end=source.indexOf('{',m.index)+1,depth=1;for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}return source.slice(m.index,end);}
try{for(const api of[8,9]){const source=fs.readFileSync(process.env['D'+api+'WG_CHAIN_FRONTEND']||root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8');fs.writeFileSync(dir+'/entry.c',`#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "runtime/d9wg_present_pacing.h"
#include "runtime/d9wg_reset.h"
typedef uint32_t DWORD;typedef int32_t LONG,HRESULT;typedef int BOOL;typedef void*HWND;typedef struct{int unused;}RECT,RGNDATA;typedef struct{int64_t QuadPart;}LARGE_INTEGER;
#define WINAPI
#define D3D_OK 0
#define D3DERR_DRIVERINTERNALERROR (-1)
#define D3DERR_WASSTILLDRAWING (-2)
#define D3DPRESENT_DONOTWAIT 1
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
#define TRACE_MARK_ENTER(...)
#define TRACE_MARK_EXIT(...)
#define SUCCEEDED(hr) ((hr)>=0)
typedef struct{DWORD FullScreen_PresentationInterval,PresentationInterval,SwapEffect;BOOL Windowed;HWND hDeviceWindow;}Parameters;
typedef struct{D9WGResetState reset_state;D9WGPresentClock present_clock;D9WGPresentPacing present_pacing;Parameters present;struct{DWORD RefreshRate;}display_mode;int iface;}Device;
typedef struct{D9WGPresentClock present_clock;D9WGPresentPacing present_pacing;Device*device;Parameters present;DWORD handle;}Chain;
typedef Device D${api}Device,IDirect3DDevice${api};typedef Chain D${api}SwapChain,IDirect3DSwapChain${api};
static unsigned mode,locked,emits,fail_emit,env_reads,qpc_reads,sleeps,device_emits,chain_emits;static uint64_t time_us;static HWND last_window;
static LONG InterlockedCompareExchange(volatile LONG*p,LONG value,LONG compare){LONG old=*p;if(old==compare)*p=value;return old;}
static DWORD GetEnvironmentVariableA(const char*name,char*out,DWORD size){assert(!strcmp(name,"D9WG_PRESENT_NO_THROTTLE")&&size>=2&&!locked);env_reads++;out[0]=mode==1?'1':'0';out[1]=0;return 1;}
static BOOL QueryPerformanceFrequency(LARGE_INTEGER*out){assert(!locked);if(mode==2)return 0;out->QuadPart=1000000;return 1;}
static BOOL QueryPerformanceCounter(LARGE_INTEGER*out){assert(!locked);qpc_reads++;if(mode==3&&qpc_reads>=4)return 0;out->QuadPart=(int64_t)time_us;return 1;}
static DWORD GetTickCount(void){assert(!locked);return (DWORD)(time_us/1000);}
static void Sleep(DWORD ms){assert(!locked&&ms&&ms<=201);sleeps++;time_us+=(uint64_t)ms*1000;}
static Device*device_from_iface(Device*d){return d;}
${api===9?'static Chain*swap_chain_from_iface(Chain*c){return c;}':''}
static BOOL emit_present_and_flush(Device*d,HWND window){(void)d;assert(!locked);locked=1;emits++;device_emits++;last_window=window;locked=0;return !fail_emit;}
${api===9?'static BOOL emit_present_swap_chain_and_flush(Chain*c,HWND window){assert(c->handle&&!locked);locked=1;emits++;chain_emits++;last_window=window;locked=0;return !fail_emit;}':''}
#include "${process.env.D9WG_CHAIN_BRIDGE||root+'/runtime/d9wg_present_pacing_bridge.h'}"
${extract(source,'device_present').replace(/&device->iface/g,'device').replace(/&swap_chain->device->iface/g,'swap_chain->device')}
${extract(source,api===8?'swapchain_present':'swap_chain_present').replace(/&device->iface/g,'device').replace(/&swap_chain->device->iface/g,'swap_chain->device')}
#define PRESENT(c,flags,window) ${api===8?'swapchain_present(c,NULL,NULL,window,NULL)':'swap_chain_present(c,NULL,NULL,window,NULL,flags)'}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Chain pacing API ${api} mode %u line %u: %s\\n",mode,__LINE__,#v);return 1;}}while(0)
int main(int argc,char**argv){CHECK(argc==2);mode=(unsigned)(argv[1][0]-'0');Device device={.display_mode={60}};device.present.PresentationInterval=1;Chain a={.device=&device,.present={.Windowed=1,.SwapEffect=4,.PresentationInterval=1,.hDeviceWindow=(void*)(uintptr_t)0x55},.handle=10},b=a;b.handle=11;b.present.hDeviceWindow=(void*)(uintptr_t)0x66;unsigned cases=0;
for(unsigned n=0;n<61;n++){CHECK(!PRESENT(&a,0,NULL));CHECK(!PRESENT(&b,0,NULL));}CHECK(emits==122&&env_reads==1&&!device.present_pacing.ready&&!device.present_clock.hz&&!locked);CHECK(mode==1?!time_us:time_us>=999000&&time_us<=1000000);cases++;
${api===8?'CHECK(last_window==b.present.hDeviceWindow&&!chain_emits);':'CHECK(!last_window&&chain_emits==122&&!device_emits);'}cases++;
D9WGPresentPacing before=a.present_pacing;unsigned before_sleeps=sleeps,before_emits=emits;fail_emit=1;CHECK(PRESENT(&a,0,(void*)(uintptr_t)0x77)==-1&&!memcmp(&before,&a.present_pacing,sizeof(before))&&sleeps==before_sleeps&&emits==before_emits+1&&last_window==(void*)(uintptr_t)0x77);fail_emit=0;cases++;
a.present.PresentationInterval=0x80000000u;a.present.SwapEffect=1;CHECK(!PRESENT(&a,0,NULL)&&!a.present_pacing.ready&&sleeps==before_sleeps);cases++;
a.present.PresentationInterval=1;a.present.SwapEffect=4;CHECK(!PRESENT(&a,0,NULL)&&sleeps==before_sleeps);cases++;
before=a.present_pacing;CHECK(!device_present(&device,NULL,NULL,NULL,NULL)&&sleeps==before_sleeps&&!memcmp(&before,&a.present_pacing,sizeof(before)));cases++;
${api===9?`
a.present_pacing=(D9WGPresentPacing){0};b.present_pacing=(D9WGPresentPacing){0};before_sleeps=sleeps;CHECK(!PRESENT(&a,1,NULL)&&sleeps==before_sleeps);before=a.present_pacing;before_emits=emits;for(unsigned flags=1;flags<=3;flags+=2){CHECK(PRESENT(&a,flags,NULL)==(mode==1?0:-2)&&sleeps==before_sleeps);if(mode!=1)CHECK(emits==before_emits&&!memcmp(&before,&a.present_pacing,sizeof(before)));cases++;}
CHECK(!PRESENT(&b,3,NULL)&&sleeps==before_sleeps);cases++;
time_us+=20000;CHECK(!PRESENT(&a,3,(void*)(uintptr_t)0x77)&&sleeps==before_sleeps&&last_window==(void*)(uintptr_t)0x77);cases++;
time_us+=20000;before=a.present_pacing;before_sleeps=sleeps;fail_emit=1;CHECK(PRESENT(&a,1,NULL)==-1&&!memcmp(&before,&a.present_pacing,sizeof(before))&&sleeps==before_sleeps);fail_emit=0;cases++;
Chain implicit={.device=&device};device.present_pacing=(D9WGPresentPacing){0};CHECK(!device_present(&device,NULL,NULL,NULL,NULL));before=device.present_pacing;before_sleeps=sleeps;before_emits=emits;CHECK(PRESENT(&implicit,1,NULL)==(mode==1?0:-2)&&sleeps==before_sleeps&&!implicit.present_pacing.ready);if(mode!=1)CHECK(emits==before_emits&&!memcmp(&before,&device.present_pacing,sizeof(before)));cases++;
time_us+=20000;CHECK(!PRESENT(&implicit,3,NULL)&&sleeps==before_sleeps&&!implicit.present_pacing.ready);before_sleeps=sleeps;CHECK(!device_present(&device,NULL,NULL,NULL,NULL)&&(mode==1?sleeps==before_sleeps:sleeps>before_sleeps));cases++;
time_us+=20000;before=device.present_pacing;before_sleeps=sleeps;fail_emit=1;CHECK(PRESENT(&implicit,1,NULL)==-1&&!memcmp(&before,&device.present_pacing,sizeof(before))&&sleeps==before_sleeps);fail_emit=0;cases++;
CHECK(!PRESENT(&implicit,0,NULL)&&!implicit.present_pacing.ready);cases++;
`:''}
printf("%u\\n",cases);return 0;}
`);const build=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-I',root,'-I',root+'/runtime',dir+'/entry.c',process.env.D9WG_CHAIN_RUNTIME||root+'/runtime/d9wg_present_pacing.c','-o',dir+'/entry'],{encoding:'utf8'});assert.equal(build.status,0,build.stderr);let cases=0;for(let mode=0;mode<4;mode++){const r=spawnSync(dir+'/entry',[String(mode)],{encoding:'utf8',timeout:10000});assert.equal(r.status,0,r.stderr+' signal='+r.signal);cases+=Number(r.stdout.trim());}console.log('PASS actual API '+api+' swap-chain Present: '+cases+' independent chain/device clocks, immediate/disable/fallback, override routing and failed-emission atomicity'+(api===9?', DONOTWAIT no-wait/no-emit refusal and implicit-device continuity':''));}}finally{fs.rmSync(dir,{recursive:true,force:true});}
