"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),eight=readComparisonSource(process.env.D8WG_SHADER_VALIDATION_SOURCE||root+"/d3d8proxy/d3d8_proxy.c","utf8"),nine=readComparisonSource(process.env.D8WG_SHADER_VALIDATION_BACKEND_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-backend-"));
function extract(source,name){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end).replace(/__attribute__\s*\(\(__stdcall__\)\)/g,"");}
// S2-41 contract delta to the frozen legacy oracle: CND is legal in ps_1_4.
function legacyCnd14(source){const before="if (!is_pixel_shader || minor > 3u) return FALSE;";assert.equal(source.split(before).length,2);return source.replace(before,"if (!is_pixel_shader || minor > 4u) return FALSE;");}
function preprocess(api,body){const file=dir+"/sdk"+api+".c";fs.writeFileSync(file,`#include <d3d${api}.h>\n#include "${root}/d3d9proxy/d3d9_core.h"\n#define TRACE(...)\n#define D8WG_TRACE(...)\n`+body.replace(/\bWINAPI\b/g,""));const p=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",["-E","-P",file],{encoding:"utf8",maxBuffer:16<<20});assert.equal(p.status,0,p.stderr);return p.stdout;}
try{
 const sdk8=preprocess(8,legacyCnd14(readComparisonSource(root+"/tests/fixtures/d3d8_shader_validation_before_core.c","utf8"))+extract(eight,"validate_shader_body").replace(/\bvalidate_shader_body\b/g,"frontend_validate")),names=["shader8_regtype_valid","shader8_opcode_supported","shader8_validate_body","d9wg_d3d9_core_validate_shader_body8"],sdk9=preprocess(9,names.map(n=>extract(nine,n)).join("\n"));
 function enumeration(s,n){const m=s.match(new RegExp("typedef enum _"+n+"\\s*\\{[\\s\\S]*?\\}\\s*"+n+"\\s*;"));assert(m,n);return m[0];}
 const reg8=s=>s.replace(/\b(D3DSPR_\w+|_D3DSHADER_PARAM_REGISTER_TYPE|D3DSHADER_PARAM_REGISTER_TYPE)\b/g,"before8_$1"),before=s=>reg8(s).replace(/\b(shader_regtype_valid|shader_opcode_supported|validate_shader_body)\b/g,"before8_$1");
 const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
typedef uint32_t DWORD,UINT;typedef uint16_t WORD;typedef int BOOL;
${enumeration(sdk9,"D3DSHADER_INSTRUCTION_OPCODE_TYPE")}
${enumeration(sdk9,"D3DSHADER_PARAM_REGISTER_TYPE")}
${reg8(enumeration(sdk8,"D3DSHADER_PARAM_REGISTER_TYPE"))}
${["shader_regtype_valid","shader_opcode_supported","validate_shader_body"].map(n=>before(extract(sdk8,n))).join("\n")}
${names.slice(0,3).map(n=>extract(sdk9,n)).join("\n")}
${extract(sdk9,names[3]).replace(/\bd9wg_d3d9_core_validate_shader_body8\b/g,"core_validate_actual")}
static UINT provider_calls,injection,observed_max,observed_minor;static BOOL observed_pixel;static const DWORD*observed_tokens;static UINT*observed_count;
static BOOL d9wg_d3d9_core_validate_shader_body8(const DWORD*t,UINT m,BOOL p,UINT v,UINT*c){provider_calls++;observed_tokens=t;observed_max=m;observed_minor=v;observed_pixel=p;observed_count=c;BOOL r=core_validate_actual(t,m,p,v,c);if(injection){*c^=0x40000000u;return !r;}return r;}
${before(extract(sdk8,"frontend_validate"))}
static UINT checks,op_cases,body_cases;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Shader8 policy line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){(void)&d9wg_d3d9_core_validate_shader_body8;_Static_assert(D3DSPR_INPUT==1&&before8_D3DSPR_INPUT==0x10000000u,"real SDK8 shifted andSDK9 ordinal registers");
 for(UINT pixel=0;pixel<2;pixel++)for(UINT r=0;r<8;r++)CHECK(shader8_regtype_valid(r<<28,pixel)==before8_shader_regtype_valid(r<<28,pixel));
 for(UINT pixel=0;pixel<2;pixel++)for(UINT minor=0;minor<6;minor++)for(UINT op=0;op<65536;op++){UINT a=0xa5a5a5a5u,b=a;BOOL good=shader8_opcode_supported((WORD)op,pixel,minor,&a),old=before8_shader_opcode_supported((WORD)op,pixel,minor,&b);CHECK(good==old);CHECK(a==b);op_cases++;}
 for(UINT pixel=0;pixel<2;pixel++)for(UINT minor=0;minor<5;minor++)for(UINT selector=0;selector<100;selector++)for(UINT dst=0;dst<8;dst++)for(UINT src=0;src<8;src++)for(UINT limit=0;limit<10;limit++){
  DWORD tokens[16];for(UINT i=0;i<16;i++)tokens[i]=0xffffu;UINT op=selector<97?selector:selector==97?0xfffdu:selector==98?0xfffeu:0xffffu,words=5;before8_shader_opcode_supported((WORD)op,pixel,minor,&words);tokens[0]=op;for(UINT i=1;i<=words;i++)tokens[i]=0x80000000u|((i==1?dst:src)<<28)|(i==1?0x000f0000u:0x00e40000u);if(op==81){tokens[2]=0xffffu;tokens[3]=0x80000000u;tokens[4]=0x7f800000u;tokens[5]=0x7fc00000u;}if(op==0xfffe)tokens[0]|=(dst*3u)<<16;DWORD saved[16];memcpy(saved,tokens,sizeof(tokens));
  struct{UINT pre,count,post;}actual={0x1234abcd,0xa5a5a5a5u,0xdcba4321},expected=actual;UINT calls=provider_calls;BOOL old=before8_validate_shader_body(tokens,limit,pixel,minor,&expected.count),good=frontend_validate(tokens,limit,pixel,minor,&actual.count);
  CHECK(good==old&&actual.count==expected.count);CHECK(actual.pre==0x1234abcd&&actual.post==0xdcba4321&&!memcmp(tokens,saved,sizeof(tokens)));CHECK(provider_calls==calls+1&&observed_tokens==tokens&&observed_max==limit&&observed_pixel==(BOOL)pixel&&observed_minor==minor&&observed_count==&actual.count);body_cases++;
 }
 const UINT limits[]={1,2,6,32,8191,8192};for(UINT b=0;b<6;b++){UINT n=limits[b];DWORD*long_body=calloc(n,sizeof(DWORD));assert(long_body);long_body[n-1]=0xffff;for(UINT cut=0;cut<2;cut++){UINT before_count=0xa5a5a5a5u,new_count=before_count;BOOL expected=before8_validate_shader_body(long_body,n-cut,0,0,&before_count),actual=frontend_validate(long_body,n-cut,0,0,&new_count);CHECK(actual==expected&&new_count==before_count);CHECK(actual==(cut==0)&&(actual?new_count==n-1:new_count==0xa5a5a5a5u));}free(long_body);}
 const DWORD comments[]={0x7ffffffeu,0xffffu};UINT untouched=0x12345678;CHECK(!frontend_validate(comments,2,0,0,&untouched)&&untouched==0x12345678);
 const DWORD valid[]={0,0xffff};for(UINT pixel=0;pixel<2;pixel++){UINT count=0x1234,injected=0;injection=1;CHECK(!frontend_validate(valid,2,pixel,0,&count)&&count==(1u^0x40000000u));injection=0;CHECK(core_validate_actual(valid,2,pixel,0,&injected)&&injected==1);}
 UINT count=0x12345678;CHECK(!core_validate_actual(NULL,2,0,0,&count)&&count==0x12345678);CHECK(!core_validate_actual(valid,2,0,0,NULL));
 printf("PASS actual shader8 core policy: %u checks,%u opcode/model cases,%u bounded body/delegation cases; priorSDK8 semantics,SDK9 register normalization,comments/DEF literals/truncation,all arguments andobserved provider propagation\\n",checks,op_cases,body_cases);return 0;}
`;
 fs.writeFileSync(dir+"/policy.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/policy.c","-o",dir+"/policy"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/policy",[],{encoding:"utf8",timeout:30000});assert.equal(r.status,0,r.stdout+r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
