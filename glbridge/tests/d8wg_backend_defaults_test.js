"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),nine=fs.readFileSync(process.env.D8WG_BACKEND_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-backend-"));
function extract(source,name){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end).replace(/__attribute__\s*\(\(__stdcall__\)\)/g,"");}
try{
 const old9=fs.readFileSync(root+"/tests/fixtures/d3d9_initial_states_before_backend8.c","utf8"),old8=fs.readFileSync(root+"/tests/fixtures/d3d8_initial_states_before_backend8.c","utf8");
 const fields=matrix=>`const struct Root*parent;UINT current_palette;struct{UINT a,b;}clip_status;BOOL clip_status_valid;DWORD render_states[256],texture_stage_states[8][33],material[17];float clip_planes[6][4];void*extra_lights;DWORD lights[8][26];BOOL light_set[8],light_enabled[8];${matrix};DWORD sampler_states[20][14];struct{UINT frequency;}streams[16];struct{BOOL EnableAutoDepthStencil;}present;`;
 const code=`#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include "runtime/d9wg_state_defaults.h"
typedef uint32_t UINT,DWORD;typedef int BOOL;typedef struct{float m[4][4];}D3DMATRIX;
#define TRUE 1
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory(p,q,n) memcpy(p,q,n)
#define D9_MAX_CLIP_PLANES 6u
#define D9_MAX_TEXTURE_STAGES 8u
#define D9_MAX_SAMPLERS 20u
#define D9_MAX_STREAMS 16u
#define D9_MAX_TRANSFORMS 512u
#define D8WG_MAX_CLIP_PLANES 6u
#define D8WG_MAX_TEXTURE_STAGES 8u
#define D8WG_MAX_TRANSFORMS 512u
struct Root{UINT compatibility_api;};
typedef struct{${fields("float transforms[512][16]")}}D9Device;
typedef struct{${fields("D3DMATRIX transforms[512]")}}D8Device;
typedef struct{float MaxPointSize;}D3DCAPS9,D3DCAPS8;
static float point_max;static UINT frees;
static void fill_caps(void*out){((D3DCAPS9*)out)->MaxPointSize=point_max;}
static void d9wg_clip_status_init(void*out){DWORD words[2]={0,0xfff};memcpy(out,words,8);}
static void d9wg_light_free(void**p){assert(*p==(void*)(uintptr_t)0x1234);*p=NULL;frees++;}
#define free_extra_lights d9wg_light_free
${extract(old8,"device_init_states").replace(/\bdevice_init_states\b/g,"init_before8")}
${extract(old9,"device_init_states").replace(/\bdevice_init_states\b/g,"init_before9")}
${extract(nine,"device_init_states_for_api")}
${extract(nine,"device_compatibility_api")}
${extract(nine,"device_init_states")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Backend8 defaults line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){(void)&device_compatibility_api;UINT checks=0;static D9Device actual,expected;static D8Device legacy;
_Static_assert(sizeof(D9Device)==sizeof(D8Device),"logical old8 and9 field strides");
for(UINT cfg=0;cfg<128;cfg++)for(UINT depth=0;depth<3;depth++)for(UINT api=8;api<=9;api++){
 point_max=cfg==0?0:cfg==1?1:cfg*8.f;memset(&actual,0xa5,sizeof(actual));struct Root root={api};actual.parent=&root;actual.present.EnableAutoDepthStencil=(BOOL)depth;actual.extra_lights=(void*)(uintptr_t)0x1234;expected=actual;memcpy(&legacy,&actual,sizeof(legacy));UINT old=frees;
 device_init_states_for_api(&actual,api);CHECK(frees==old+1&&!actual.extra_lights);
 if(api==9){init_before9(&expected);CHECK(!memcmp(&actual,&expected,sizeof(actual)));expected=actual;expected.extra_lights=(void*)(uintptr_t)0x1234;device_init_states(&expected);CHECK(!memcmp(&actual,&expected,sizeof(actual)));}
 else{init_before8(&legacy);CHECK(!memcmp(&actual,&legacy,offsetof(D9Device,sampler_states)));CHECK(actual.render_states[155]==0&&actual.material[0]==0x3f800000u&&actual.material[3]==0);expected=actual;expected.extra_lights=(void*)(uintptr_t)0x1234;device_init_states(&expected);CHECK(!memcmp(&actual,&expected,sizeof(actual)));}
 for(UINT slot=0;slot<20;slot++)for(UINT s=0;s<14;s++)CHECK(actual.sampler_states[slot][s]==d9wg_sampler_default(s));for(UINT slot=0;slot<16;slot++)CHECK(actual.streams[slot].frequency==1);
}
printf("PASS actual backend8 defaults: %u checks,768 initializer profiles; complete prior8 shared states/material/planes/lights/transforms,byte-identical prior9 state andReset wrapper,20 sampler/16 stream defaults\\n",checks);return 0;}
`;
 fs.writeFileSync(dir+"/defaults.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-I",root,dir+"/defaults.c","-o",dir+"/defaults"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/defaults",[],{encoding:"utf8",timeout:10000});assert.equal(r.status,0,r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
