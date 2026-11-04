"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-private-entry-"));
function extract(source,name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
try{for(const api of[8,9]){const source=fs.readFileSync(process.env['D'+api+'WG_PARAMETERS_FRONTEND']||root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8'),chain=api===8?'device_create_swapchain':'device_create_additional_swap_chain';function prefix(name,marker){const full=extract(source,name),at=full.indexOf(marker);assert(at>0,name);return full.slice(0,at)+(api===9&&name==='device_reset'?'    (void)backbuffer_format;\n':'')+'    return 0x12345678;\n}';}fs.writeFileSync(dir+'/entry.c',`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
#include "runtime/d9wg_present_pacing.h"
#include "runtime/d9wg_reset.h"
#include "runtime/d9wg_present_multisample.h"
#include "runtime/d9wg_swap_effect.h"
typedef uint32_t UINT,DWORD;typedef uint8_t BYTE;typedef int BOOL,HRESULT,D3DDEVTYPE,D3DFORMAT;typedef void*HWND;typedef struct{int left,top,right,bottom;}RECT;typedef struct{int x,y;}POINT;
#define WINAPI
#define NULL ((void*)0)
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DFMT_UNKNOWN 0
#define D3DDEVTYPE_HAL 1
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_NOTAVAILABLE (-2)
#define D3DERR_DEVICELOST ((int32_t)0x88760868u)
#define D8WG_TRACE_ERROR(v) (trace_errors++,(v))
#define TRACE_REFUSE(v) (trace_errors++,(v))
#define TRACE(...)
#define TRACE_MARK_ENTER(...)
#define TRACE_MARK_EXIT(...)
#define HOSTLOG_REFUSED(...)
typedef struct{int magic;UINT compatibility_api;}Factory;typedef struct{int magic,blocked;D9WGResetState reset_state;struct{BOOL Windowed;}present;}Device;typedef struct{int magic;}Chain;
typedef Factory D${api}Direct3D,IDirect3D${api};typedef Device D${api}Device,IDirect3DDevice${api};typedef Chain D${api}SwapChain,IDirect3DSwapChain${api},D${api}Surface,IDirect3DSurface${api};
typedef struct{BOOL Windowed;DWORD PresentationInterval,FullScreen_PresentationInterval,BackBufferCount,MultiSampleType,BackBufferWidth,BackBufferHeight,BackBufferFormat,SwapEffect,MultiSampleQuality,Flags;}D3DPRESENT_PARAMETERS;
static unsigned trace_errors;static Factory*d3d_from_iface(Factory*f){return f;}static Device*device_from_iface(Device*d){return d;}
${api===9?'static UINT d9wg_display_mode_backbuffer_format(UINT f){return f?f:22;}':''}
${api===8?'static BOOL supported_multisample_type(UINT type){return !type||type==4;}':''}
${prefix('d3d_create_device',api===8?'    if (!supported_multisample_type(parameters->MultiSampleType))':'    backbuffer_format = ')}
${prefix('device_reset',api===8?'    if (parameters->EnableAutoDepthStencil':'    if (!supported_multisample(')}
${prefix(chain,api===8?'    if (parameters->BackBufferFormat != D3DFMT_UNKNOWN':'    if (!params->Windowed || !device->present.Windowed)')}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Parameter preflight API ${api} line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){Factory factory={123,9};Device device={.magic=456,.present={1}},before_device=device;uint32_t requests[]={0,1,2,4,8,0x80000000u,3,5,6,9,16,0x40000000u,0xffffffffu,0x80000001u};unsigned cases=0;
for(unsigned method=0;method<3;method++)for(unsigned windowed=0;windowed<2;windowed++){if(method==2&&!windowed)continue;for(unsigned i=0;i<14;i++){uint32_t interval=requests[i];D3DPRESENT_PARAMETERS p={.SwapEffect=1,.Windowed=(int)windowed,.PresentationInterval=interval,.FullScreen_PresentationInterval=interval,.BackBufferCount=1,.BackBufferWidth=64,.BackBufferHeight=64,.BackBufferFormat=22},before=p;int valid=interval==0||interval==1||interval==0x80000000u;if(${api}==8&&windowed)valid=interval==0;HRESULT expected=valid?0x12345678:-1;Device*out=(void*)(uintptr_t)1;Chain*chain_out=(void*)(uintptr_t)1;device.reset_state.required=0;before_device=device;trace_errors=0;HRESULT r=method==0?d3d_create_device(&factory,0,1,NULL,0x22,&p,&out):method==1?device_reset(&device,&p):${chain}(&device,&p,&chain_out);if(method==1)before_device.reset_state.required=1;CHECK(r==expected&&!memcmp(&p,&before,sizeof(p))&&!memcmp(&device,&before_device,sizeof(device))&&factory.magic==123);if(method==0)CHECK(!out);if(method==2)CHECK(!chain_out);CHECK(trace_errors==(valid?0u:1u));cases++;}}
Device*out=(void*)(uintptr_t)1;CHECK(d3d_create_device(&factory,0,1,NULL,0x22,NULL,&out)==-1&&!out);CHECK(device_reset(&device,NULL)==-1);Chain*chain_out=(void*)(uintptr_t)1;CHECK(${chain}(&device,NULL,&chain_out)==-1&&!chain_out);cases+=3;
printf("PASS actual API ${api} presentation preflight: %u constructor/Reset/additional-chain interval guards, NULL output initialization and unchanged input/device fields except Reset status checks; accepted cases stop before allocation/window work\\n",cases);return 0;}
`);const b=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-parameter','-I',root,dir+'/entry.c',process.env.D9WG_PARAMETERS_RUNTIME||root+'/runtime/d9wg_present_pacing.c','-o',dir+'/entry'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+'/entry',[],{encoding:'utf8'});assert.equal(r.status,0,r.stderr+' signal='+r.signal);process.stdout.write(r.stdout);}}finally{fs.rmSync(dir,{recursive:true,force:true});}
