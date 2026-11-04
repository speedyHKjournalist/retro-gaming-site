"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),eight=fs.readFileSync(process.env.D8WG_MODES_SOURCE||root+"/d3d8proxy/d3d8_proxy.c","utf8"),nine=fs.readFileSync(process.env.D8WG_MODES_BACKEND_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-modes-core-"));
function extract(source,name){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end).replace(/__attribute__\s*\(\(__stdcall__\)\)/g,"");}
function preprocess(api,body){const file=dir+"/sdk"+api+".c";fs.writeFileSync(file,`#define COBJMACROS\n#include <d3d${api}.h>\n#define TRACE(...)\n#define TRACE_REFUSE(v) (v)\n#define D8WG_TRACE_ERROR(v) (v)\n`+body.replace(/\bWINAPI\b/g,""));const p=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",["-E","-P",file],{encoding:"utf8",maxBuffer:16<<20});assert.equal(p.status,0,p.stderr);return p.stdout;}
try{
 const names=["d3d_get_adapter_mode_count","d3d_enum_adapter_modes"],sdk8=preprocess(8,names.map(n=>extract(eight,n)).join("\n")+fs.readFileSync(root+"/tests/fixtures/d3d8_modes_before_core.c","utf8")),sdk9=preprocess(9,["fill_display_mode","enumerate_adapter_modes8","enumerate_adapter_modes",...names,"d9wg_d3d9_core_adapter_mode_count8","d9wg_d3d9_core_enum_adapter_modes8"].map(n=>extract(nine,n)).join("\n")+fs.readFileSync(root+"/tests/fixtures/d3d9_modes_before_core.c","utf8"));
 const type=sdk9.match(/typedef struct _D3DDISPLAYMODE\s*\{[\s\S]*?\}\s*D3DDISPLAYMODE\s*;/)[0];
 const observed=s=>s.replace(/\(factory\)->lpVtbl->GetAdapterModeCount\s*\(factory,/g,"provider_count(factory,").replace(/\(factory\)->lpVtbl->EnumAdapterModes\s*\(factory,/g,"provider_enum(factory,");
 const code=`#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t UINT,DWORD;typedef int32_t HRESULT;typedef int BOOL;typedef unsigned char BYTE;
${sdk9.match(/typedef enum _D3DFORMAT\s*\{[\s\S]*?\}\s*D3DFORMAT\s*;/)[0]}
${type}
typedef struct{UINT compatibility_api;}IDirect3D9;typedef struct{IDirect3D9*backend;}IDirect3D8;
#define d3d_from_iface(p) (p)
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory(p,q,n) memcpy(p,q,n)
typedef struct{UINT dmSize,dmPelsWidth,dmPelsHeight,dmBitsPerPel,dmDisplayFrequency,reserved[4];}DEVMODEA;
static DEVMODEA driver[128];static UINT driver_count,win32_calls,count_calls,enum_calls,checks,entries,nine_entries;
static BOOL EnumDisplaySettingsA(const char*name,DWORD index,DEVMODEA*out){assert(!name&&out->dmSize==sizeof(*out));for(UINT i=0;i<4;i++)assert(!out->reserved[i]);win32_calls++;if(index>=driver_count)return 0;*out=driver[index];return 1;}
${["fill_display_mode","enumerate_adapter_modes8","enumerate_adapter_modes"].map(n=>extract(sdk9,n)).join("\n")}
${names.map(n=>extract(sdk9,n).replace(new RegExp("\\b"+n+"\\b","g"),n+"_actual9")).join("\n")}
static UINT provider_count(IDirect3D9*r,UINT a,D3DFORMAT f){count_calls++;return d3d_get_adapter_mode_count_actual9(r,a,f);}
static HRESULT provider_enum(IDirect3D9*r,UINT a,D3DFORMAT f,UINT i,D3DDISPLAYMODE*m){enum_calls++;return d3d_enum_adapter_modes_actual9(r,a,f,i,m);}
${["d9wg_d3d9_core_adapter_mode_count8","d9wg_d3d9_core_enum_adapter_modes8"].map(n=>observed(extract(sdk9,n))).join("\n")}
${names.map(n=>extract(sdk8,n)).join("\n")}
${names.map(n=>extract(sdk8,n+"_before8")).join("\n")}
${["enumerate_adapter_modes_before9",...names.map(n=>n+"_before9")].map(n=>extract(sdk9,n)).join("\n")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"D3D8 modes core line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){(void)&provider_count;(void)&provider_enum;(void)&d9wg_d3d9_core_adapter_mode_count8;(void)&d9wg_d3d9_core_enum_adapter_modes8;
 _Static_assert(sizeof(D3DDISPLAYMODE)==16,"shared SDK mode");IDirect3D9 b8={8},b9={9};IDirect3D8 r8={&b8};const D3DFORMAT formats[]={0,21,22,23,24,99};
 for(UINT round=0;round<64;round++)for(UINT profile=0;profile<4;profile++){
  memset(driver,0,sizeof(driver));driver_count=profile==0?0:profile==1?1:profile==2?9:127;
  for(UINT j=0;j<driver_count;j++)driver[j]=(DEVMODEA){.dmPelsWidth=640+16*j,.dmPelsHeight=480+8*j,.dmBitsPerPel=j%3==0?16u:32u,.dmDisplayFrequency=j%5?60+j:1};
  if(profile==2){driver[2].dmPelsWidth=0;driver[3].dmPelsHeight=0;driver[4].dmBitsPerPel=24;driver[5].dmDisplayFrequency=0;driver[6]=driver[1];}
  UINT c=count_calls,w=win32_calls;CHECK(d3d_get_adapter_mode_count(&r8,0)==d3d_get_adapter_mode_count_before8(&r8,0)&&count_calls==c+1&&win32_calls==w);
  for(UINT i=0;i<12;i++){
   struct{DWORD before[4];D3DDISPLAYMODE value;DWORD after[4];}g;memset(&g,0xa5,sizeof(g));D3DDISPLAYMODE old=g.value;UINT e=enum_calls;HRESULT expected=d3d_enum_adapter_modes_before8(&r8,0,i,&old);CHECK(d3d_enum_adapter_modes(&r8,0,i,&g.value)==expected&&enum_calls==e+1&&win32_calls==w);CHECK(!memcmp(&old,&g.value,16));for(UINT j=0;j<4;j++)CHECK(g.before[j]==0xa5a5a5a5u&&g.after[j]==0xa5a5a5a5u);if(!expected)entries++;
  }
  for(UINT f=0;f<6;f++){
   UINT old_count=d3d_get_adapter_mode_count_before9(&b9,0,formats[f]);CHECK(d3d_get_adapter_mode_count_actual9(&b9,0,formats[f])==old_count);
   for(UINT i=0;i<old_count+3;i++){struct{DWORD before;D3DDISPLAYMODE value;DWORD after;}g;memset(&g,0xa5,sizeof(g));D3DDISPLAYMODE old=g.value;HRESULT expected=d3d_enum_adapter_modes_before9(&b9,0,formats[f],i,&old);CHECK(d3d_enum_adapter_modes_actual9(&b9,0,formats[f],i,&g.value)==expected&&!memcmp(&old,&g.value,16)&&g.before==0xa5a5a5a5u&&g.after==0xa5a5a5a5u);if(!expected)nine_entries++;}
   D3DDISPLAYMODE mode,old;memset(&mode,0xa5,16);old=mode;CHECK(d3d_get_adapter_mode_count_actual9(&b9,1,formats[f])==0);CHECK(d3d_enum_adapter_modes_actual9(&b9,1,formats[f],0,&mode)==(HRESULT)0x8876086cu&&!memcmp(&mode,&old,16));CHECK(d3d_enum_adapter_modes_actual9(&b9,0,formats[f],UINT32_MAX,&mode)==(HRESULT)0x8876086cu&&!memcmp(&mode,&old,16));CHECK(d3d_enum_adapter_modes_actual9(&b9,0,formats[f],0,NULL)==(HRESULT)0x8876086cu);
  }
  D3DDISPLAYMODE mode,old;memset(&mode,0xa5,16);old=mode;const UINT adapters[]={1,UINT32_MAX};for(UINT a=0;a<2;a++){c=count_calls;UINT e=enum_calls;CHECK(!d3d_get_adapter_mode_count(&r8,adapters[a])&&count_calls==c+1);CHECK(d3d_enum_adapter_modes(&r8,adapters[a],0,&mode)==(HRESULT)0x8876086cu&&enum_calls==e+1&&!memcmp(&mode,&old,16));}
  UINT e=enum_calls;CHECK(d3d_enum_adapter_modes(&r8,0,UINT32_MAX,&mode)==(HRESULT)0x8876086cu&&enum_calls==e+1&&!memcmp(&mode,&old,16));e=enum_calls;CHECK(d3d_enum_adapter_modes(&r8,0,0,NULL)==(HRESULT)0x8876086cu&&enum_calls==e);
 }
 printf("PASS actual D3D8 modes/core C: %u checks,256 driver/profile rounds,%u prior D3D8 entries,%u prior D3D9 entries; legacy ordering/16-bit formats,ordinary9 live filters/refresh/duplicates/end-of-list,observed delegation,16-byte guards and unchanged invalid outputs\\n",checks,entries,nine_entries);return 0;
}
`;
 fs.writeFileSync(dir+"/modes.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/modes.c","-o",dir+"/modes"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/modes",[],{encoding:"utf8",timeout:10000});assert.equal(r.status,0,r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
