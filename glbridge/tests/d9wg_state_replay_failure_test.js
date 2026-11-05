"use strict";
// Execute the production replay engine with fallible setter providers. This
// checks sequencing/error propagation; real objects/rendering use SDK XP tests.
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict");
const {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "state-replay-"));
const source = fs.readFileSync(process.env.D9WG_REPLAY_SOURCE || root + "/d3d9proxy/d3d9_proxy.c", "utf8");
function extract(name) {
    const m = new RegExp("static HRESULT " + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name);
    let end = source.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {assert(end < source.length); depth += (source[end] === "{") - (source[end] === "}");}
    return source.slice(m.index, end);
}
const setters = ["device_set_current_texture_palette", "IDirect3DDevice9_SetRenderState", "IDirect3DDevice9_SetTextureStageState", "IDirect3DDevice9_SetTexture", "IDirect3DDevice9_SetSamplerState", "IDirect3DDevice9_SetTransform", "IDirect3DDevice9_SetMaterial", "IDirect3DDevice9_SetClipPlane", "IDirect3DDevice9_SetNPatchMode", "IDirect3DDevice9_SetLight", "IDirect3DDevice9_LightEnable", "IDirect3DDevice9_SetViewport", "IDirect3DDevice9_SetScissorRect", "IDirect3DDevice9_SetVertexShader", "IDirect3DDevice9_SetPixelShader", "IDirect3DDevice9_SetVertexShaderConstantF", "IDirect3DDevice9_SetPixelShaderConstantF", "IDirect3DDevice9_SetVertexShaderConstantI", "IDirect3DDevice9_SetPixelShaderConstantI", "IDirect3DDevice9_SetVertexShaderConstantB", "IDirect3DDevice9_SetPixelShaderConstantB", "IDirect3DDevice9_SetVertexDeclaration", "IDirect3DDevice9_SetFVF", "device_set_stream_binding", "IDirect3DDevice9_SetStreamSourceFreq", "IDirect3DDevice9_SetIndices"];
const header = fs.readFileSync(root + "/d3d9proxy/d3d9_core.h", "utf8").match(/enum D9WGStateBlockApplyGroup8 \{[\s\S]*?typedef HRESULT \(WINAPI \*D9WGStateBlockApply8\)[\s\S]*?;/)[0];
const code = `#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef uint32_t UINT,DWORD; typedef int32_t HRESULT; typedef int BOOL;
#define WINAPI
#define D3D_OK 0
#define FAILED(x) ((x)<0)
#define D3DERR_DRIVERINTERNALERROR ((HRESULT)0x88760827u)
#define E_OUTOFMEMORY ((HRESULT)0x8007000eu)
${header}
#define D9_MAX_RENDER_STATES 2
#define D9_MAX_TEXTURE_BINDINGS 3
#define D9_MAX_TEXTURE_STAGES 2
#define D9_MAX_TEXTURE_STAGE_STATES 2
#define D9_MAX_SAMPLERS 2
#define D9_MAX_SAMPLER_STATES 2
#define D9_STATE_BLOCK_TRANSFORMS 2
#define D9_MAX_CLIP_PLANES 2
#define D9_MAX_LIGHTS 2
#define D9_MAX_VS_CONST_F 2
#define D9_MAX_PS_CONST_F 2
#define D9_MAX_CONST_I 2
#define D9_MAX_CONST_B 2
#define D9_MAX_STREAMS 2
typedef int D3DRENDERSTATETYPE,D3DTEXTURESTAGESTATETYPE,D3DSAMPLERSTATETYPE,D3DTRANSFORMSTATETYPE;
typedef float D3DMATRIX[16];
typedef struct Interface {int value;} IDirect3DDevice9,IDirect3DBaseTexture9;
typedef struct Resource {IDirect3DDevice9 iface;} Resource;
typedef struct Shader {struct{IDirect3DDevice9 vertex,pixel;}iface;} Shader;
typedef struct D9Device {IDirect3DDevice9 iface;} D9Device;
typedef struct D9WGLightState {struct D9WGLightState*next;UINT index;BOOL light_mask,set,enable_mask,enabled;int light;} D9WGLightState;
struct D9StateBlock {
 D9Device*device;BOOL has_palette;UINT current_palette;
 BOOL has_render_state[2];UINT render_states[2];
 BOOL has_texture_stage_state[2][2];UINT texture_stage_states[2][2];
 BOOL has_texture[3];Resource*textures[3],*cube_textures[3],*volume_textures[3];
 BOOL has_sampler_state[2][2];UINT sampler_states[2][2];
 BOOL has_transform[2];float transforms[2][16];BOOL has_material;int material;
 BOOL has_clip_plane[2];float clip_planes[2][4];BOOL has_npatch_mode;float npatch_segments;
 BOOL has_light[2],has_light_enable[2],light_enabled[2];int lights[2];D9WGLightState*extra_lights;
 BOOL has_viewport,has_scissor;int viewport,scissor_rect;
 BOOL has_vertex_shader,has_pixel_shader;Shader*vertex_shader,*pixel_shader;
 BOOL has_vs_const_f[2],has_ps_const_f[2];float vs_const_f[2][4],ps_const_f[2][4];
 BOOL has_vs_const_i[2],has_ps_const_i[2];int vs_const_i[2][4],ps_const_i[2][4];
 BOOL has_vs_const_b[2],has_ps_const_b[2],vs_const_b[2],ps_const_b[2];
 BOOL has_vertex_format;Resource*vertex_declaration;UINT fvf;
 BOOL has_stream[2],has_stream_frequency[2];struct{Resource*buffer;UINT offset,stride,frequency;}streams[2];
 BOOL has_indices;Resource*index_buffer;
};
static UINT trace[256],calls,fail_at,index_observations;static HRESULT injected,observed_index;
static HRESULT setter(UINT kind,...) {assert(calls<256);trace[calls++]=kind;return calls==fail_at?injected:D3D_OK;}
${setters.map((name,i) => "#define " + name + "(...) setter(" + i + ",__VA_ARGS__)").join("\n")}
static UINT texture_binding_stage(UINT i){return i;}static UINT sampler_state_index(UINT i){return i;}static UINT d9wg_state_block_transform_state(UINT i){return i;}
${extract("state_block_apply_shared")}
${extract("state_block_apply")}
static HRESULT WINAPI intercept(void*context,UINT group,UINT index,HRESULT result){
 assert(context==(void*)1);assert(group<=D9WG_APPLY8_INDICES);
 if(group==D9WG_APPLY8_INDICES){index_observations++;observed_index=result;return result;}
 assert(!index||group==D9WG_APPLY8_TEXTURE_STAGE);return setter(80+group);
}
static UINT checks,cases;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"state replay failure line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 D9Device device={0};Resource resource={{0}};Shader shader={0};D9WGLightState light={NULL,999,1,1,1,1,0};
 for(UINT mode=0;mode<4;mode++){
  struct D9StateBlock b;memset(&b,0,sizeof(b));b.device=&device;
  b.has_palette=b.has_material=b.has_npatch_mode=b.has_viewport=b.has_scissor=1;
  b.has_vertex_shader=b.has_pixel_shader=b.has_vertex_format=b.has_indices=1;
  b.vertex_shader=b.pixel_shader=&shader;b.vertex_declaration=mode%2?NULL:&resource;b.fvf=mode%2?68:0;b.index_buffer=&resource;b.extra_lights=&light;
  for(UINT i=0;i<2;i++){
   b.has_render_state[i]=b.has_transform[i]=b.has_clip_plane[i]=b.has_light[i]=b.has_light_enable[i]=b.has_vs_const_f[i]=b.has_ps_const_f[i]=b.has_vs_const_i[i]=b.has_ps_const_i[i]=b.has_vs_const_b[i]=b.has_ps_const_b[i]=b.has_stream[i]=b.has_stream_frequency[i]=1;
   b.streams[i].buffer=&resource;
   for(UINT j=0;j<2;j++)b.has_texture_stage_state[i][j]=b.has_sampler_state[i][j]=1;
  }
  for(UINT i=0;i<3;i++)b.has_texture[i]=1;
  b.textures[0]=&resource;b.cube_textures[1]=&resource;b.volume_textures[2]=&resource;
  BOOL api8=mode>=2;calls=fail_at=index_observations=0;
  CHECK((api8?state_block_apply_shared(&b,intercept,(void*)1):state_block_apply(&b))==0);
  UINT count=calls,baseline[256];memcpy(baseline,trace,sizeof(trace));CHECK(count>35);
  for(UINT kind=0;kind<26;kind++){
   UINT seen=0;for(UINT i=0;i<count;i++)seen+=baseline[i]==kind;
   if(api8&&(kind==1||kind==2||kind==4||kind==13||kind==14||kind==21||kind==22))CHECK(!seen);
   else if(kind==21||kind==22)CHECK(seen==(mode%2?(kind==22):(kind==21)));
   else CHECK(seen>0);
  }
  CHECK(index_observations==(api8?1u:0u));
  for(UINT stop=1;stop<=count;stop++)for(UINT error=0;error<2;error++){
   calls=index_observations=0;fail_at=stop;injected=error?E_OUTOFMEMORY:D3DERR_DRIVERINTERNALERROR;
   HRESULT hr=api8?state_block_apply_shared(&b,intercept,(void*)1):state_block_apply(&b);
   CHECK(hr==injected);CHECK(calls==stop);CHECK(!memcmp(trace,baseline,stop*sizeof(UINT)));
   CHECK(index_observations==(api8&&stop==count?1u:0u));if(index_observations)CHECK(observed_index==injected);cases++;
  }
  // An empty ordinary9 snapshot has no setter effects. Interceptors remain
  // responsible for their own masks, so the API8 path is tested above.
  memset(&b,0,sizeof(b));b.device=&device;calls=0;CHECK(state_block_apply(&b)==0&&calls==0);
 }
 printf("PASS actual shared state replay: %u checks / %u injected failures; every setter family, first-error stop, exact executed prefix, API8 raw overrides and index commit notification\\n",checks,cases);return 0;
}
`;
try {
    fs.writeFileSync(dir + "/test.c", code);
    const build = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", dir + "/test.c", "-o", dir + "/test"], {encoding:"utf8"});
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(dir + "/test", [], {encoding:"utf8"}); assert.equal(run.status, 0, run.stdout + run.stderr); process.stdout.write(run.stdout);
} finally {fs.rmSync(dir, {recursive:true, force:true});}
