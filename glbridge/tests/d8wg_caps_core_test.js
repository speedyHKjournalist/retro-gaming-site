// Root8 policy is production source; device_get_caps is the retired baseline.
// Actual new Device8 forwarding is checked against Root8 in the SDK XP probe.
"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),eight=fs.readFileSync(process.env.D8WG_CAPS_CORE_SOURCE||root+"/d3d8proxy/d3d8_proxy.c","utf8"),nine=fs.readFileSync(process.env.D8WG_CAPS_CORE_BACKEND_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-caps-core-"));
function extract(source,name){const m=new RegExp("^[^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
function macros(source,names){return names.map(n=>{const m=new RegExp("^#define\\s+"+n+"\\s+([^\\n]+)","m").exec(source);assert(m,n);return "#define "+n+" "+m[1];}).join("\n");}
function preprocess(api,source,body){const defines=macros(source,api===8?["D8WG_MAX_STREAMS","D8WG_MAX_TEXTURE_STAGES","D8WG_MAX_VS_CONSTANTS","D8WG_MAX_LIGHTS","D8WG_MAX_CLIP_PLANES","D3DPSHADECAPS_COLORFLATRGB","D3DPSHADECAPS_ALPHAFLATBLEND"]:["D9_MAX_STREAMS","D9_MAX_TEXTURE_STAGES","D9_MAX_VS_CONST_F","D9_PATCH_MAX_SEGMENTS","D9_MAX_RENDER_TARGETS","D9_MAX_CLIP_PLANES"]);const file=dir+"/sdk"+api+".c";fs.writeFileSync(file,"#define COBJMACROS\n#include <d3d"+api+".h>\n#include \""+root+"/d3d9proxy/d3d9_core.h\"\n#include \""+root+"/runtime/d9wg_present_pacing.h\"\n#include \""+root+"/runtime/d9wg_state_bridge8.h\"\n#define D8WG_TRACE_ERROR(v) (v)\n"+defines+"\n"+body.replace(/\bWINAPI\b/g,""));const p=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",["-E","-P",file],{encoding:"utf8",maxBuffer:16<<20});assert.equal(p.status,0,p.stderr);return p.stdout;}
try{
    const sdk8=preprocess(8,eight,[extract(eight,"fill_caps"),extract(eight,"d3d_get_device_caps"),extract(fs.readFileSync(root+"/tests/fixtures/d3d8_legacy_proxy.c","utf8"),"device_get_caps"),fs.readFileSync(root+"/tests/fixtures/d3d8_caps_before_core.c","utf8").replace(/\bfill_caps\b/g,"caps8_before")].join("\n"));
    const sdk9=preprocess(9,nine,["apply_permissive_caps","reported_user_clip_planes","fill_caps_for_profile","fill_caps","d9wg_d3d9_core_caps8"].map(n=>extract(nine,n)).join("\n")+"\n"+fs.readFileSync(root+"/tests/fixtures/d3d9_caps_before_core.c","utf8").replace(/\bfill_caps\b/g,"caps9_before"));
    function type(source,name){const m=new RegExp("typedef struct _"+name+"\\s*\\{[\\s\\S]*?\\}\\s*"+name+"\\s*;").exec(source);assert(m,name);return m[0];}
    const structs=type(sdk8,"D3DCAPS8")+"\n"+["D3DVSHADERCAPS2_0","D3DPSHADERCAPS2_0","D3DCAPS9"].map(n=>type(sdk9,n)).join("\n");
    const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint32_t DWORD,UINT;typedef int32_t INT,HRESULT;typedef int D3DDEVTYPE,BOOL;typedef struct{int unused;}IDirect3D8,IDirect3DDevice8;
#define TRUE 1
#define FALSE 0
#define D3DDEVTYPE_HAL 1
${structs}
static UINT profile,permissive,injection,backend_calls,checks;
static UINT shader_model_version(void){return profile;}
static BOOL env_flag_enabled(const char*name){assert(!strcmp(name,"D9WG_CAPS_PERMISSIVE"));return permissive;}
${extract(sdk9,"apply_permissive_caps")}
${extract(sdk9,"reported_user_clip_planes")}
${extract(sdk9,"fill_caps_for_profile").replace(/\bfill_caps_for_profile\b/g,"fill_caps_for_profile_actual")}
/* The observed provider executes the real shared table then injects valid
 * alternative common limits. This verifies propagation through the ABI and
 * distinguishes delegation from a frozen copy of the old D3D8 constants. */
static void fill_caps_for_profile(D3DCAPS9*caps,UINT shader_model,BOOL allow_permissive){backend_calls++;fill_caps_for_profile_actual(caps,shader_model,allow_permissive);
 if(injection==1){caps->MaxTextureWidth=1024;caps->MaxTextureHeight=512;caps->MaxPointSize=64.0f;}
 if(injection==2){caps->MaxStreams=32;caps->MaxTextureBlendStages=16;caps->MaxSimultaneousTextures=16;caps->MaxActiveLights=16;caps->MaxUserClipPlanes=12;caps->MaxVertexBlendMatrices=8;caps->MaxVertexBlendMatrixIndex=511;}
 if(injection==3)caps->FVFCaps=7|0x00100000u;
 if(injection==4)caps->FVFCaps=32|0x00100000u;
 if(injection==5)caps->MaxAnisotropy=1;
 if(injection==6){caps->CubeTextureFilterCaps=0;caps->VolumeTextureFilterCaps=0;}
}
${extract(sdk9,"fill_caps")}
${extract(sdk9,"d9wg_d3d9_core_caps8")}
${extract(sdk9,"caps9_before")}
${extract(sdk8,"fill_caps").replace(/\bfill_caps\b/g,"caps8_current")}
${extract(sdk8,"d3d_get_device_caps").replace(/\bfill_caps\b/g,"caps8_current")}
${extract(sdk8,"device_get_caps").replace(/\bfill_caps\b/g,"caps8_current")}
${extract(sdk8,"caps8_before")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"D3D8 caps core line %u: %s profile=%u permissive=%u injection=%u\\n",__LINE__,#v,profile,permissive,injection);return 1;}checks++;}while(0)
int main(void){
 (void)&fill_caps_for_profile;(void)&fill_caps_for_profile_actual;(void)&d9wg_d3d9_core_caps8;
 _Static_assert(sizeof(D3DCAPS8)==212,"SDK8 prefix size");_Static_assert(__builtin_offsetof(D3DCAPS9,DevCaps2)==212,"SDK9 prefix extent");
 ${process.env.D8WG_CAPS_PRINT_REFERENCE === "1" ? 'D3DCAPS8 reference;caps8_before(&reference);DWORD words_ref[53];memcpy(words_ref,&reference,sizeof(reference));printf("CAPS8_REFERENCE [");for(UINT i=0;i<53;i++)printf("%s%u",i?",":"",words_ref[i]);puts("]");' : ""}
 const UINT profiles[]={0,2,3};UINT cases=0,words=0;
 for(UINT round=0;round<32;round++)for(UINT p=0;p<3;p++)for(permissive=0;permissive<2;permissive++){
  profile=profiles[p];injection=0;D3DCAPS9 before9,current9;caps9_before(&before9);before9.TextureCaps |= 0x80u;fill_caps(&current9);CHECK(!memcmp(&before9,&current9,sizeof(before9)));
  D3DCAPS8 before8;caps8_before(&before8);
  /* Stage 6 Wine texop_test validates the four existing host modulate/add
   * operations that the old API8 mask omitted. Keep the retired fixture
   * unchanged and make the intentional capability increase explicit. */
  before8.TextureOpCaps |= 0x001e0000u;
  UINT saved_profile=profile,saved_permissive=permissive;D3DCAPS9 raw_expected;profile=0;permissive=0;caps9_before(&raw_expected);raw_expected.TextureCaps |= 0x80u;profile=saved_profile;permissive=saved_permissive;
  if(raw_expected.RasterCaps&0x04000000u)raw_expected.RasterCaps|=0x00004000u;
  struct{uint32_t before[4];D3DCAPS8 caps;uint32_t after[32];}raw;memset(&raw,0xa5,sizeof(raw));UINT raw_calls=backend_calls;d9wg_d3d9_core_caps8(&raw.caps);
  CHECK(backend_calls==raw_calls+1&&!memcmp(&raw.caps,&raw_expected,212));for(UINT i=0;i<4;i++)CHECK(raw.before[i]==0xa5a5a5a5u);for(UINT i=0;i<32;i++)CHECK(raw.after[i]==0xa5a5a5a5u);

  for(injection=0;injection<7;injection++){
   struct{uint32_t before[4];D3DCAPS8 caps;uint32_t after[32];}guard;memset(&guard,0xa5,sizeof(guard));UINT calls=backend_calls;
   CHECK(!d3d_get_device_caps(NULL,0,D3DDEVTYPE_HAL,&guard.caps)&&backend_calls==calls+1);
   for(UINT i=0;i<4;i++)CHECK(guard.before[i]==0xa5a5a5a5u);for(UINT i=0;i<32;i++)CHECK(guard.after[i]==0xa5a5a5a5u);
   D3DCAPS8 from_device;CHECK(!device_get_caps(NULL,&from_device)&&!memcmp(&from_device,&guard.caps,sizeof(from_device)));
   CHECK(guard.caps.VertexShaderVersion==0xfffe0101u&&guard.caps.PixelShaderVersion==0xffff0104u&&guard.caps.MaxVertexShaderConst==96&&guard.caps.MaxPixelShaderValue==8.0f);
   CHECK(!(guard.caps.DevCaps&(0x00100000u|0x00800000u|0x00400000u|0x01000000u)));
   if(injection==0){if(memcmp(&before8,&guard.caps,sizeof(before8))){for(UINT i=0;i<53;i++)if(((DWORD*)&before8)[i]!=((DWORD*)&guard.caps)[i])fprintf(stderr,"CAPS WORD %u old=%08x new=%08x\\n",i,((DWORD*)&before8)[i],((DWORD*)&guard.caps)[i]);}CHECK(!memcmp(&before8,&guard.caps,sizeof(before8)));words+=53;}
   if(injection==1)CHECK(guard.caps.MaxTextureWidth==1024&&guard.caps.MaxTextureHeight==512&&guard.caps.MaxPointSize==64.0f);
   if(injection==2)CHECK(guard.caps.MaxStreams==16&&guard.caps.MaxTextureBlendStages==8&&guard.caps.MaxSimultaneousTextures==8&&guard.caps.MaxActiveLights==8&&guard.caps.MaxUserClipPlanes==6&&guard.caps.MaxVertexBlendMatrices==4&&guard.caps.MaxVertexBlendMatrixIndex==255);
   if(injection==3)CHECK((guard.caps.FVFCaps&0xffffu)==7);
   if(injection==4)CHECK((guard.caps.FVFCaps&0xffffu)==8);
   if(injection==5)CHECK(!(guard.caps.RasterCaps&0x00020000u));
   if(injection==6)CHECK(!guard.caps.CubeTextureFilterCaps&&!guard.caps.VolumeTextureFilterCaps);
   cases++;
  }
 }
 injection=0;UINT calls=backend_calls;D3DCAPS8 caps;memset(&caps,0xa5,sizeof(caps));CHECK(d3d_get_device_caps(NULL,1,1,&caps)==(HRESULT)0x8876086cu&&backend_calls==calls);CHECK(d3d_get_device_caps(NULL,0,2,&caps)==(HRESULT)0x8876086cu&&backend_calls==calls);CHECK(d3d_get_device_caps(NULL,0,1,NULL)==(HRESULT)0x8876086cu&&device_get_caps(NULL,NULL)==(HRESULT)0x8876086cu&&backend_calls==calls);
 printf("PASS actual D3D8 caps/core C: %u checks, %u profile/provider cases, %u D3D8 baseline words plus four stage6 texops; all53 prefix fields, full D3D9 profiles with native alpha-palette support, precise212-byte copy, backend propagation, version masks/limits and API guards\\n",checks,cases,words);return 0;
}
`;
    fs.writeFileSync(dir+"/caps.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/caps.c","-o",dir+"/caps"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/caps",[],{encoding:"utf8",timeout:10000});assert.equal(r.status,0,r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
