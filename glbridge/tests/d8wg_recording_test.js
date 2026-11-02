"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.D8WG_RECORD_SOURCE||path.join(root,"d3d8proxy/d3d8_proxy.c"),"utf8");
function extract(name){const re=new RegExp("static (?:HRESULT WINAPI|BOOL|void) "+name+"\\([^;]*?\\)\\s*\\{");const match=re.exec(source);assert(match,name);const first=source.indexOf("{",match.index);let at=first+1,depth=1;for(;depth&&at<source.length;++at){if(source[at]==="{")depth++;if(source[at]==="}")depth--;}assert.equal(depth,0);return source.slice(match.index,at);}
const names=["matrix_equal","matrix_multiply","bytes_equal","device_set_render_state","device_get_render_state","device_set_texture_stage_state","device_get_texture_stage_state","device_set_viewport","device_get_viewport","device_set_material","device_get_material","device_set_transform","device_get_transform","device_multiply_transform","device_set_vs_constant","device_get_vs_constant","device_set_ps_constant","device_get_ps_constant","device_set_clip_plane","device_get_clip_plane"];
const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "${path.join(root,'d3d9proxy/d3d9_protocol.h')}"
typedef int BOOL,HRESULT,D3DRENDERSTATETYPE,D3DTEXTURESTAGESTATETYPE,D3DTRANSFORMSTATETYPE;
typedef uint32_t DWORD,UINT;typedef uint8_t BYTE;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define D8WG_TRACE_ERROR(x) (x)
#define D8WG_TRACE(...) ((void)0)
#define CopyMemory memcpy
#define FillMemory(p,n,v) memset(p,v,n)
#define D8WG_MAX_VS_CONSTANTS 96u
#define D8WG_MAX_PS_CONSTANTS 8u
#define D8WG_MAX_CLIP_PLANES 6u
#define D8WG_MAX_RENDER_STATES 256
#define D8WG_MAX_TEXTURE_STAGES 8
#define D8WG_MAX_TEXTURE_STAGE_STATES 32
#define CHECK(x) do{if(!(x)){fprintf(stderr,"recording line %u\\n",__LINE__);return 1;}}while(0)
typedef struct D3DVIEWPORT8 {DWORD X,Y,Width,Height;float MinZ,MaxZ;} D3DVIEWPORT8;
typedef struct Color {float r,g,b,a;} Color;
typedef struct D3DMATERIAL8 {Color Diffuse,Ambient,Specular,Emissive;float Power;} D3DMATERIAL8;
typedef struct D3DMATRIX {float m[4][4];} D3DMATRIX;
typedef struct D8StateSnapshot {BYTE vs_constant_mask[96],ps_constant_mask[8],clip_plane_mask[6];float vs_constants[96][4],ps_constants[8][4],clip_planes[6][4];BYTE render_mask[256],texture_stage_mask[8][32],transform_mask[4];BOOL viewport_mask,material_mask;DWORD render_states[256],texture_stage_states[8][32];D3DVIEWPORT8 viewport;D3DMATERIAL8 material;D3DMATRIX transforms[4];} D8StateSnapshot;
typedef struct D8StateBlock {D8StateSnapshot state;} D8StateBlock;
typedef struct D8Device {float vs_constants[96][4],ps_constants[8][4],clip_planes[6][4];D8StateBlock *recording_state_block;DWORD handle,render_states[256],texture_stage_states[8][32];D3DVIEWPORT8 viewport;D3DMATERIAL8 material;D3DMATRIX transforms[4];} D8Device;
typedef D8Device IDirect3DDevice8;
static D8Device *device_from_iface(IDirect3DDevice8 *device){return device;}
static unsigned emissions;
static BOOL emit_render_state(D8Device *d,D3DRENDERSTATETYPE s,DWORD v){(void)d;(void)s;(void)v;emissions++;return TRUE;}
static BOOL emit_texture_stage_state(D8Device *d,DWORD t,D3DTEXTURESTAGESTATETYPE s,DWORD v){(void)d;(void)t;(void)s;(void)v;emissions++;return TRUE;}
static BOOL emit_command(uint32_t op,const void *payload,uint32_t bytes){(void)op;(void)payload;(void)bytes;emissions++;return TRUE;}
static HRESULT emit_transform(D8Device *d,D3DTRANSFORMSTATETYPE s,const D3DMATRIX *m){(void)d;(void)s;(void)m;emissions++;return D3D_OK;}
static BOOL emit_set_shader_constant(uint32_t op,D8Device *device,DWORD reg,const float *data,DWORD count){(void)op;(void)device;(void)reg;(void)data;(void)count;emissions++;return TRUE;}
static BOOL transform_slot(D3DTRANSFORMSTATETYPE state,UINT *slot){if(state<0||state>=4)return FALSE;*slot=state;return TRUE;}
static void current_render_target_size(D8Device *d,UINT *width,UINT *height){(void)d;*width=*height=64;}
${names.map(extract).join('\n')}
int main(void){
 D8Device device={0};D8StateBlock block={0};device.viewport=(D3DVIEWPORT8){0,0,64,64,0,1};device.material.Diffuse.r=.75f;for(unsigned c=0;c<4;++c)device.transforms[0].m[c][c]=1;
 D8Device before=device;device.recording_state_block=&block;DWORD value;
 for(unsigned i=0;i<32;++i){CHECK(device_set_render_state(&device,7,i)==0);CHECK(device_get_render_state(&device,7,&value)==0&&value==0);CHECK(block.state.render_mask[7]&&block.state.render_states[7]==i);}
 CHECK(device_set_render_state(&device,256,11)==-1&&block.state.render_states[7]==31);
 CHECK(device_set_texture_stage_state(&device,3,9,123)==0);CHECK(device_get_texture_stage_state(&device,3,9,&value)==0&&value==0);CHECK(block.state.texture_stage_mask[3][9]&&block.state.texture_stage_states[3][9]==123);
 CHECK(device_set_texture_stage_state(&device,8,0,123)==-1);
 D3DVIEWPORT8 vp={1,2,32,30,.25f,.75f},read_vp;CHECK(device_set_viewport(&device,&vp)==0);CHECK(device_get_viewport(&device,&read_vp)==0&&read_vp.Width==64);CHECK(block.state.viewport_mask&&block.state.viewport.Width==32);
 vp.Width=64;CHECK(device_set_viewport(&device,&vp)==-1&&block.state.viewport.Width==32);
 D3DMATERIAL8 material={0},read_material;material.Diffuse.r=.125f;CHECK(device_set_material(&device,&material)==0);CHECK(device_get_material(&device,&read_material)==0&&read_material.Diffuse.r==.75f);CHECK(block.state.material_mask&&block.state.material.Diffuse.r==.125f);
 D3DMATRIX matrix={0},scale={0},read_matrix;for(unsigned c=0;c<4;++c)matrix.m[c][c]=scale.m[c][c]=1;matrix.m[0][0]=2;matrix.m[1][1]=3;matrix.m[3][0]=4;scale.m[0][0]=.5f;scale.m[1][1]=2;
 CHECK(device_set_transform(&device,0,&matrix)==0);CHECK(device_multiply_transform(&device,0,&scale)==0);CHECK(device_get_transform(&device,0,&read_matrix)==0&&read_matrix.m[0][0]==1&&read_matrix.m[3][0]==0);
 CHECK(block.state.transform_mask[0]&&block.state.transforms[0].m[0][0]==1&&block.state.transforms[0].m[1][1]==6&&block.state.transforms[0].m[3][0]==2);
 CHECK(device_set_transform(&device,4,&matrix)==-1&&device_set_material(&device,NULL)==-1);
 float data[2][4]={{.125f,.25f,.5f,.75f},{-.125f,-.25f,-.5f,1}},read[2][4];
 CHECK(device_set_vs_constant(&device,94,data,2)==0&&device_get_vs_constant(&device,94,read,2)==0&&read[0][0]==0&&read[1][3]==0);
 CHECK(block.state.vs_constant_mask[94]&&block.state.vs_constant_mask[95]&&!block.state.vs_constant_mask[93]&&block.state.vs_constants[95][3]==1);
 CHECK(device_set_vs_constant(&device,95,data,1)==0&&block.state.vs_constants[95][0]==.125f);
 CHECK(device_set_vs_constant(&device,95,data,2)==-1&&device_set_vs_constant(&device,0xffffffffu,NULL,0)==0);
 CHECK(device_set_ps_constant(&device,6,data,2)==0&&device_get_ps_constant(&device,6,read,2)==0&&read[0][0]==0&&read[1][3]==0);
 CHECK(block.state.ps_constant_mask[6]&&block.state.ps_constant_mask[7]&&!block.state.ps_constant_mask[5]&&block.state.ps_constants[7][3]==1);
 CHECK(device_set_ps_constant(&device,7,data,2)==-1&&device_set_ps_constant(&device,0xffffffffu,NULL,0)==0);
 CHECK(device_set_clip_plane(&device,5,data[0])==0&&device_get_clip_plane(&device,5,read[0])==0&&read[0][0]==0);
 CHECK(block.state.clip_plane_mask[5]&&!block.state.clip_plane_mask[4]&&block.state.clip_planes[5][2]==.5f);
 CHECK(device_set_clip_plane(&device,6,data[0])==-1&&device_set_clip_plane(&device,0,NULL)==-1);
 CHECK(!emissions);device.recording_state_block=NULL;CHECK(!memcmp(&device,&before,sizeof(device)));
 CHECK(device_set_render_state(&device,7,31)==0&&device.render_states[7]==31&&emissions==1);
 CHECK(device_set_render_state(&device,7,31)==0&&emissions==1);
 return 0;
}
`;
const directory=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-record-"));
try{const input=path.join(directory,"record.c"),binary=path.join(directory,"record");fs.writeFileSync(input,code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",input,"-o",binary],{encoding:"utf8"});assert.equal(build.status,0,build.stderr||String(build.error));const result=spawnSync(binary,[],{encoding:"utf8"});assert.equal(result.status,0,result.stderr||String(result.error));console.log("PASS actual D3D8 C recording entry points: repeated/same values, live queries, validation, viewport/material/transform composition, VS/PS constant boundaries and masks, clip planes, zero host emission and subsequent ordinary writes");}finally{fs.rmSync(directory,{recursive:true,force:true});}
