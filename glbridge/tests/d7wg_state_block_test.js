"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.D7WG_STATE_BLOCK_SOURCE||path.join(root,"ddrawproxy/d3d7_proxy.inc"),"utf8"),reference=require(path.resolve(root,"../../v86/tests/glbridge/fixtures/d3d7_native_state_blocks.json"));
function extract(name){const m=new RegExp("static (?:HRESULT WINAPI|BOOL|void|D3D7StateBlock \\*) ?"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let at=source.indexOf("{",m.index)+1,depth=1;for(;depth;++at){assert(at<source.length);if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return source.slice(m.index,at);}
const search=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",["-E","-Wp,-v","-x","c","-"],{input:"",encoding:"utf8"});assert.equal(search.status,0,search.stderr||String(search.error));const sdkPath=process.env.D9WG_D3D7_TYPES||search.stderr.split("\n").map(s=>path.join(s.trim(),"d3dtypes.h")).find(p=>fs.existsSync(p));assert(sdkPath,"MinGW D3D7 SDK header required");
const sdk=fs.readFileSync(sdkPath,"utf8"),types=['D3DRENDERSTATETYPE','D3DTRANSFORMSTATETYPE','D3DTEXTURESTAGESTATETYPE','D3DSTATEBLOCKTYPE','D3DTEXTUREMAGFILTER','D3DTEXTUREMINFILTER','D3DTEXTUREMIPFILTER'];
const enums=types.map(name=>{const m=new RegExp('typedef\\s+enum(?:\\s+\\w+)?\\s*\\{[^}]+\\}\\s*'+name+'\\s*;').exec(sdk);assert(m,name);return m[0];}).join('\n');
const {nativeScopeCases,legacyStateCases}=require("./d7wg_state_scope_cases.js"),scopeReference=require(require("./host_paths.js").testPath("fixtures/d3d7_native_state_scope.json"));
const fields=['RS_CULL','RS_SHADE','RS_FACTOR','RS_SPECULAR','RS_LIGHTING','RS_FOG','TSS_COLOR','TSS_COORD','TSS_TRANSFORM','WORLD','VIEW','PROJECTION','TEX_MATRIX','VIEWPORT','MATERIAL','CLIP_PLANE','TEXTURE'],input=[['during',0],['record_apply',0]].map(([phase,type])=>fields.map(field=>{const row=reference.cases.find(r=>r.type===type&&r.phase===phase&&r.field===field);assert(row);return row.words;}));
const names=['d3d7_render_state_is_vertex','d3d7_render_state_is_pixel','d3d7_render_state_valid','d3d_legacy_render_state_valid','d3d7_set_render_state','d3d7_transform_state_valid','d3d7_texture_stage_state_valid','d3d7_state_block_mark_type','d3d7_state_block_capture','d3d7_state_block_destroy','d3d7_find_state_block',...['Transform','Viewport','Material','RenderState','Texture','TextureStageState','ClipPlane'].flatMap(n=>['d3d7_device_Set'+n,'d3d7_device_Get'+n]),'d3d7_device_MultiplyTransform','d3d7_device_BeginStateBlock','d3d7_device_EndStateBlock','d3d7_device_ApplyStateBlock','d3d7_device_CaptureStateBlock','d3d7_device_DeleteStateBlock','d3d7_device_CreateStateBlock'];
// SetTextureStageState calls the pure filter validator.
names.splice(names.indexOf('d3d7_device_SetTextureStageState'),0,'d3d7_sampler_value');
const block=/struct D3D7StateBlock \{[\s\S]*?\n\};/.exec(source);assert(block);
const code=`#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "${path.join(root,'ddrawproxy/ddraw_protocol.h')}"
typedef int BOOL,WINBOOL,HRESULT;typedef uint32_t DWORD,UINT;typedef uint8_t BYTE;typedef float D3DVALUE;
#define DIRECT3D_VERSION 0x0700
${enums}
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define DDERR_INVALIDPARAMS ((int32_t)0x80070057u)
#define DDERR_OUTOFMEMORY (-2)
#define D3DERR_INITFAILED (-3)
#define D3DERR_UNSUPPORTEDTEXTUREFILTER (-4)
#define D3DERR_MATERIAL_SETDATA_FAILED (-5)
#define D3DERR_INBEGINSTATEBLOCK (-6)
#define D3DERR_NOTINBEGINSTATEBLOCK (-7)
#define D3DERR_INVALIDSTATEBLOCK (-8)
#define DDWG_TRACE_ERROR(x) (x)
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
#define D3D7_MAX_LIGHTS 8u
#define D3D7_MAX_RENDER_STATES 256u
#define D3D7_MAX_TEXTURE_STAGES 8u
#define D3D7_MAX_TEXTURE_STAGE_STATES 33u
#define D3D7_MAX_TRANSFORMS 24u
#define D3D7_MAX_CLIP_PLANES 6u
#define FAILED(x) ((x)<0)
typedef struct D3DMATRIX {float m[16];} D3DMATRIX;
typedef struct Color {float r,g,b,a;} Color;
typedef struct D3DMATERIAL7 {Color diffuse,ambient,specular,emissive;float power;} D3DMATERIAL7;
typedef struct D3DVIEWPORT7 {DWORD dwX,dwY,dwWidth,dwHeight;float dvMinZ,dvMaxZ;} D3DVIEWPORT7;
typedef struct D3DLIGHT7 {uint32_t words[26];} D3DLIGHT7;
#define D9WG_LIGHTING_SDK_LIGHT D3DLIGHT7
static unsigned allocations,frees,emissions,uploads;
static void *heap_alloc_zero(size_t n){allocations++;return calloc(1,n);}
static void heap_free(void *p){if(p){frees++;free(p);}}
#define D9WG_LIGHT_ALLOC(n) heap_alloc_zero(n)
#define D9WG_LIGHT_FREE(p) heap_free(p)
#include "${path.join(root,'runtime/d9wg_light_state.h')}"
typedef struct Owner {uint32_t device_handle;} Owner;
typedef struct IDirectDrawSurface7 {const void *lpVtbl;} IDirectDrawSurface7;
typedef struct DDSurface {const void *vtbl7;Owner *owner;uint32_t handle;UINT width,height,format;int refs;} DDSurface;
static const int g_surface_vtbl=1;
static void surface_AddRef(IDirectDrawSurface7 *p){DDSurface *s=(void*)p;if(s->refs<=0)abort();s->refs++;}
static void surface_Release(IDirectDrawSurface7 *p){DDSurface *s=(void*)p;if(s->refs<=0)abort();s->refs--;}
static DDSurface *surface_from_any(void *p,void *unused){(void)unused;return p;}
static BOOL format_is_depth(UINT format){return format==75;}
static BOOL sync_external_surface_for_read(DDSurface *p){(void)p;uploads++;return TRUE;}
static BOOL prepare_surface_for_host(DDSurface *p){(void)p;uploads++;return TRUE;}
static void rebind_host_surface_state(DDSurface *p){(void)p;uploads++;}
typedef struct D3D7StateBlock D3D7StateBlock;
${block[0]}
typedef struct D3D7Device {Owner *owner;DDSurface *render_target;DWORD render_states[256],rejected_render_values[256];BYTE rejected_render_set[256];DWORD texture_stage_states[8][33];DDSurface *textures[8];D3DMATRIX transforms[24];D3DMATERIAL7 material;D3DVIEWPORT7 viewport;float clip_planes[6][4];D3DLIGHT7 lights[8];BOOL light_set[8],light_enabled[8];D9WGLightState *extra_lights;D3D7StateBlock *recording_state_block,*state_blocks;DWORD next_state_block_handle;} D3D7Device;
typedef D3D7Device IDirect3DDevice7;
static D3D7Device *d3d7_device_from_iface(D3D7Device *p){return p;}
static BOOL emit_command(uint32_t op,const void *p,uint32_t n){(void)op;(void)p;(void)n;emissions++;return TRUE;}
static BOOL d3d7_emit_render_state(D3D7Device *d,DWORD state,DWORD value){(void)d;(void)state;(void)value;emissions++;return TRUE;}
static BOOL d3d7_emit_sampler_state(D3D7Device *d,DWORD stage,DWORD state,DWORD value){(void)d;(void)stage;(void)state;(void)value;emissions++;return TRUE;}
static BOOL d3d7_emit_texture_stage_state(D3D7Device *d,DWORD stage,DWORD state,DWORD value){(void)d;(void)stage;(void)state;(void)value;emissions++;return TRUE;}
static void d3d7_multiply_matrix(const D3DMATRIX *left,const D3DMATRIX *right,D3DMATRIX *out){D3DMATRIX result={0};for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k)result.m[i*4+j]+=left->m[i*4+k]*right->m[k*4+j];*out=result;}
static HRESULT d3d7_device_SetLight(IDirect3DDevice7 *d,DWORD i,D3DLIGHT7 *p){(void)d;(void)i;(void)p;return 0;}
static HRESULT d3d7_device_LightEnable(IDirect3DDevice7 *d,DWORD i,BOOL p){(void)d;(void)i;(void)p;return 0;}
${names.map(extract).join('\n')}
static DDSurface textures[2];
static const uint32_t input[2][17][17]={${input.map(variant=>'{'+variant.map(words=>'{'+words.map(n=>n+'u').join(',')+'}').join(',')+'}').join(',')}};
static const struct Native {unsigned type,phase,field,count;uint32_t words[17];} native[]={${reference.cases.map(r=>'{'+[r.type,['restore','recapture','during','end','record_apply','record_capture'].indexOf(r.phase),fields.indexOf(r.field),r.words.length].join(',')+',{'+r.words.map(n=>n+'u').join(',')+'}}').join(',')}};
#define CHECK(x) do{if(!(x)){fprintf(stderr,"D3D7 state block line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
static int seed(D3D7Device *d,unsigned variant){const DWORD rs[]={22,9,60,29,137,28},tss[]={1,11,24},matrices[]={1,2,3,16};for(unsigned i=0;i<6;++i)CHECK(d3d7_device_SetRenderState(d,rs[i],input[variant][i][0])==0);for(unsigned i=0;i<3;++i)CHECK(d3d7_device_SetTextureStageState(d,0,tss[i],input[variant][i+6][0])==0);D3DMATRIX matrix;for(unsigned i=0;i<4;++i){memcpy(&matrix,input[variant][i+9],64);CHECK(d3d7_device_SetTransform(d,matrices[i],&matrix)==0);}D3DVIEWPORT7 viewport;memcpy(&viewport,input[variant][13],24);CHECK(d3d7_device_SetViewport(d,&viewport)==0);D3DMATERIAL7 material;memcpy(&material,input[variant][14],68);CHECK(d3d7_device_SetMaterial(d,&material)==0);float plane[4];memcpy(plane,input[variant][15],16);CHECK(d3d7_device_SetClipPlane(d,5,plane)==0);CHECK(d3d7_device_SetTexture(d,0,(void*)&textures[variant].vtbl7)==0);return 0;}
static int verify(D3D7Device *d,unsigned type,unsigned phase){const DWORD rs[]={22,9,60,29,137,28},tss[]={1,11,24},matrices[]={1,2,3,16};for(unsigned n=0;n<170;++n){const struct Native *r=&native[n];if(r->type!=type||r->phase!=phase)continue;uint32_t got[17]={0};HRESULT hr;if(r->field<6)hr=d3d7_device_GetRenderState(d,rs[r->field],got);else if(r->field<9)hr=d3d7_device_GetTextureStageState(d,0,tss[r->field-6],got);else if(r->field<13){D3DMATRIX matrix;hr=d3d7_device_GetTransform(d,matrices[r->field-9],&matrix);memcpy(got,&matrix,64);}else if(r->field==13){D3DVIEWPORT7 vp;hr=d3d7_device_GetViewport(d,&vp);memcpy(got,&vp,24);}else if(r->field==14){D3DMATERIAL7 material;hr=d3d7_device_GetMaterial(d,&material);memcpy(got,&material,68);}else if(r->field==15){float plane[4];hr=d3d7_device_GetClipPlane(d,5,plane);memcpy(got,plane,16);}else{IDirectDrawSurface7 *texture;hr=d3d7_device_GetTexture(d,0,&texture);got[0]=texture==(void*)&textures[0].vtbl7?1:texture==(void*)&textures[1].vtbl7?2:texture?3:0;if(texture)surface_Release(texture);}if(hr||memcmp(got,r->words,r->count*4)){fprintf(stderr,"D3D7 state block line %u: native type=%u phase=%u field=%u\\n",__LINE__,type,phase,r->field);return 1;}}return 0;}
${nativeScopeCases(scopeReference)}
${legacyStateCases(fs.readFileSync(process.env.D7WG_LEGACY_STATE_SOURCE||path.join(root,"ddrawproxy/d3d_legacy_proxy.inc"),"utf8"))}
int main(void){Owner owner={7};DDSurface target={&g_surface_vtbl,&owner,3,128,128,22,1};D3D7Device device={0};device.owner=&owner;device.render_target=&target;device.next_state_block_handle=1;device.render_states[D3DRENDERSTATE_TEXTUREMAPBLEND]=D3D7TBLEND_MODULATE;for(unsigned stage=0;stage<8;++stage){device.texture_stage_states[stage][D3DTSS_MAGFILTER]=D3DTFG_POINT;device.texture_stage_states[stage][D3DTSS_MINFILTER]=D3DTFN_POINT;device.texture_stage_states[stage][D3DTSS_MIPFILTER]=D3DTFP_NONE;}for(unsigned i=0;i<2;++i)textures[i]=(DDSurface){&g_surface_vtbl,&owner,i+1,2,2,22,1};
 for(unsigned type=1;type<=3;++type){DWORD block;CHECK(!seed(&device,0));CHECK(d3d7_device_CreateStateBlock(&device,type,&block)==0);CHECK(!seed(&device,1));CHECK(d3d7_device_ApplyStateBlock(&device,block)==0);CHECK(!verify(&device,type,0));CHECK(!seed(&device,1)&&d3d7_device_CaptureStateBlock(&device,block)==0&&!seed(&device,0)&&d3d7_device_ApplyStateBlock(&device,block)==0&&!verify(&device,type,1));CHECK(d3d7_device_DeleteStateBlock(&device,block)==0);}
 CHECK(!seed(&device,0));unsigned sent=emissions,uploaded=uploads;CHECK(d3d7_device_BeginStateBlock(&device)==0&&!seed(&device,1)&&!verify(&device,0,2));CHECK(d3d7_device_SetTexture(&device,0,(void*)&textures[1].vtbl7)==0&&textures[1].refs==2);CHECK(emissions==sent&&uploads==uploaded);DWORD block;CHECK(d3d7_device_EndStateBlock(&device,&block)==0&&!verify(&device,0,3)&&emissions==sent&&uploads==uploaded);CHECK(d3d7_device_ApplyStateBlock(&device,block)==0&&!verify(&device,0,4));CHECK(!seed(&device,0)&&d3d7_device_CaptureStateBlock(&device,block)==0&&!seed(&device,1)&&d3d7_device_ApplyStateBlock(&device,block)==0&&!verify(&device,0,5));CHECK(d3d7_device_DeleteStateBlock(&device,block)==0);
 D3DMATRIX matrix,scale={0};memcpy(&matrix,input[1][9],64);for(unsigned i=0;i<4;++i)scale.m[i*5]=1;scale.m[0]=3;CHECK(d3d7_device_BeginStateBlock(&device)==0&&d3d7_device_SetTransform(&device,1,&matrix)==0&&d3d7_device_MultiplyTransform(&device,1,&scale)==0);scale.m[0]=.5f;CHECK(d3d7_device_MultiplyTransform(&device,1,&scale)==0&&device.recording_state_block->transforms[1].m[0]==3&&device.recording_state_block->transforms[1].m[12]==6);CHECK(d3d7_device_SetTextureStageState(&device,0,D3DTSS_MAGFILTER,0)==D3DERR_UNSUPPORTEDTEXTUREFILTER);CHECK(d3d7_device_EndStateBlock(&device,&block)==0&&d3d7_device_ApplyStateBlock(&device,block)==0&&d3d7_device_DeleteStateBlock(&device,block)==0);
 CHECK(d3d7_device_SetRenderState(&device,D3DRENDERSTATE_WRAP0,3)==0&&d3d7_device_BeginStateBlock(&device)==0&&d3d7_device_SetRenderState(&device,D3DRENDERSTATE_WRAP0,3)==0&&d3d7_device_SetRenderState(&device,D3DRENDERSTATE_WRAPU,0)==DDERR_INVALIDPARAMS&&device.render_states[D3DRENDERSTATE_WRAP0]==3);CHECK(d3d7_device_EndStateBlock(&device,&block)==0&&d3d7_device_ApplyStateBlock(&device,block)==0&&device.render_states[D3DRENDERSTATE_WRAP0]==3&&d3d7_device_DeleteStateBlock(&device,block)==0);
 CHECK(native_scope_cases(&device)==0&&legacy_state_cases(&device)==0);
 CHECK(d3d7_device_SetTexture(&device,0,NULL)==0&&textures[0].refs==1&&textures[1].refs==1&&allocations==frees);return 0;}
`;
const directory=fs.mkdtempSync(path.join(os.tmpdir(),"d7wg-state-block-"));try{const file=path.join(directory,"state.c"),binary=path.join(directory,"state");fs.writeFileSync(file,code);const built=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",file,"-o",binary],{encoding:"utf8"});assert.equal(built.status,0,built.stderr||String(built.error));const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr||String(run.error));console.log("PASS actual D3D7 state-block C: 170 ordinary and 1052 full-scope/retail native observations, independent SDK enums, Begin/Get/End isolation, Apply/Capture scopes, ordered Multiply, rejected obsolete Device7 states and retained legacy interface calls, exact texture ownership and zero early uploads");}finally{fs.rmSync(directory,{recursive:true,force:true});}
