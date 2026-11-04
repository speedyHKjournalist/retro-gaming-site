"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),eight=fs.readFileSync(process.env.D8WG_ADAPTER_SOURCE||root+"/d3d8proxy/d3d8_proxy.c","utf8"),nine=fs.readFileSync(process.env.D8WG_ADAPTER_BACKEND_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-adapter-core-"));
function extract(source,name){const m=new RegExp("^[^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end).replace(/__attribute__\s*\(\(__stdcall__\)\)/g,"");}
function preprocess(api,body,identity){const defines=["D9_ADAPTER_NATIVE","D9_ADAPTER_GEFORCE4_MX","D9_ADAPTER_VMWARE_SVGA","D9_ADAPTER_GEFORCEFX_5200"].map(n=>nine.match(new RegExp("^#define\\s+"+n+"\\s+[^\\n]+","m"))[0]).join("\n");const file=dir+"/sdk"+api+".c";fs.writeFileSync(file,`#define COBJMACROS\n#include <d3d${api}.h>\n#include "${root}/d3d9proxy/d3d9_core.h"\n#define TRACE(...)\n#define TRACE_REFUSE(v) (v)\n#define D8WG_TRACE_ERROR(v) (v)\n${defines}\n#define D9_ADAPTER_IDENTITY ${identity}\n`+body.replace(/\bWINAPI\b/g,""));const p=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",["-E","-P",file],{encoding:"utf8",maxBuffer:16<<20});assert.equal(p.status,0,p.stderr);return p.stdout;}
try{
let total=0;
for(let identity=0;identity<4;identity++){
 const names=["d3d_get_adapter_identifier","d3d_get_adapter_display_mode","d3d_register_software_device"];
 const sdk8=preprocess(8,names.map(n=>extract(eight,n)).join("\n")+fs.readFileSync(root+"/tests/fixtures/d3d8_adapter_before_core.c","utf8"),identity);
 const sdk9=preprocess(9,["fill_adapter_identifier8",...names,"d9wg_d3d9_core_register_software","d9wg_d3d9_core_adapter_identifier8","d9wg_d3d9_core_adapter_display_mode"].map(n=>extract(nine,n)).join("\n")+fs.readFileSync(root+"/tests/fixtures/d3d9_adapter_before_core.c","utf8"),identity);
 function type(source,name){const m=new RegExp("typedef struct _"+name+"\\s*\\{[\\s\\S]*?\\}\\s*"+name+"\\s*;").exec(source);assert(m,name);return m[0];}
 const observed=body=>body.replace(/\(factory\)->lpVtbl->GetAdapterIdentifier\s*\(factory,/g,"provider_identifier(factory,").replace(/\(factory\)->lpVtbl->GetAdapterDisplayMode\s*\(factory,/g,"provider_mode(factory,").replace(/\(factory\)->lpVtbl->RegisterSoftwareDevice\s*\(factory,/g,"provider_software(factory,");
 const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
typedef uint32_t DWORD,UINT;typedef int32_t INT,LONG,HRESULT;typedef uint16_t WORD;typedef uint8_t BYTE;typedef int D3DFORMAT;
typedef struct{DWORD Data1;WORD Data2,Data3;BYTE Data4[8];}GUID;
typedef union{struct{DWORD LowPart;LONG HighPart;};int64_t QuadPart;}LARGE_INTEGER;
#pragma pack(push,4)
${type(sdk8,"D3DADAPTER_IDENTIFIER8")}
${type(sdk9,"D3DADAPTER_IDENTIFIER9")}
${type(sdk9,"D3DDISPLAYMODE")}
#pragma pack(pop)
typedef struct{UINT compatibility_api;}IDirect3D9;typedef struct{IDirect3D9*backend;}IDirect3D8;
#define d3d_from_iface(v) (v)
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory(p,q,n) memcpy(p,q,n)
static void lstrcpynA(char*out,const char*in,UINT n){if(n){UINT len=(UINT)strlen(in);if(len>=n)len=n-1;memcpy(out,in,len);out[len]=0;}}
typedef struct{UINT width,height,format,refresh_rate;}D9WGDisplayMode;
static UINT checks,id_calls,mode_calls,software_calls,current_calls;static void*observed_software;static D9WGDisplayMode current;
static void d9wg_display_mode_current(D9WGDisplayMode*out){current_calls++;*out=current;}
static void fill_display_mode(D3DDISPLAYMODE*m,UINT w,UINT h,D3DFORMAT f){m->Width=w;m->Height=h;m->Format=f;m->RefreshRate=60;}
${extract(sdk9,"fill_adapter_identifier8")}
${names.map(n=>extract(sdk9,n).replace(new RegExp("\\b"+n+"\\b","g"),n+"_actual9")).join("\n")}
static HRESULT provider_identifier(IDirect3D9*r,UINT a,DWORD f,D3DADAPTER_IDENTIFIER9*i){id_calls++;return d3d_get_adapter_identifier_actual9(r,a,f,i);}
static HRESULT provider_mode(IDirect3D9*r,UINT a,D3DDISPLAYMODE*m){mode_calls++;return d3d_get_adapter_display_mode_actual9(r,a,m);}
static HRESULT provider_software(IDirect3D9*r,void*p){software_calls++;observed_software=p;return d3d_register_software_device_actual9(r,p);}
${["d9wg_d3d9_core_register_software","d9wg_d3d9_core_adapter_identifier8","d9wg_d3d9_core_adapter_display_mode"].map(n=>observed(extract(sdk9,n))).join("\n")}
${names.map(n=>extract(sdk8,n)).join("\n")}
${[8,9].map(api=>names.map(n=>extract(api===8?sdk8:sdk9,n+"_before"+api)).join("\n")).join("\n")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"D3D8 adapter core line %u: %s identity=${identity}\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){
 (void)&d9wg_d3d9_core_register_software;(void)&provider_identifier;(void)&provider_mode;(void)&provider_software;(void)&d9wg_d3d9_core_adapter_identifier8;(void)&d9wg_d3d9_core_adapter_display_mode;
 _Static_assert(sizeof(D3DADAPTER_IDENTIFIER8)==1068,"SDK8 size");_Static_assert(sizeof(D3DADAPTER_IDENTIFIER9)==1100,"SDK9 size");_Static_assert(offsetof(D3DADAPTER_IDENTIFIER8,DriverVersion)==1024,"SDK8 version offset");_Static_assert(offsetof(D3DADAPTER_IDENTIFIER9,DriverVersion)==1056,"SDK9 version offset");
 IDirect3D9 b8={8},b9={9};IDirect3D8 r8={&b8};const DWORD flags[]={0,2,0xffffffffu,0x80000000u};
 for(UINT round=0;round<64;round++)for(UINT f=0;f<4;f++){
  struct{DWORD before[4];D3DADAPTER_IDENTIFIER8 value;DWORD after[32];}g;D3DADAPTER_IDENTIFIER8 old;memset(&g,0xa5,sizeof(g));memset(&old,0xa5,sizeof(old));UINT n=id_calls;
  CHECK(!d3d_get_adapter_identifier(&r8,0,flags[f],&g.value)&&id_calls==n+1);CHECK(!d3d_get_adapter_identifier_before8(&r8,0,flags[f],&old)&&!memcmp(&old,&g.value,sizeof(old)));
  for(UINT i=0;i<4;i++)CHECK(g.before[i]==0xa5a5a5a5u);for(UINT i=0;i<32;i++)CHECK(g.after[i]==0xa5a5a5a5u);
  CHECK(g.value.WHQLLevel==((flags[f]&2)?0u:1u));
  D3DADAPTER_IDENTIFIER9 actual9,old9;CHECK(!d3d_get_adapter_identifier_actual9(&b9,0,flags[f],&actual9));CHECK(!d3d_get_adapter_identifier_before9(&b9,0,flags[f],&old9)&&!memcmp(&actual9,&old9,sizeof(old9)));CHECK(actual9.WHQLLevel==1);
  current=(D9WGDisplayMode){round%2?640u:1920u,round%2?480u:1080u,round%2?23u:22u,round%3?60u:85u};D3DDISPLAYMODE old_mode,new_mode;UINT m=mode_calls,c=current_calls;CHECK(!d3d_get_adapter_display_mode(&r8,0,&new_mode)&&mode_calls==m+1&&current_calls==c+1);CHECK(!d3d_get_adapter_display_mode_before8(&r8,0,&old_mode)&&!memcmp(&old_mode,&new_mode,16));CHECK(!d3d_get_adapter_display_mode_actual9(&b9,0,&new_mode)&&!d3d_get_adapter_display_mode_before9(&b9,0,&old_mode)&&!memcmp(&old_mode,&new_mode,16));
  void*callback=(void*)(uintptr_t)(round%2?1u:0u);UINT s=software_calls;CHECK(d3d_register_software_device(&r8,callback)==(HRESULT)0x8876086cu&&software_calls==s+1&&observed_software==callback);CHECK(d3d_register_software_device_before8(&r8,callback)==(HRESULT)0x8876086cu&&d3d_register_software_device_before9(&b9,callback)==(HRESULT)0x8876086cu);
 }
 struct{DWORD before[4];D3DADAPTER_IDENTIFIER8 value;DWORD after[32];}g,unchanged;memset(&g,0xa5,sizeof(g));unchanged=g;UINT n=id_calls;CHECK(d3d_get_adapter_identifier(&r8,1,0,&g.value)==(HRESULT)0x8876086cu&&id_calls==n+1&&!memcmp(&g,&unchanged,sizeof(g)));n=id_calls;CHECK(d3d_get_adapter_identifier(&r8,0,0,NULL)==(HRESULT)0x8876086cu&&id_calls==n);
 CHECK(d9wg_d3d9_core_adapter_identifier8(&b8,0,0,&g.value,1064)==(HRESULT)0x8876086cu&&d9wg_d3d9_core_adapter_identifier8(&b8,0,0,&g.value,1100)==(HRESULT)0x8876086cu&&id_calls==n&&!memcmp(&g,&unchanged,sizeof(g)));
 D3DDISPLAYMODE mode,old;memset(&mode,0xa5,sizeof(mode));old=mode;UINT m=mode_calls,c=current_calls;CHECK(d3d_get_adapter_display_mode(&r8,0xffffffffu,&mode)==(HRESULT)0x8876086cu&&mode_calls==m+1&&current_calls==c&&!memcmp(&mode,&old,16));m=mode_calls;CHECK(d3d_get_adapter_display_mode(&r8,0,NULL)==(HRESULT)0x8876086cu&&mode_calls==m);
 printf("ADAPTER checks=%u\\n",checks);return 0;
}
`;
 fs.writeFileSync(dir+"/adapter.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/adapter.c","-o",dir+"/adapter"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/adapter",[],{encoding:"utf8",timeout:10000});assert.equal(r.status,0,r.stderr+" signal="+r.signal);total+=Number(r.stdout.match(/checks=(\d+)/)[1]);
}
console.log(`PASS actual D3D8 adapter/core C: ${total} checks, 4 D3D9 identity configurations, 1024 legacy identity/display/software rounds; exact1068/1100-byte prior outputs, WHQL polarity, guarded copies, observed delegation and untouched error outputs`);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
