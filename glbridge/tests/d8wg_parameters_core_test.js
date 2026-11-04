"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),nine=fs.readFileSync(process.env.D8WG_PARAMETERS_BACKEND_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-parameters-core-"));
function extract(source,name){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end).replace(/__attribute__\s*\(\(__stdcall__\)\)/g,"");}
function preprocess(api){const file=dir+"/sdk"+api+".c";fs.writeFileSync(file,`#include <d3d${api}.h>\n#include "${root}/d3d9proxy/d3d9_core.h"\n#include "${root}/runtime/d9wg_present_parameters.h"\n`+fs.readFileSync(root+"/tests/fixtures/d3d_parameters_before_core.c","utf8")+(api===9?extract(nine,"d9wg_d3d9_core_parameters_effective8").replace(/\bWINAPI\b/g,""):""));const p=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",["-E","-P",file],{encoding:"utf8",maxBuffer:16<<20});assert.equal(p.status,0,p.stderr);return p.stdout;}
try{
 const sdk8=preprocess(8),sdk9=preprocess(9),rename=(s,n)=>s.replace(/\bD3DPRESENT_PARAMETERS\b/g,n);
 function type(s,n){const m=s.match(/typedef struct _D3DPRESENT_PARAMETERS_\s*\{[\s\S]*?\}\s*D3DPRESENT_PARAMETERS\s*;/);assert(m);return rename(m[0].replace('struct _D3DPRESENT_PARAMETERS_','struct'),n);}
 const code=`#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t UINT,DWORD,HWND,D3DFORMAT,D3DMULTISAMPLE_TYPE,D3DSWAPEFFECT;typedef uint8_t BYTE;typedef int32_t WINBOOL;
#pragma pack(push,4)
${type(sdk8,"PP8")}
${type(sdk9,"PP9")}
#pragma pack(pop)
static UINT checks,cases,provider_calls,injection,seed=0x8fe60001u;static PP8 observed_input;
${rename(extract(sdk8,"parameters_before_core"),"PP8").replace(/\bparameters_before_core\b/g,"parameters_before8")}
${rename(extract(sdk9,"parameters_before_core"),"PP9").replace(/\bparameters_before_core\b/g,"parameters_before9")}
${rename(extract(sdk9,"d9wg_present_parameters_effective"),"PP9").replace(/\bd9wg_present_parameters_effective\b/g,"effective_actual9")}
static void d9wg_present_parameters_effective(PP9*out,const PP9*in,UINT w,UINT h,UINT f,HWND window){provider_calls++;assert(in->MultiSampleQuality==0);assert(!memcmp(in,&observed_input,20));assert(!memcmp((const BYTE*)in+24,(const BYTE*)&observed_input+20,32));PP9 before;parameters_before9(&before,in,w,h,f,window);effective_actual9(out,in,w,h,f,window);assert(!memcmp(out,&before,56));if(injection){out->BackBufferCount=2;out->Flags^=0x40000000u;}}
${rename(extract(sdk9,"d9wg_d3d9_core_parameters_effective8"),"PP9")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"PP8 core line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
static UINT next(void){seed=seed*1664525u+1013904223u;return seed;}
int main(void){(void)&d9wg_present_parameters_effective;
 _Static_assert(sizeof(PP8)==52&&sizeof(PP9)==56,"SDK parameter sizes");_Static_assert(__builtin_offsetof(PP8,FullScreen_PresentationInterval)==48&&__builtin_offsetof(PP9,PresentationInterval)==52,"SDK parameter final fields");
 for(UINT round=0;round<1024;round++)for(UINT cfg=0;cfg<8;cfg++)for(UINT alias=0;alias<2;alias++)for(injection=0;injection<2;injection++){
  struct Guard{DWORD before[4];PP8 p;DWORD after[8];}input,output,expected_input,expected_output;memset(&input,0x5a,sizeof(input));memset(&output,0xa5,sizeof(output));DWORD words[13];for(UINT i=0;i<13;i++)words[i]=next();words[3]=cfg%4==0?0:cfg%4==1?1:cfg%4==2?3:UINT32_MAX;words[5]=(cfg&3)+1;words[6]=cfg&1?0:next();words[7]=cfg&1;words[12]=cfg&2?0x80000000u:0;
  memcpy(&input.p,words,52);observed_input=input.p;expected_input=input;expected_output=output;UINT w=cfg&1?next():0,h=cfg&2?8192:next(),f=cfg&4?24:next();HWND window=cfg&1?0:next();PP8 expected;parameters_before8(&expected,&input.p,w,h,f,window);if(injection){expected.BackBufferCount=2;expected.Flags^=0x40000000u;}if(alias)expected_input.p=expected;else expected_output.p=expected;
  UINT before=provider_calls;d9wg_d3d9_core_parameters_effective8(alias?&input.p:&output.p,&input.p,w,h,f,window);CHECK(provider_calls==before+1);CHECK(!memcmp(&input,&expected_input,sizeof(input)));CHECK(!memcmp(&output,&expected_output,sizeof(output)));cases++;
 }
 printf("PASS actual PP8/core C: %u checks,%u SDK8/9 metadata/provider/alias cases; exact52/56-byte layouts,zero quality,unchanged legacy swap/interval/flags,old8/9 normalization,all13 words,guarded immutable input and in-place propagation\\n",checks,cases);return 0;
}
`;
 fs.writeFileSync(dir+"/parameters.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/parameters.c","-o",dir+"/parameters"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/parameters",[],{encoding:"utf8",timeout:10000});assert.equal(r.status,0,r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
