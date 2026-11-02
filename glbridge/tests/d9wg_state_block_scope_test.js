"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const {testPath}=require("./host_paths.js"),reference=require(testPath("fixtures/d3d_state_block_scope_reference.json")),root=path.resolve(__dirname,"..");
function extract(source,name){const match=new RegExp("static (?:void|BOOL) "+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(match,name);let at=source.indexOf("{",match.index)+1,depth=1;for(;depth;++at){assert(at<source.length);if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return source.slice(match.index,at);}
const directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-scope-"));
try{for(const api of [8,9]){
 const ref=reference.apis[api],source=fs.readFileSync(process.env["D"+api+"WG_SCOPE_SOURCE"]||path.join(root,"d3d"+api+"proxy/d3d"+api+"_proxy.c"),"utf8"),header=process.env.D9WG_SCOPE_HEADER||path.join(root,"runtime/d9wg_state_block_scope.h");
 const old=api===9&&source.includes("static const WORD g_pixel_state_render_states[]");
 const oldArrays=old?["g_pixel_state_render_states","g_vertex_state_render_states","g_state_block_transforms"].map(name=>{const m=new RegExp("static const WORD "+name+"\\[\\] = \\{[\\s\\S]*?\\};").exec(source);assert(m,name);return m[0];}).join("\n"):"";
 const privateTypes=api===8?`
#define D8WG_MAX_RENDER_STATES 256u
#define D8WG_MAX_TEXTURE_STAGES 8u
#define D8WG_MAX_TEXTURE_STAGE_STATES 32u
#define D8WG_MAX_LIGHTS 8u
#define D8WG_MAX_VS_CONSTANTS 96u
#define D8WG_MAX_PS_CONSTANTS 8u
#define D8WG_MAX_CLIP_PLANES 6u
typedef struct D8StateSnapshot {BYTE render_mask[256],texture_stage_mask[8][32],texture_mask[8],stream_mask[16],transform_mask[266],light_mask[8],light_enable_mask[8],vs_constant_mask[96],ps_constant_mask[8],clip_plane_mask[6];BOOL palette_mask,all_lights_scope,viewport_mask,material_mask,indices_mask,vertex_shader_mask,pixel_shader_mask;} D8StateSnapshot;
typedef struct D8StateBlock {D8StateSnapshot state;} D8StateBlock;
${extract(source,'state_block_set_scope')}
`:`
#define D9_MAX_RENDER_STATES 256u
#define D9_MAX_TEXTURE_STAGES 8u
#define D9_MAX_TEXTURE_STAGE_STATES 33u
#define D9_MAX_TEXTURE_BINDINGS 20u
#define D9_MAX_SAMPLERS 20u
#define D9_MAX_SAMPLER_STATES 14u
#define D9_MAX_LIGHTS 8u
#define D9_MAX_STREAMS 16u
#define D9_MAX_CLIP_PLANES 6u
#define D9_MAX_VS_CONST_F 256u
#define D9_MAX_PS_CONST_F 224u
#define D9_MAX_CONST_I 16u
#define D9_MAX_CONST_B 16u
#define D3DTS_VIEW 2
#define D3DTS_PROJECTION 3
#define D3DTS_WORLD 256
#define D3DTS_WORLDMATRIX(i) (256+(i))
${Array.from({length:8},(_,i)=>'#define D3DTS_TEXTURE'+i+' '+(16+i)).join('\n')}
#define D9_STATE_BLOCK_TRANSFORMS ${old?14:'D9WG_STATE_BLOCK_TRANSFORMS'}
typedef struct Light {uint32_t words[26];} Light;
#define D9WG_LIGHTING_SDK_LIGHT Light
#define D9WG_LIGHT_ALLOC(bytes) calloc(1,bytes)
#define D9WG_LIGHT_FREE(pointer) free(pointer)
#include "${path.join(root,'runtime/d9wg_light_state.h')}"
typedef struct D9StateBlock D9StateBlock;
typedef struct D9Device {BOOL light_set[8];D9WGLightState *extra_lights;D9StateBlock *recording_result;} D9Device;
struct D9StateBlock {D9Device *device;BOOL has_render_state[256],has_texture_stage_state[8][33],has_sampler_state[20][14],has_texture[20],has_transform[266],has_palette,has_material,has_clip_plane[6],has_npatch_mode,has_light[8],has_light_enable[8],has_vertex_shader,has_vertex_format,has_pixel_shader,has_vs_const_f[256],has_vs_const_i[16],has_vs_const_b[16],has_ps_const_f[224],has_ps_const_i[16],has_ps_const_b[16],has_viewport,has_scissor,has_stream[16],has_stream_frequency[16],has_indices,light_allocation_failed;D9WGLightState *extra_lights;};
static BOOL device_light_is_set(D9Device *device,UINT index){return index<8&&device->light_set[index];}
${oldArrays}
${['state_block_record_transform','state_block_mark_pixel_states','state_block_mark_vertex_states','state_block_mark_all'].map(n=>extract(source,n)).join('\n')}
`;
 const typeChecks=api===8?`
 D8StateBlock block={0};state_block_set_scope(&block,type);D8StateSnapshot *s=&block.state;
 for(unsigned i=0;i<256;++i)CHECK(!!s->render_mask[i]==(type==1?1:expected[type-2][i]));
 for(unsigned stage=0;stage<8;++stage){CHECK(!!s->texture_mask[stage]==(type==1));for(unsigned i=0;i<32;++i)CHECK(!!s->texture_stage_mask[stage][i]==tss_expected(type,i));}
 for(unsigned i=0;i<16;++i)CHECK(!!s->stream_mask[i]==(type==1));for(unsigned i=0;i<266;++i)CHECK(!!s->transform_mask[i]==(type==1));
 CHECK(!!s->palette_mask==(type==1));
 CHECK(!!s->viewport_mask==(type==1)&&!!s->material_mask==(type==1)&&!!s->indices_mask==(type==1));
 CHECK(!!s->vertex_shader_mask==(type!=2)&&!!s->pixel_shader_mask==(type!=3));
 for(unsigned i=0;i<96;++i)CHECK(!!s->vs_constant_mask[i]==(type!=2));for(unsigned i=0;i<8;++i)CHECK(!!s->ps_constant_mask[i]==(type!=3));
 for(unsigned i=0;i<6;++i)CHECK(!!s->clip_plane_mask[i]==(type==1));
 for(unsigned i=0;i<8;++i)CHECK(!!s->light_mask[i]==(type!=2)&&!!s->light_enable_mask[i]==(type!=2));
`:`
 D9WGLightState light={0};light.index=37;light.set=TRUE;D9Device device={0};device.light_set[3]=TRUE;device.extra_lights=&light;
 D9StateBlock block={0};block.device=&device;if(type==1)state_block_mark_all(&block);else if(type==2)state_block_mark_pixel_states(&block);else state_block_mark_vertex_states(&block);
 for(unsigned i=0;i<256;++i)CHECK(!!block.has_render_state[i]==(type==1?1:expected[type-2][i]));
 for(unsigned stage=0;stage<8;++stage)for(unsigned i=0;i<33;++i)CHECK(!!block.has_texture_stage_state[stage][i]==tss_expected(type,i));
 for(unsigned i=0;i<20;++i){CHECK(!!block.has_texture[i]==(type==1));for(unsigned state=0;state<14;++state)CHECK(!!block.has_sampler_state[i][state]==(state&&state<=13&&(type==1||(state==13?type==3:type==2))));}
 for(unsigned i=0;i<266;++i)CHECK(!!block.has_transform[i]==(type==1));
 CHECK(!!block.has_palette==(type==1));
 CHECK(!!block.has_material==(type==1)&&!!block.has_viewport==(type==1)&&!!block.has_scissor==(type==1)&&!!block.has_indices==(type==1));
 for(unsigned i=0;i<6;++i)CHECK(!!block.has_clip_plane[i]==(type==1));
 CHECK(!!block.has_npatch_mode==(type!=2));for(unsigned i=0;i<16;++i)CHECK(!!block.has_stream[i]==(type==1)&&!!block.has_stream_frequency[i]==(type!=2));
 CHECK(!!block.has_vertex_shader==(type!=2)&&!!block.has_pixel_shader==(type!=3)&&!!block.has_vertex_format==(type!=2));
 for(unsigned i=0;i<256;++i)CHECK(!!block.has_vs_const_f[i]==(type!=2));for(unsigned i=0;i<224;++i)CHECK(!!block.has_ps_const_f[i]==(type!=3));
 for(unsigned i=0;i<16;++i)CHECK(!!block.has_vs_const_i[i]==(type!=2)&&!!block.has_vs_const_b[i]==(type!=2)&&!!block.has_ps_const_i[i]==(type!=3)&&!!block.has_ps_const_b[i]==(type!=3));
 for(unsigned i=0;i<8;++i)CHECK(!!block.has_light[i]==(type!=2&&i==3)&&!!block.has_light_enable[i]==(type!=2&&i==3));
 CHECK(type==2?!block.extra_lights:block.extra_lights&&block.extra_lights->index==37&&block.extra_lights->light_mask&&block.extra_lights->enable_mask);
 d9wg_light_free(&block.extra_lights);
 D9StateBlock recorded={0};device.recording_result=&recorded;state_block_record_transform(&device,511);CHECK(recorded.has_transform[265]);
`;
 const expected=['pixel','vertex'].map(kind=>Array.from({length:256},(_,i)=>ref[kind].some(r=>r.state===i)?1:0));
 const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef uint32_t UINT,DWORD,D3DSTATEBLOCKTYPE;typedef uint16_t WORD;typedef uint8_t BYTE;typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define FillMemory(p,n,v) memset(p,v,n)
enum {D3DSBT_ALL=1,D3DSBT_PIXELSTATE=2,D3DSBT_VERTEXSTATE=3,${Object.entries(ref.sdkEnums).map(([n,v])=>n+'='+v+'u').join(',')}};
#define D9WG_STATE_BLOCK_API ${api}
${old?'#define g_pixel_state_render_states header_pixel_states\n#define g_vertex_state_render_states header_vertex_states':''}
#include "${header}"
${old?'#undef g_pixel_state_render_states\n#undef g_vertex_state_render_states':''}
${privateTypes}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"scope API ${api} line %u type %u: %s\\n",__LINE__,type,#x);return 1;}}while(0)
static const uint8_t expected[2][256]={${expected.map(a=>'{'+a.join(',')+'}').join(',')}};
static int tss_expected(unsigned type,unsigned state){if(type==1)return 1;if(type==3)return state==11||state==24;return (state>=1&&state<=11)||(state>=22&&state<=24)||(state>=26&&state<=28)${api===8?'||(state>=13&&state<=21)||state==25':''};}
int main(void){unsigned type=0;
 ${old?'(void)header_pixel_states;(void)header_vertex_states;':''}
 uint8_t membership[2][256]={{0}};for(unsigned i=0;i<sizeof(g_pixel_state_render_states)/2;++i){CHECK(g_pixel_state_render_states[i]<256);membership[0][g_pixel_state_render_states[i]]=1;}for(unsigned i=0;i<sizeof(g_vertex_state_render_states)/2;++i){CHECK(g_vertex_state_render_states[i]<256);membership[1][g_vertex_state_render_states[i]]=1;}CHECK(!memcmp(membership,expected,sizeof(expected)));
 for(uint32_t slot=0;slot<266;++slot){uint32_t got=999,state=d9wg_state_block_transform_state(slot);CHECK(d9wg_state_block_transform_slot(state,&got)&&got==slot);}CHECK(d9wg_state_block_transform_state(266)==UINT32_MAX);
 for(uint32_t state=0;state<1024;++state){uint32_t slot=999;int valid=state==2||state==3||(state>=16&&state<=23)||(state>=256&&state<=511);CHECK(d9wg_state_block_transform_slot(state,&slot)==valid);if(!valid)CHECK(slot==999);}
 for(type=1;type<=3;++type){for(unsigned state=0;state<${api===8?32:33};++state)CHECK(d9wg_state_block_texture_state(type,state)==tss_expected(type,state));${typeChecks}}
 return 0;
}
`;
 const input=path.join(directory,'scope'+api+'.c'),binary=path.join(directory,'scope'+api);fs.writeFileSync(input,code);const built=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror',input,'-o',binary],{encoding:'utf8'});assert.equal(built.status,0,built.stderr||String(built.error));const result=spawnSync(binary,[],{encoding:'utf8'});assert.equal(result.status,0,result.stderr||String(result.error));
 }
 console.log('PASS actual D3D8/9 predefined C scopes: independent Wine/SDK RS tables, overlapping texture classes, sampler split, ALL-only resources/material/planes, independent frequency, known lights and all 266 transforms');
}finally{fs.rmSync(directory,{recursive:true,force:true});}
