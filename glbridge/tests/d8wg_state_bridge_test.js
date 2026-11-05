"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"state-bridge8-")),source8=readComparisonSource(process.env.D8WG_STATE_SOURCE8||root+"/d3d8proxy/d3d8_proxy.c","utf8"),source9=readComparisonSource(process.env.D8WG_STATE_SOURCE9||root+"/d3d9proxy/d3d9_proxy.c","utf8"),header=process.env.D8WG_STATE_BRIDGE_HEADER||root+"/runtime/d9wg_state_bridge8.h";
function extract(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return s.slice(m.index,b);}
function family(s,names,prefix){let code=names.map(n=>extract(s,n)).join("\n");for(const n of names)code=code.replace(new RegExp("\\b"+n+"\\b","g"),prefix+n);return code;}
function sdk(api){const r=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",["-E","-P","-x","c","-"],{input:"#include <d3d"+api+".h>\n",encoding:"utf8",maxBuffer:16<<20});assert.equal(r.status,0,r.stderr);return r.stdout;}
try{
 const sdk8=sdk(8),sdk9=sdk(9);for(const[name,id]of Object.entries({D3DRS_LINEPATTERN:10,D3DRS_ZVISIBLE:30,D3DRS_EDGEANTIALIAS:40,D3DRS_ZBIAS:47,D3DRS_SOFTWAREVERTEXPROCESSING:153,D3DRS_PATCHSEGMENTS:164,D3DRS_POSITIONORDER:172,D3DRS_NORMALORDER:173,D3DTSS_ADDRESSU:13,D3DTSS_ADDRESSV:14,D3DTSS_ADDRESSW:25,D3DTSS_BORDERCOLOR:15,D3DTSS_MAGFILTER:16,D3DTSS_MINFILTER:17,D3DTSS_MIPFILTER:18,D3DTSS_MIPMAPLODBIAS:19,D3DTSS_MAXMIPLEVEL:20,D3DTSS_MAXANISOTROPY:21}))assert.equal(Number(new RegExp("\\b"+name+"\\s*=\\s*(\\d+)").exec(sdk8)?.[1]),id,name);
 for(const[name,id]of Object.entries({D3DRS_DEPTHBIAS:195,D3DSAMP_ADDRESSU:1,D3DSAMP_ADDRESSV:2,D3DSAMP_ADDRESSW:3,D3DSAMP_BORDERCOLOR:4,D3DSAMP_MAGFILTER:5,D3DSAMP_MINFILTER:6,D3DSAMP_MIPFILTER:7,D3DSAMP_MIPMAPLODBIAS:8,D3DSAMP_MAXMIPLEVEL:9,D3DSAMP_MAXANISOTROPY:10}))assert.equal(Number(new RegExp("\\b"+name+"\\s*=\\s*(\\d+)").exec(sdk9)?.[1]),id,name);
 const names8=['emit_render_state','emit_texture_stage_state','device_set_render_state','device_get_render_state','device_set_texture_stage_state','device_get_texture_stage_state'],names9=['device_set_render_state','device_set_texture_stage_state','device_set_sampler_state'];
 const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "${root}/d3d9proxy/d3d9_protocol.h"
#include "${header}"
typedef uint32_t UINT,DWORD,D3DRENDERSTATETYPE,D3DTEXTURESTAGESTATETYPE,D3DSAMPLERSTATETYPE;typedef int32_t BOOL,HRESULT;
#define WINAPI
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define TRUE 1
#define FALSE 0
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE_REFUSE(v) (v)
#define HOSTLOG_REFUSED(...) ((void)0)
#define CopyMemory(p,q,n) memcpy(p,q,n)
#define D8WG_MAX_RENDER_STATES D9WG_RENDER_STATES8
#define D8WG_MAX_TEXTURE_STAGES D9WG_TEXTURE_STAGES8
#define D8WG_MAX_TEXTURE_STAGE_STATES D9WG_TEXTURE_STAGE_STATES8
#define D9_MAX_RENDER_STATES 256u
#define D9_MAX_TEXTURE_STAGES 8u
#define D9_MAX_TEXTURE_STAGE_STATES 33u
#define D9_MAX_SAMPLER_STATES 14u
#define D3DVERTEXTEXTURESAMPLER0 257u
#define D3DRS_ENABLEADAPTIVETESSELLATION 184u
#define D3DRS_ADAPTIVETESS_X 180u
#define D9FOURCC_NVDB 0x4244564eu
#define D3DRS_POINTSIZE 154u
typedef struct {uint8_t render_mask[256],texture_stage_mask[8][32];DWORD render_states[256],texture_stage_states[8][32];} D8StateSnapshot;
typedef struct {D8StateSnapshot state;} Block8;
typedef struct {BOOL has_render_state[256],has_texture_stage_state[8][33],has_sampler_state[20][14];DWORD render_states[256],texture_stage_states[8][33],sampler_states[20][14];} D9StateBlock;
typedef struct Device{UINT api,handle;DWORD render_states[256],texture_stage_states[8][33],sampler_states[20][14];Block8*recording_state_block;D9StateBlock*recording_result;} Device,D8Device,D9Device,IDirect3DDevice8,IDirect3DDevice9;
static Device*device_from_iface(Device*d){return d;}static UINT device_compatibility_api(Device*d){return d->api;}
typedef struct{UINT count,op;DWORD bytes[4];} Capture;static Capture captured;static BOOL fail;
static BOOL emit_command(UINT op,const void*p,UINT n){assert(n==16);captured.count++;captured.op=op;memcpy(captured.bytes,p,16);return !fail;}
${['sampler_state_slot','state_block_record_render_state','state_block_record_texture_stage_state','state_block_record_sampler_state'].map(n=>extract(source9,n)).join('\n')}
${family(readComparisonSource(root+'/tests/fixtures/d3d8_states_before_core.c','utf8'),['d3d8_stage_state_to_sampler_state',...names8],'old8_')}
${family(source8,names8,'now8_')}
${family(readComparisonSource(root+'/tests/fixtures/d3d9_states_before_core.c','utf8'),names9,'old9_')}
${family(source9,names9,'now9_')}
#define IDirect3DDevice9_SetRenderState now9_device_set_render_state
#define IDirect3DDevice9_SetTextureStageState now9_device_set_texture_stage_state
#define IDirect3DDevice9_SetSamplerState now9_device_set_sampler_state
${['d9wg_d3d9_core_set_render_state8','d9wg_d3d9_core_set_texture_stage_state8'].map(n=>extract(source9,n)).join('\n')}
static UINT cases,checks,emitter_cases,frontend_cases,backend_cases,ordinary_cases;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"State bridge8 case %u line %u: %s\\n",cases,__LINE__,#v);return 1;}checks++;}while(0)
static void reset_capture(void){memset(&captured,0,sizeof(captured));}
int main(void){static Device a,b;static Block8 r8a,r8b;static D9StateBlock r9a,r9b;DWORD values[160]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,31,32,33,255,256,65535,65536,0xffffffffu,0x80000000u,0x7fc01234u,0x3f800000u,0x7f800000u,0xff800000u};UINT seed=0x12345678u;for(UINT i=32;i<160;i++){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;values[i]=seed;}
/* Exact public8 emitter bytes, including negative zero, all holes, seven
 * deletions and the ten sampler relocations, against frozen actual bodies. */
for(UINT tss=0;tss<2;tss++)for(UINT stage=0;stage<(tss?8u:1u);stage++)for(UINT state=0;state<(tss?32u:256u);state++)for(UINT vi=0;vi<160;vi++)for(fail=0;fail<2;fail++){
 a=(Device){.api=8,.handle=77};b=a;reset_capture();BOOL e=tss?old8_emit_texture_stage_state(&a,stage,state,values[vi]):old8_emit_render_state(&a,state,values[vi]);Capture c=captured;reset_capture();BOOL n=tss?now8_emit_texture_stage_state(&b,stage,state,values[vi]):now8_emit_render_state(&b,state,values[vi]);CHECK(e==n&&!memcmp(&c,&captured,sizeof(c)));emitter_cases++;cases++;
}
/* Public8 raw shadow/getter/recording and failure order stay frontend-owned. */
for(UINT recording=0;recording<2;recording++)for(UINT tss=0;tss<2;tss++)for(UINT stage=0;stage<(tss?9u:1u);stage++)for(UINT state=0;state<(tss?34u:258u);state++)for(UINT vi=0;vi<32;vi++)for(fail=0;fail<2;fail++){
 a=(Device){.api=8,.handle=77};b=a;memset(&r8a,0,sizeof(r8a));memset(&r8b,0,sizeof(r8b));if(recording){a.recording_state_block=&r8a;b.recording_state_block=&r8b;}reset_capture();HRESULT e=tss?old8_device_set_texture_stage_state(&a,stage,state,values[vi]):old8_device_set_render_state(&a,state,values[vi]);Capture c=captured;reset_capture();HRESULT n=tss?now8_device_set_texture_stage_state(&b,stage,state,values[vi]):now8_device_set_render_state(&b,state,values[vi]);CHECK(e==n&&!memcmp(&c,&captured,sizeof(c))&&!memcmp(&r8a,&r8b,sizeof(r8a)));DWORD x=99,y=99;HRESULT ex=tss?old8_device_get_texture_stage_state(&a,stage,state,&x):old8_device_get_render_state(&a,state,&x),ny=tss?now8_device_get_texture_stage_state(&b,stage,state,&y):now8_device_get_render_state(&b,state,&y);CHECK(ex==ny&&x==y);CHECK((tss?now8_device_get_texture_stage_state(&b,stage,state,NULL):now8_device_get_render_state(&b,state,NULL))==-1);a.recording_state_block=b.recording_state_block=NULL;CHECK(!memcmp(&a,&b,sizeof(a)));if(recording)b.recording_state_block=&r8b;reset_capture();CHECK((tss?now8_device_set_texture_stage_state(&b,stage,state,values[vi]):now8_device_set_render_state(&b,state,values[vi]))==(n==-1?-1:0)&&!captured.count);frontend_cases++;cases++;
}
/* Private8 boundary calls actual core9 setters, so duplicate filtering and
 * backend state-block records are tested in their translated numeric space. */
for(UINT recording=0;recording<2;recording++)for(UINT tss=0;tss<2;tss++)for(UINT stage=0;stage<(tss?8u:1u);stage++)for(UINT state=0;state<(tss?32u:256u);state++)for(UINT vi=0;vi<160;vi++)for(fail=0;fail<2;fail++){
 a=(Device){.api=8,.handle=77};b=a;memset(&r9a,0,sizeof(r9a));memset(&r9b,0,sizeof(r9b));if(recording){a.recording_result=&r9a;b.recording_result=&r9b;}reset_capture();if(tss)old8_emit_texture_stage_state(&a,stage,state,values[vi]);else old8_emit_render_state(&a,state,values[vi]);Capture plan=captured;reset_capture();HRESULT e=0;if(plan.count){if(plan.op==D9WG_OP_SET_RENDER_STATE)e=old9_device_set_render_state(&a,plan.bytes[1],plan.bytes[2]);else if(plan.op==D9WG_OP_SET_SAMPLER_STATE)e=old9_device_set_sampler_state(&a,stage,plan.bytes[2],plan.bytes[3]);else e=old9_device_set_texture_stage_state(&a,stage,plan.bytes[2],plan.bytes[3]);}Capture c=captured;reset_capture();HRESULT n=tss?d9wg_d3d9_core_set_texture_stage_state8(&b,stage,state,values[vi]):d9wg_d3d9_core_set_render_state8(&b,state,values[vi]);CHECK(e==n&&!memcmp(&c,&captured,sizeof(c))&&!memcmp(&r9a,&r9b,sizeof(r9a)));a.recording_result=b.recording_result=NULL;CHECK(!memcmp(&a,&b,sizeof(a)));backend_cases++;cases++;
}
/* Ordinary9 methods remain9 semantics, including slots8..15/257..260. */
for(UINT recording=0;recording<2;recording++)for(UINT kind=0;kind<3;kind++)for(UINT stage=0;stage<(kind?22u:1u);stage++)for(UINT state=0;state<(kind==0?258u:kind==1?35u:16u);state++)for(UINT vi=0;vi<32;vi++)for(fail=0;fail<2;fail++){
 a=(Device){.api=9,.handle=77};b=a;memset(&r9a,0,sizeof(r9a));memset(&r9b,0,sizeof(r9b));if(recording){a.recording_result=&r9a;b.recording_result=&r9b;}UINT slot=stage<17?stage:stage+240;reset_capture();HRESULT e=kind==0?old9_device_set_render_state(&a,state,values[vi]):kind==1?old9_device_set_texture_stage_state(&a,stage,state,values[vi]):old9_device_set_sampler_state(&a,slot,state,values[vi]);Capture c=captured;reset_capture();HRESULT n=kind==0?now9_device_set_render_state(&b,state,values[vi]):kind==1?now9_device_set_texture_stage_state(&b,stage,state,values[vi]):now9_device_set_sampler_state(&b,slot,state,values[vi]);CHECK(e==n&&!memcmp(&c,&captured,sizeof(c))&&!memcmp(&r9a,&r9b,sizeof(r9a)));a.recording_result=b.recording_result=NULL;CHECK(!memcmp(&a,&b,sizeof(a)));ordinary_cases++;cases++;
}
/* Wrong profiles/null/bounds must have no side effects even for dropped8 states. */
fail=0;for(UINT bad=0;bad<8;bad++){a=(Device){.api=bad==0?9:8,.handle=77};b=a;reset_capture();Device*p=bad==1?NULL:&b;UINT rs=bad==2?256u:bad==3?0xffffffffu:10u,stage=bad==4?8u:bad==5?0xffffffffu:7u,state=bad==6?32u:bad==7?0xffffffffu:13u;if(bad<4)CHECK(d9wg_d3d9_core_set_render_state8(p,rs,123)==-1);if(bad<2||bad>=4)CHECK(d9wg_d3d9_core_set_texture_stage_state8(p,stage,state,123)==-1);CHECK(!captured.count&&!memcmp(&a,&b,sizeof(a)));cases++;}
printf("PASS shared API8 states: %u checks,%u cases;emitters=%u rawFrontend=%u translatedBackend=%u ordinary9=%u;SDK8/9 enums,seven drops,ZBIAS clamp/floatbits,ten samplers,raw getters/recording,duplicate/failure order,actual core9 recording and profile/bounds\\n",checks,cases,emitter_cases,frontend_cases,backend_cases,ordinary_cases);return 0;}
`;
 fs.writeFileSync(dir+"/state.c",code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/state.c","-o",dir+"/state"],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+"/state",[],{encoding:"utf8",timeout:180000});assert.equal(run.status,0,run.stdout+run.stderr+" signal="+run.signal);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
