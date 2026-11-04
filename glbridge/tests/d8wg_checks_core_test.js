"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),eight=fs.readFileSync(process.env.D8WG_CHECKS_SOURCE||root+"/d3d8proxy/d3d8_proxy.c","utf8"),nine=fs.readFileSync(process.env.D8WG_CHECKS_BACKEND_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-checks-core-"));
function extract(source,name){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end).replace(/__attribute__\s*\(\(__stdcall__\)\)/g,"");}
function preprocess(api,body){const file=dir+"/sdk"+api+".c",macros=nine.match(/^#define D9_FOURCC[^\n]*\n[^\n]*\n[^\n]*/m)[0]+"\n"+["DF16","DF24","INTZ","NULL","RAWZ"].map(n=>nine.match(new RegExp("^#define D9FMT_"+n+" [^\\n]+","m"))[0]).join("\n");fs.writeFileSync(file,`#define COBJMACROS\n#include <d3d${api}.h>\n#define TRACE(...)\n#define TRACE_REFUSE(v) (v)\n#define D8WG_TRACE_ERROR(v) (v)\n#define HOSTLOG_REFUSED(...)\n`+macros+"\n"+body.replace(/\bWINAPI\b/g,""));const p=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",["-E","-P",file],{encoding:"utf8",maxBuffer:16<<20});assert.equal(p.status,0,p.stderr);return p.stdout;}
try{
 const names=["d3d_check_device_type","d3d_check_multisample","d3d_check_depth_stencil"],shared=["supported_render_target_format","supported_depth_stencil_format","supported_backbuffer_format","supported_multisample"],bridges=["d9wg_d3d9_core_check_type","d9wg_d3d9_core_check_multisample8","d9wg_d3d9_core_check_depth"];
 const sdk8=preprocess(8,names.map(n=>extract(eight,n)).join("\n")+fs.readFileSync(root+"/tests/fixtures/d3d8_checks_before_core.c","utf8")),sdk9=preprocess(9,[...shared,"supported_render_target_format8","supported_depth_stencil_format8",...names,...bridges].map(n=>extract(nine,n)).join("\n")+fs.readFileSync(root+"/tests/fixtures/d3d9_checks_before_core.c","utf8"));
 const enums=["D3DFORMAT","D3DDEVTYPE","D3DMULTISAMPLE_TYPE"].map(n=>sdk9.match(new RegExp("typedef enum _"+n+"\\s*\\{[\\s\\S]*?\\}\\s*"+n+"\\s*;"))[0]).join("\n"),observe=s=>s.replace(/\(factory\)->lpVtbl->CheckDeviceType\s*\(factory,/g,"provider_type(factory,").replace(/\(factory\)->lpVtbl->CheckDeviceMultiSampleType\s*\(factory,/g,"provider_ms(factory,").replace(/\(factory\)->lpVtbl->CheckDepthStencilMatch\s*\(factory,/g,"provider_depth(factory,");
 const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t BYTE;typedef uint32_t UINT,DWORD;typedef int32_t HRESULT;typedef int BOOL,WINBOOL;
#define TRUE 1
#define FALSE 0
${enums}
typedef struct{UINT compatibility_api;}IDirect3D9;typedef struct{IDirect3D9*backend;}IDirect3D8;
#define d3d_from_iface(p) (p)
static UINT checks,type_cases,ms_cases,depth_cases,type_calls,ms_calls,depth_calls,observed[6],injection,common_calls;
${extract(sdk9,"supported_render_target_format").replace(/\bsupported_render_target_format\b/g,"render_actual")}
${extract(sdk9,"supported_depth_stencil_format").replace(/\bsupported_depth_stencil_format\b/g,"depth_actual")}
static BOOL supported_render_target_format(D3DFORMAT f){common_calls++;return !(injection==1&&f==21)&&render_actual(f);}
static BOOL supported_depth_stencil_format(D3DFORMAT f){common_calls++;return !(injection==1&&f==80)&&depth_actual(f);}
${extract(sdk9,"supported_backbuffer_format")}
${extract(sdk9,"supported_multisample").replace(/\bsupported_multisample\b/g,"ms_actual")}
static BOOL supported_multisample(D3DFORMAT f,D3DMULTISAMPLE_TYPE n){common_calls++;return !(injection==2&&n==4)&&ms_actual(f,n);}
${["supported_render_target_format8","supported_depth_stencil_format8"].map(n=>extract(sdk9,n)).join("\n")}
${names.map(n=>extract(sdk9,n).replace(new RegExp("\\b"+n+"\\b","g"),n+"_actual9")).join("\n")}
static HRESULT provider_type(IDirect3D9*r,UINT a,D3DDEVTYPE t,D3DFORMAT d,D3DFORMAT b,BOOL w){type_calls++;UINT v[]={a,t,d,b,(UINT)w,0};memcpy(observed,v,sizeof(v));return d3d_check_device_type_actual9(r,a,t,d,b,w);}
static HRESULT provider_ms(IDirect3D9*r,UINT a,D3DDEVTYPE t,D3DFORMAT f,BOOL w,D3DMULTISAMPLE_TYPE n,DWORD*q){ms_calls++;UINT v[]={a,t,f,(UINT)w,n,q!=NULL};memcpy(observed,v,sizeof(v));return d3d_check_multisample_actual9(r,a,t,f,w,n,q);}
static HRESULT provider_depth(IDirect3D9*r,UINT a,D3DDEVTYPE t,D3DFORMAT d,D3DFORMAT b,D3DFORMAT f){depth_calls++;UINT v[]={a,t,d,b,f,0};memcpy(observed,v,sizeof(v));return d3d_check_depth_stencil_actual9(r,a,t,d,b,f);}
${bridges.map(n=>observe(extract(sdk9,n))).join("\n")}
${["supported_render_target_format","supported_depth_stencil_format","supported_backbuffer_format","supported_multisample_type",...names].map(n=>extract(sdk8,n+"_before8")).join("\n")}
${names.map(n=>extract(sdk8,n)).join("\n")}
${[...shared,...names].map(n=>extract(sdk9,n+"_before9")).join("\n")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"D3D8 checks core line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){(void)&provider_type;(void)&provider_ms;(void)&provider_depth;${bridges.map(n=>"(void)&"+n+";").join("")}
 IDirect3D9 b8={8},b9={9};IDirect3D8 r8={&b8};const UINT adapters[]={0,1,UINT32_MAX},types[]={0,1,2,3,UINT32_MAX};const int windows[]={0,1,-1};const D3DFORMAT formats[]={0,20,21,22,23,24,25,26,28,50,60,70,71,73,75,77,79,80,82,111,112,113,114,115,116,0x5a544e49u,0x36314644u,0x34324644u,0x5a574152u,0x4c4c554eu,0x31545844u,0x7fffffffu,(D3DFORMAT)UINT32_MAX};const UINT nf=sizeof(formats)/sizeof(formats[0]);
 for(UINT a=0;a<3;a++)for(UINT t=0;t<5;t++)for(UINT w=0;w<3;w++)for(UINT f=0;f<nf;f++){
  for(UINT b=0;b<nf;b++){UINT c=type_calls,v[]={adapters[a],types[t],formats[f],formats[b],(UINT)windows[w],0};HRESULT old=d3d_check_device_type_before8(&r8,adapters[a],types[t],formats[f],formats[b],windows[w]);CHECK(d3d_check_device_type(&r8,adapters[a],types[t],formats[f],formats[b],windows[w])==old&&type_calls==c+1&&!memcmp(v,observed,sizeof(v)));CHECK(d3d_check_device_type_actual9(&b9,adapters[a],types[t],formats[f],formats[b],windows[w])==d3d_check_device_type_before9(&b9,adapters[a],types[t],formats[f],formats[b],windows[w]));type_cases++;}
  for(UINT n=0;n<20;n++){UINT sample=n==19?UINT32_MAX:n,c=ms_calls,v[]={adapters[a],types[t],formats[f],(UINT)windows[w],sample,0};HRESULT old=d3d_check_multisample_before8(&r8,adapters[a],types[t],formats[f],windows[w],sample);CHECK(d3d_check_multisample(&r8,adapters[a],types[t],formats[f],windows[w],sample)==old&&ms_calls==c+1&&!memcmp(v,observed,sizeof(v)));DWORD quality=0xa5a5a5a5u,old_quality=quality;CHECK(d3d_check_multisample_actual9(&b9,adapters[a],types[t],formats[f],windows[w],sample,&quality)==d3d_check_multisample_before9(&b9,adapters[a],types[t],formats[f],windows[w],sample,&old_quality)&&quality==old_quality);CHECK(d3d_check_multisample_actual9(&b9,adapters[a],types[t],formats[f],windows[w],sample,NULL)==d3d_check_multisample_before9(&b9,adapters[a],types[t],formats[f],windows[w],sample,NULL));ms_cases++;}
  for(UINT d=0;d<3;d++)for(UINT b=0;b<3;b++){UINT c=depth_calls,v[]={adapters[a],types[t],formats[d],formats[b],formats[f],0};HRESULT old=d3d_check_depth_stencil_before8(&r8,adapters[a],types[t],formats[d],formats[b],formats[f]);CHECK(d3d_check_depth_stencil(&r8,adapters[a],types[t],formats[d],formats[b],formats[f])==old&&depth_calls==c+1&&!memcmp(v,observed,sizeof(v)));CHECK(d3d_check_depth_stencil_actual9(&b9,adapters[a],types[t],formats[d],formats[b],formats[f])==d3d_check_depth_stencil_before9(&b9,adapters[a],types[t],formats[d],formats[b],formats[f]));depth_cases++;}
 }
 injection=1;UINT c=common_calls;CHECK(d3d_check_device_type(&r8,0,1,22,21,1)==(HRESULT)0x8876086au&&common_calls==c+1);c=common_calls;CHECK(d3d_check_depth_stencil(&r8,0,1,22,21,80)==(HRESULT)0x8876086au&&common_calls==c+1);injection=2;c=common_calls;CHECK(d3d_check_multisample(&r8,0,1,0,1,4)==(HRESULT)0x8876086au&&common_calls==c+1);
 printf("PASS actual D3D8 checks/core C: %u checks,%u type,%u MSAA,%u depth matrix cases; prior8/9 results and9 quality outputs,all argument forwarding,version restrictions/legacy acceptance and observed common-predicate propagation\\n",checks,type_cases,ms_cases,depth_cases);return 0;
}
`;
 fs.writeFileSync(dir+"/checks.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/checks.c","-o",dir+"/checks"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/checks",[],{encoding:"utf8",timeout:10000});assert.equal(r.status,0,r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
