"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),eight=fs.readFileSync(process.env.D8WG_CHECKS_SOURCE||root+"/d3d8proxy/d3d8_proxy.c","utf8"),nine=fs.readFileSync(process.env.D8WG_BACKEND_RESET_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-backend-"));
function extract(source,name){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end).replace(/__attribute__\s*\(\(__stdcall__\)\)/g,"");}
function preprocess(api,body){const file=dir+"/sdk"+api+".c",macros=nine.match(/^#define D9_FOURCC[^\n]*\n[^\n]*\n[^\n]*/m)[0]+"\n"+["DF16","DF24","INTZ","NULL","RAWZ"].map(n=>nine.match(new RegExp("^#define D9FMT_"+n+" [^\\n]+","m"))[0]).join("\n");fs.writeFileSync(file,`#define COBJMACROS\n#include <d3d${api}.h>\n#define TRACE(...)\n#define TRACE_REFUSE(v) (v)\n#define D8WG_TRACE_ERROR(v) (v)\n#define HOSTLOG_REFUSED(...)\n#define HOSTLOG_FAILED(...)\n`+macros+"\n"+body.replace(/\bWINAPI\b/g,""));const p=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",["-E","-P",file],{encoding:"utf8",maxBuffer:16<<20});assert.equal(p.status,0,p.stderr);return p.stdout;}
try{
 const shared=["supported_render_target_format","supported_depth_stencil_format","supported_backbuffer_format","supported_multisample"],sdk9=preprocess(9,[...shared,"supported_render_target_format8","supported_depth_stencil_format8","device_reset"].map(n=>extract(nine,n)).join("\n")+fs.readFileSync(root+"/tests/fixtures/d3d9_reset_before_backend8.c","utf8").replace(/\bdevice_reset\b/g,"reset_before9")),sdk8=preprocess(8,["supported_render_target_format","supported_depth_stencil_format","supported_backbuffer_format","supported_multisample_type"].map(n=>extract(eight,n)).join("\n").replace(/\bsupported_(render_target_format|depth_stencil_format|backbuffer_format|multisample_type)\b/g,"supported_$1_before8")+fs.readFileSync(root+"/tests/fixtures/d3d8_reset_before_backend8.c","utf8").replace(/\bdevice_reset\b/g,"reset_before8").replace(/\bsupported_(render_target_format|depth_stencil_format|backbuffer_format|multisample_type)\b/g,"supported_$1_before8"));
 const enums=["D3DFORMAT","D3DDEVTYPE","D3DMULTISAMPLE_TYPE"].map(n=>sdk9.match(new RegExp("typedef enum _"+n+"\\s*\\{[\\s\\S]*?\\}\\s*"+n+"\\s*;"))[0]).join("\n");
 function prefix(s,n){const b=extract(s,n),end=b.indexOf("    window =");assert(end>0,n);return b.slice(0,end)+"return 0x12345678;\n}";}
 const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
#include "runtime/d9wg_present_pacing.h"
#include "runtime/d9wg_present_multisample.h"
#include "runtime/d9wg_swap_effect.h"
#include "runtime/d9wg_reset.h"
typedef uint8_t BYTE;typedef uint32_t UINT,DWORD;typedef int32_t HRESULT;typedef int BOOL;typedef uintptr_t HWND;
${enums}
typedef struct{int x,y;}POINT;typedef struct{int left,top,right,bottom;}RECT;
typedef struct{UINT compatibility_api;D9WGResetState reset_state;struct{UINT Format;}display_mode;}IDirect3D9,IDirect3DDevice9,D9Device,IDirect3DDevice8,D8Device;
#define device_from_iface(p) (p)
#define device_compatibility_api(p) ((p)->compatibility_api)
typedef struct{UINT BackBufferWidth,BackBufferHeight;D3DFORMAT BackBufferFormat;UINT BackBufferCount;D3DMULTISAMPLE_TYPE MultiSampleType;UINT MultiSampleQuality;UINT SwapEffect;HWND hDeviceWindow;BOOL Windowed,EnableAutoDepthStencil;D3DFORMAT AutoDepthStencilFormat;DWORD Flags;UINT FullScreen_RefreshRateInHz;UINT PresentationInterval,FullScreen_PresentationInterval;}D3DPRESENT_PARAMETERS;
#define d3d_from_iface(p) (p)
static UINT desktop_queries,desktop_format=22;
static UINT d9wg_display_mode_backbuffer_format(UINT f){if(f)return f;desktop_queries++;return desktop_format;}
${shared.map(n=>extract(sdk9,n)).join("\n")}
${["supported_render_target_format8","supported_depth_stencil_format8"].map(n=>extract(sdk9,n)).join("\n")}
${["supported_render_target_format","supported_depth_stencil_format","supported_backbuffer_format","supported_multisample_type"].map(n=>extract(sdk8,n+"_before8")).join("\n")}
${prefix(sdk8,"reset_before8")}
${prefix(sdk9,"reset_before9")}
${prefix(sdk9,"device_reset")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Backend8 Reset preflight line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){UINT checks=0,cases=0;(void)&supported_render_target_format8;(void)&supported_depth_stencil_format8;IDirect3D9 r8={.compatibility_api=8,.display_mode={21}},r9={.compatibility_api=9,.display_mode={21}};
const UINT formats[]={0,20,21,22,23,24,25,26,28,50,60,70,71,73,75,77,79,80,82,111,112,113,114,115,116,0x5a544e49u,0x36314644u,0x34324644u,0x5a574152u,0x4c4c554eu,0x31545844u,0x7fffffffu,UINT32_MAX};
const UINT behavior[]={0,0x20,0x40,0x80,0x60,0x10,0x30,0x50,UINT32_MAX},intervals[]={0,1,2,4,8,0x80000000u,3,UINT32_MAX},samples[]={0,1,2,4,8,16,UINT32_MAX};
for(UINT api=8;api<=9;api++)for(UINT windowed=0;windowed<2;windowed++)for(UINT f=0;f<33;f++)for(UINT swap=0;swap<6;swap++)for(UINT n=0;n<7;n++)for(UINT flags=0;flags<2;flags++){
 D3DPRESENT_PARAMETERS p={.BackBufferFormat=formats[f],.SwapEffect=swap,.BackBufferCount=1,.MultiSampleType=samples[n],.Flags=flags,.Windowed=windowed,.PresentationInterval=0,.FullScreen_PresentationInterval=0},before=p;IDirect3D9*out=(void*)(uintptr_t)1,*old=out;desktop_queries=0;
 HRESULT a=device_reset(api==8?&r8:&r9,&p);UINT queries=desktop_queries;desktop_queries=0;
 HRESULT expected=api==8?reset_before8(&r8,&p):reset_before9(&r9,&p);
 CHECK(a==expected&&!memcmp(&p,&before,sizeof(p)));CHECK(queries==desktop_queries);cases++;
}
for(UINT api=8;api<=9;api++)for(UINT i=0;i<8;i++)for(UINT windowed=0;windowed<2;windowed++)for(UINT f=0;f<33;f++)for(UINT depth=0;depth<33;depth++){
 D3DPRESENT_PARAMETERS p={.BackBufferFormat=formats[f],.SwapEffect=1,.EnableAutoDepthStencil=1,.AutoDepthStencilFormat=formats[depth],.Windowed=windowed,.PresentationInterval=intervals[i],.FullScreen_PresentationInterval=intervals[i]},before=p;IDirect3D9*out=(void*)(uintptr_t)1,*old=out;
 HRESULT a=device_reset(api==8?&r8:&r9,&p),expected=api==8?reset_before8(&r8,&p):reset_before9(&r9,&p);CHECK(a==expected&&!memcmp(&p,&before,sizeof(p)));cases++;
}
for(UINT api=8;api<=9;api++){D3DPRESENT_PARAMETERS p={.BackBufferCount=4},before=p;CHECK(device_reset(api==8?&r8:&r9,&p)==(HRESULT)0x8876086cu&&!memcmp(&p,&before,sizeof(p)));CHECK(device_reset(api==8?&r8:&r9,NULL)==(HRESULT)0x8876086cu);cases+=2;}
printf("PASS actual backend8 Reset preflight: %u checks,%u prior8/9 matrix cases; legacy copy-vsync,MSAA-lockable,formats,depth,intervals,guarded preflight and9 desktop query preservation\\n",checks,cases);return 0;}
`;
 fs.writeFileSync(dir+"/backend.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-Wno-unused-variable","-Wno-unused-parameter","-I",root,dir+"/backend.c",root+"/runtime/d9wg_present_pacing.c","-o",dir+"/backend"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/backend",[],{encoding:"utf8",timeout:10000});assert.equal(r.status,0,r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
