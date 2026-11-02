"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.D7WG_LIGHT_STATE_SOURCE||path.join(root,"ddrawproxy/d3d7_proxy.inc"),"utf8"),native=require(path.resolve(root,"../../v86/tests/glbridge/fixtures/d3d7_native_light_state.json"));
function extract(name){const m=new RegExp("static (?:HRESULT WINAPI|BOOL|D3D7StateBlock \\*) ?"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let at=source.indexOf("{",m.index)+1,depth=1;for(;depth;++at){assert(at<source.length);if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return source.slice(m.index,at);}
const names=['d3d7_emit_light','d3d7_device_SetLight','d3d7_device_GetLight','d3d7_device_LightEnable','d3d7_device_GetLightEnable','d3d7_state_block_capture','d3d7_device_EndStateBlock','d3d7_prepare_lighting'];
const code=`#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "${path.join(root,'d3d9proxy/d3d9_protocol.h')}"
typedef int BOOL,WINBOOL,HRESULT;typedef uint32_t DWORD,UINT;typedef uint8_t BYTE;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define DDERR_INVALIDPARAMS ((HRESULT)0x80070057u)
#define DDERR_OUTOFMEMORY ((HRESULT)0x8007000eu)
#define D3DERR_LIGHT_SET_FAILED (-3)
#define D3DERR_NOTINBEGINSTATEBLOCK (-4)
#define DDWG_TRACE_ERROR(x) (x)
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
#define D3D7_MAX_LIGHTS 8u
#define D3D7_MAX_RENDER_STATES 256u
#define D3D7_MAX_TEXTURE_STAGES 8u
#define D3D7_MAX_TEXTURE_STAGE_STATES 33u
#define D3D7_MAX_TRANSFORMS 24u
#define D3D7_MAX_CLIP_PLANES 6u
#define D3DTRANSFORMSTATE_VIEW 2u
#define D3DSBT_ALL 1u
#define D3DSBT_PIXELSTATE 2u
#define D3DSBT_VERTEXSTATE 3u
#define D3DLIGHT_POINT 1u
#define D3DLIGHT_DIRECTIONAL 3u
#define D3DLIGHT_GLSPOT 5u
#define D3DLIGHT_PARALLELPOINT 4u
typedef struct Color {float r,g,b,a;} Color;
typedef struct Vector {float x,y,z;} Vector;
typedef struct D3DLIGHT7 {DWORD dltType;Color dcvDiffuse,dcvSpecular,dcvAmbient;Vector dvPosition,dvDirection;float dvRange,dvFalloff,dvAttenuation0,dvAttenuation1,dvAttenuation2,dvTheta,dvPhi;} D3DLIGHT7;
_Static_assert(sizeof(D3DLIGHT7)==104,"SDK light");
#define D9WG_LIGHTING_SDK_LIGHT D3DLIGHT7
static unsigned allocations,frees,emissions;static int fail_alloc;
static void *allocate(size_t n){if(fail_alloc)return NULL;allocations++;return calloc(1,n);}
static void release(void *p){frees++;free(p);}
#define D9WG_LIGHT_ALLOC(n) allocate(n)
#define D9WG_LIGHT_FREE(p) release(p)
#include "${path.join(root,'runtime/d9wg_light_state.h')}"
typedef struct D3DMATRIX {float data[16];} D3DMATRIX;
typedef struct Material {uint32_t words[17];} Material;
typedef struct Viewport {DWORD x,y,width,height;float min_z,max_z;} Viewport;
typedef struct DDSurface {int vtbl7;} DDSurface;
typedef struct IDirectDrawSurface7 {int unused;} IDirectDrawSurface7;
static void surface_AddRef(IDirectDrawSurface7 *p){(void)p;}
static void surface_Release(IDirectDrawSurface7 *p){(void)p;}
typedef struct D3D7StateBlock {struct D3D7StateBlock *next;DWORD handle,type;BOOL all_lights_scope;BYTE light_mask[8],light_enable_mask[8],render_mask[256],texture_mask[8],texture_stage_mask[8][33],transform_mask[24],clip_plane_mask[6];BOOL viewport_mask,material_mask;DWORD render_states[256],texture_stage_states[8][33];DDSurface *textures[8];D3DMATRIX transforms[24];D3DLIGHT7 lights[8];BOOL light_set[8],light_enabled[8];D9WGLightState *extra_lights;float clip_planes[6][4];Viewport viewport;Material material;} D3D7StateBlock;
typedef struct Owner {uint32_t device_handle;} Owner;
typedef struct D3D7Device {D3DLIGHT7 lights[8];BOOL light_set[8],light_enabled[8];D9WGLightState *extra_lights;D3D7StateBlock *recording_state_block,*state_blocks;DWORD next_state_block_handle;Owner *owner;DWORD render_states[256],texture_stage_states[8][33];DDSurface *textures[8];D3DMATRIX transforms[24];float clip_planes[6][4];Viewport viewport;Material material;} D3D7Device;
typedef D3D7Device IDirect3DDevice7;
static D3D7Device *d3d7_device_from_iface(D3D7Device *p){return p;}
static uint32_t ddraw_light_type_to_d3d9(uint32_t t){return t;}
static BOOL emit_command(uint32_t op,const void *p,uint32_t n){(void)op;(void)p;(void)n;emissions++;return TRUE;}
typedef struct D9WGTNLLighting {struct {BOOL lighting;} flags;unsigned count;uint32_t indices[8];} D9WGTNLLighting;
static void d9wg_lighting_sdk_prepare(D9WGTNLLighting *state,const void *material,const DWORD *rs,const void *matrix){(void)material;(void)rs;(void)matrix;memset(state,0,sizeof(*state));}
static BOOL d9wg_lighting_sdk_add(D9WGTNLLighting *state,uint32_t index,const D3DLIGHT7 *light,const void *view){(void)light;(void)view;if(state->count==8)return FALSE;state->indices[state->count++]=index;return TRUE;}
${names.map(extract).join('\n')}
static const struct Row {uint32_t index,op,changed,get,enable,enabled,words[26];} rows[]={${native.cases.map(r=>'{'+[r.index,r.op,r.changed,r.get,r.enable,r.enabled].map(n=>n+'u').join(',')+',{'+r.words.map(n=>n+'u').join(',')+'}}').join(',')}};
#define CHECK(x) do{if(!(x)){fprintf(stderr,"D3D7 light state line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
int main(void){Owner owner={77};D3D7Device device={0};device.owner=&owner;D3DLIGHT7 custom;memcpy(&custom,rows[3].words,104);
 for(unsigned i=0;i<35;++i){const struct Row *r=&rows[i];HRESULT changed=0;if(r->op==1||r->op==4||r->op==6)changed=d3d7_device_LightEnable(&device,r->index,FALSE);if(r->op==2||r->op==5)changed=d3d7_device_LightEnable(&device,r->index,TRUE);if(r->op==3)changed=d3d7_device_SetLight(&device,r->index,&custom);D3DLIGHT7 got;memset(&got,0xcc,104);BOOL enabled=(BOOL)0xccccccccu;HRESULT get=d3d7_device_GetLight(&device,r->index,&got),enable=d3d7_device_GetLightEnable(&device,r->index,&enabled);CHECK((uint32_t)changed==r->changed&&(uint32_t)get==r->get&&(uint32_t)enable==r->enable&&(uint32_t)enabled==r->enabled&&!memcmp(&got,r->words,104));}
 CHECK(d3d7_device_SetLight(&device,0xffffffffu,&custom)==0);BOOL enabled;CHECK(d3d7_device_LightEnable(&device,0xffffffffu,7)==0&&d3d7_device_GetLightEnable(&device,0xffffffffu,&enabled)==0&&enabled==128);CHECK(d3d7_device_LightEnable(&device,0xffffffffu,FALSE)==0);
 D3D7StateBlock block={0};device.recording_state_block=&block;D3D7Device before=device;unsigned sent=emissions;custom.dcvDiffuse.r=.5f;CHECK(d3d7_device_SetLight(&device,37,&custom)==0&&d3d7_device_LightEnable(&device,37,TRUE)==0&&d3d7_device_LightEnable(&device,1001,FALSE)==0);CHECK(d3d7_device_SetLight(&device,3,&custom)==0&&d3d7_device_LightEnable(&device,3,TRUE)==0);CHECK(!memcmp(&device,&before,sizeof(device))&&emissions==sent);CHECK(block.light_mask[3]&&block.light_enable_mask[3]&&block.light_enabled[3]);D9WGLightState *recorded=d9wg_light_find(block.extra_lights,37),*standard=d9wg_light_find(block.extra_lights,1001);CHECK(recorded&&recorded->light_mask&&recorded->enable_mask&&recorded->light.dcvDiffuse.r==.5f&&recorded->enabled);CHECK(standard&&standard->light_mask&&standard->enable_mask&&standard->light.dvDirection.z==1&&standard->light.dcvDiffuse.r==1&&!standard->enabled);
 DWORD token=0;CHECK(d3d7_device_EndStateBlock(&device,&token)==0&&token&&device.recording_state_block==NULL&&recorded->light.dcvDiffuse.r==.5f);device.recording_state_block=&block;fail_alloc=1;CHECK(d3d7_device_LightEnable(&device,2000,TRUE)==DDERR_OUTOFMEMORY&&!d9wg_light_find(block.extra_lights,2000));fail_alloc=0;device.recording_state_block=NULL;d9wg_light_free(&block.extra_lights);
 /* Predefined scopes select keys once. Native D3D7 vertex blocks save the
  * enable bit; ALL additionally saves properties. Later Capture adds no keys. */
 for(unsigned type=1;type<=3;++type){D3D7StateBlock capture={0};capture.type=type;capture.all_lights_scope=type!=2;for(unsigned i=0;i<8;++i){capture.light_mask[i]=type==1;capture.light_enable_mask[i]=type!=2;}CHECK(d3d7_state_block_capture(&device,&capture,FALSE));D9WGLightState *entry=d9wg_light_find(capture.extra_lights,37);CHECK(type==2?!entry:entry&&entry->light_mask==(int)(type==1)&&entry->enable_mask);CHECK(d3d7_device_SetLight(&device,3000+type,&custom)==0);CHECK(d3d7_state_block_capture(&device,&capture,FALSE)&&!d9wg_light_find(capture.extra_lights,3000+type));d9wg_light_free(&capture.extra_lights);}
 CHECK(d3d7_device_LightEnable(&device,37,TRUE)==0);D9WGTNLLighting lighting;D3DMATRIX matrix={0};CHECK(d3d7_prepare_lighting(&device,&matrix,TRUE,&lighting)&&lighting.count==1&&lighting.indices[0]==37);for(unsigned i=0;i<8;++i)CHECK(d3d7_device_LightEnable(&device,10000+i,TRUE)==0);CHECK(!d3d7_prepare_lighting(&device,&matrix,TRUE,&lighting));
 d9wg_light_free(&device.extra_lights);CHECK(allocations==frees);return 0;}
`;
const directory=fs.mkdtempSync(path.join(os.tmpdir(),"d7wg-light-state-"));try{const file=path.join(directory,"light.c"),binary=path.join(directory,"light");fs.writeFileSync(file,code);const built=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",file,"-o",binary],{encoding:"utf8"});assert.equal(built.status,0,built.stderr||String(built.error));const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr||String(run.error));console.log("PASS actual D3D7 light C: 35 native queries, exact SDK fields/BOOLs, arbitrary DWORD indices, deferred/default recording, fixed capture keys, native vertex/ALL masks, atomic OOM, sparse TNL and active budget");}finally{fs.rmSync(directory,{recursive:true,force:true});}
