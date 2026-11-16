"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),eight=fs.readFileSync(process.env.D8WG_FORMAT_SOURCE||root+"/d3d8proxy/d3d8_proxy.c","utf8"),nine=fs.readFileSync(process.env.D8WG_FORMAT_BACKEND_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-format-core-"));
function extract(source,name){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end).replace(/__attribute__\s*\(\(__stdcall__\)\)/g,"");}
function preprocess(api,body){const file=dir+"/sdk"+api+".c",macros=nine.match(/^#define D9_FOURCC[^\n]*\n[^\n]*\n[^\n]*/m)[0]+"\n"+[...nine.matchAll(/^#define (?:D9FMT_|D9FOURCC_)[^\n]+/gm)].map(m=>m[0]).join("\n");fs.writeFileSync(file,`#define COBJMACROS\n#include <d3d${api}.h>\n#define TRACE(...)\n#define TRACE_REFUSE(v) (v)\n#define D8WG_TRACE_ERROR(v) (v)\n#define D8WG_TRACE(...)\n#define HOSTLOG_REFUSED(...)\n#define HOSTLOG_INFO(...)\n`+macros+"\n"+body.replace(/\bWINAPI\b/g,""));const p=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",["-E","-P",file],{encoding:"utf8",maxBuffer:16<<20});assert.equal(p.status,0,p.stderr);return p.stdout;}
try{
 const shared=["supported_texture_format","supported_render_target_format","supported_depth_stencil_format","supported_volume_texture_format","texture_create_supported","supported_render_target_format8","supported_depth_stencil_format8","supported_format8","d3d_check_device_format8","d3d_check_device_format","d9wg_d3d9_core_check_format8"];
 const sdk8=preprocess(8,extract(eight,"d3d_check_device_format")+fs.readFileSync(root+"/tests/fixtures/d3d8_format_before_core.c","utf8")),sdk9=preprocess(9,shared.map(n=>extract(nine,n)).join("\n")+fs.readFileSync(root+"/tests/fixtures/d3d9_format_before_core.c","utf8"));
 const enums=["D3DFORMAT","D3DDEVTYPE","D3DRESOURCETYPE","D3DPOOL"].map(n=>sdk9.match(new RegExp("typedef enum _"+n+"\\s*\\{[\\s\\S]*?\\}\\s*"+n+"\\s*;"))[0]).join("\n");
 const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t BYTE;typedef uint32_t UINT,DWORD;typedef int32_t HRESULT;typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define D3DERR_INVALIDCALL ((HRESULT)0x8876086cu)
#define D3DERR_NOTAVAILABLE ((HRESULT)0x8876086au)
#include "${root}/d3d9proxy/d3d9_protocol.h"
#define d9wg_format_layout layout_actual
#include "${root}/runtime/d9wg_format_layouts.h"
#undef d9wg_format_layout
#include "${root}/runtime/d9wg_format_queries.h"
${enums}
typedef struct{UINT compatibility_api;}IDirect3D9;typedef struct{IDirect3D9*backend;}IDirect3D8;
#define d3d_from_iface(p) (p)
static UINT checks,cases,backend_calls,layout_calls,observed[6],profile,features,host_calls,injection;
static UINT shader_model_version(void){return profile;}
static uint32_t runtime_host_features(void){host_calls++;return features;}
static int d9wg_format_layout(UINT api,UINT format,UINT*w,UINT*h,UINT*b){layout_calls++;return !(injection&&format==21)&&layout_actual(api,format,w,h,b);}
${shared.filter(n=>!n.startsWith("d3d_check_device_format")&&!n.startsWith("d9wg_")).map(n=>extract(sdk9,n)).join("\n")}
${extract(sdk9,"d3d_check_device_format8")}
${extract(sdk9,"d3d_check_device_format").replace(/\bd3d_check_device_format\b/g,"check_actual9")}
static HRESULT provider_format(IDirect3D9*r,UINT a,D3DDEVTYPE t,D3DFORMAT d,DWORD u,D3DRESOURCETYPE k,D3DFORMAT f){backend_calls++;UINT v[]={a,t,d,u,k,f};memcpy(observed,v,sizeof(v));return check_actual9(r,a,t,d,u,k,f);}
${extract(sdk9,"d9wg_d3d9_core_check_format8").replace(/\(factory\)->lpVtbl->CheckDeviceFormat\s*\(factory,/g,"provider_format(factory,")}
${["texture_format_layout","supported_image_surface_format","supported_texture_format","supported_render_target_format","supported_depth_stencil_format","d3d_check_device_format"].map(n=>extract(sdk8,n+"_before8")).join("\n")}
${extract(sdk8,"d3d_check_device_format")}
${extract(sdk9,"d3d_check_device_format_before9")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"D3D8 format core line %u: %s profile=%u features=%x\\n",__LINE__,#v,profile,features);return 1;}checks++;}while(0)
int main(void){(void)&provider_format;(void)&d9wg_d3d9_core_check_format8;(void)&supported_depth_stencil_format8;(void)&supported_render_target_format8;IDirect3D9 b8={8},b9={9};IDirect3D8 r8={&b8};const UINT usages[]={0,1,2,3,8,16,512,513,514,515,1024,1025,1026,16384,32768,65536,131072,262144,524288,1048576,2097152,0x3f8000,0x20001,0x20002,0x40001,0xffffffffu,0x80000000u},adapters[]={0,1,UINT32_MAX},types[]={1,2,UINT32_MAX},resources[]={0,1,2,3,4,5,6,7,UINT32_MAX},extra[]={0,UINT32_MAX,0x4643502du,0x5a574152u,0x4c4c554eu};const UINT count=sizeof(d9wg_format_layouts)/sizeof(d9wg_format_layouts[0]),nu=sizeof(usages)/sizeof(usages[0]);
 for(UINT p=0;p<3;p++)for(UINT h=0;h<2;h++){profile=p==0?0:p==1?2:3;features=h?D9WG_HOST_FEATURE_MASK:0;
 for(UINT a=0;a<3;a++)for(UINT t=0;t<3;t++)for(UINT k=0;k<9;k++)for(UINT u=0;u<nu;u++)for(UINT i=0;i<count+5;i++){
  D3DFORMAT format=i<count?d9wg_format_layouts[i].format:extra[i-count],display=(i%3==0)?22:(i%3==1)?24:(D3DFORMAT)UINT32_MAX;UINT c=backend_calls,hc=host_calls,v[]={adapters[a],types[t],display,usages[u],resources[k],format};HRESULT old=resources[k]==6||resources[k]==7 ? D3DERR_INVALIDCALL : d3d_check_device_format_before8(&r8,adapters[a],types[t],display,usages[u],resources[k],format);CHECK(d3d_check_device_format(&r8,adapters[a],types[t],display,usages[u],resources[k],format)==old&&backend_calls==c+1&&host_calls==hc&&!memcmp(v,observed,sizeof(v)));
  UINT before=host_calls;HRESULT old9=d3d_check_device_format_before9(&b9,adapters[a],types[t],display,usages[u],resources[k],format);UINT calls=host_calls-before;
  if(resources[k]==6||resources[k]==7){old9=D3DERR_INVALIDCALL;calls=0;}
  else if(resources[k]==1&&(usages[u]&0x40000u)&&!(usages[u]&1u)){old9=D3DERR_NOTAVAILABLE;calls=0;}
  else if(old9==0&&(usages[u]&0x400u)&&!supported_render_target_format(format))old9=(HRESULT)0x0876086fu;
  before=host_calls;CHECK(check_actual9(&b9,adapters[a],types[t],display,usages[u],resources[k],format)==old9&&host_calls-before==calls);cases++;
 }}
 /* Wine test_check_device_format: UNKNOWN is rejected before device type,
  * ARGB display formats are unavailable, and buffer resource types invalid. */
 for(UINT type=1;type<=3;type++)for(UINT kind=1;kind<=7;kind++) {
  CHECK(d3d_check_device_format(&r8,0,type,0,0,kind,21)==D3DERR_INVALIDCALL);
  CHECK(check_actual9(&b9,0,type,0,0,kind,21)==D3DERR_INVALIDCALL);
  CHECK(check_actual9(&b9,0,type,21,0,kind,22)==(kind>=6?D3DERR_INVALIDCALL:D3DERR_NOTAVAILABLE));
  CHECK(d3d_check_device_format(&r8,0,type,21,0,kind,22)==(kind>=6?D3DERR_INVALIDCALL:D3DERR_NOTAVAILABLE));
 }
 injection=1;UINT c=layout_calls;CHECK(d3d_check_device_format(&r8,0,1,22,0,3,21)==(HRESULT)0x8876086au&&layout_calls==c+1);
 printf("PASS actual D3D8 format/core C: %u checks,%u resource/usage/format/profile cases,%u formats,27 usages,six shader/host configurations; prior8 plus reviewed native9 argument/query rules,all arguments,API8 layout/packed-surface/volume restrictions,host-feature query equivalence and observed shared-layout propagation\\n",checks,cases,count+5);return 0;
}
`;
 fs.writeFileSync(dir+"/format.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/format.c","-o",dir+"/format"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/format",[],{encoding:"utf8",timeout:20000});assert.equal(r.status,0,r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
