"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),nine=fs.readFileSync(process.env.D8WG_BACKEND_RESET_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-backend-bridge-"));
function extract(source,name){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end).replace(/__attribute__\s*\(\(__stdcall__\)\)/g,"");}
function preprocess(api){const file=dir+"/sdk"+api+".c";fs.writeFileSync(file,`#include <d3d${api}.h>\n#include "${root}/d3d9proxy/d3d9_core.h"\n#include "${root}/runtime/d9wg_present_parameters.h"\n`+fs.readFileSync(root+"/tests/fixtures/d3d_parameters_before_core.c","utf8")+(api===9?(extract(nine,"parameters8_to9")+"\n"+extract(nine,"d9wg_d3d9_core_reset8")).replace(/\bWINAPI\b/g,""):""));const p=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",["-E","-P",file],{encoding:"utf8",maxBuffer:16<<20});assert.equal(p.status,0,p.stderr);return p.stdout;}
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
typedef int32_t HRESULT;typedef struct{UINT compatibility_api;}IDirect3DDevice9;
#define device_from_iface(p) (p)
static UINT device_compatibility_api(const IDirect3DDevice9*d){return d->compatibility_api;}
static UINT checks,cases,provider_calls,seed=0x8fe60001u,fail_provider,expect_null;static IDirect3DDevice9*expected_device;static PP8 expected_input;
static HRESULT provider(IDirect3DDevice9*d,PP9*p){provider_calls++;assert(d==expected_device);if(expect_null){assert(!p);return (HRESULT)0x8876086cu;}assert(p&&p->MultiSampleQuality==0);assert(!memcmp(p,&expected_input,20));assert(!memcmp((BYTE*)p+24,(BYTE*)&expected_input+20,32));memset(p,0xa5,sizeof(*p));return fail_provider?(HRESULT)0x8876086cu:0x12345678;}
${rename(extract(sdk9,"parameters8_to9"),"PP9")}
${rename(extract(sdk9,"d9wg_d3d9_core_reset8"),"PP9").replace(/\(iface\)->lpVtbl->Reset\s*\(iface,/g,"provider(iface,")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Backend8 Reset bridge line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
static UINT next(void){seed=seed*1664525u+1013904223u;return seed;}
int main(void){(void)&provider;IDirect3DDevice9 good={8},wrong={9};_Static_assert(sizeof(PP8)==52&&sizeof(PP9)==56,"SDK sizes");
for(UINT round=0;round<1024;round++)for(fail_provider=0;fail_provider<2;fail_provider++){
 struct Guard{DWORD pre[4];PP8 p;DWORD post[4];}input,saved;DWORD words[13];for(UINT i=0;i<13;i++)words[i]=next();memset(&input,0x5a,sizeof(input));memcpy(&input.p,words,52);saved=input;expected_input=input.p;expected_device=&good;UINT calls=provider_calls;
 CHECK(d9wg_d3d9_core_reset8(&good,&input.p)==(fail_provider?(HRESULT)0x8876086cu:0x12345678));CHECK(provider_calls==calls+1);CHECK(!memcmp(&input,&saved,sizeof(input)));cases++;
}
PP8 p;memset(&p,0x5a,sizeof(p));UINT calls=provider_calls;CHECK(d9wg_d3d9_core_reset8(NULL,&p)==(HRESULT)0x8876086cu&&provider_calls==calls);CHECK(d9wg_d3d9_core_reset8(&wrong,&p)==(HRESULT)0x8876086cu&&provider_calls==calls);expect_null=1;CHECK(d9wg_d3d9_core_reset8(&good,NULL)==(HRESULT)0x8876086cu&&provider_calls==calls+1);cases+=3;
printf("PASS actual backend8 Reset bridge: %u checks,%u SDK8/9 guarded forwarding/failure cases; exact52-to56-byte conversion,zero quality,immutable caller input,wrong-profile/null-device guards andNULL parameters forwarded for pending status\\n",checks,cases);return 0;}

`;
 fs.writeFileSync(dir+"/parameters.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/parameters.c","-o",dir+"/parameters"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/parameters",[],{encoding:"utf8",timeout:10000});assert.equal(r.status,0,r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
