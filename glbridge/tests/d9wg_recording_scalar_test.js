"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.D9WG_RECORD_SCALAR_SOURCE||path.join(root,"d3d9proxy/d3d9_proxy.c"),"utf8"),ref=require(path.resolve(root,"../../v86/tests/glbridge/fixtures/d3d_state_block_scope_reference.json")).apis[9];
function extract(name){const m=new RegExp("static (?:HRESULT(?: WINAPI)?|BOOL|void|float WINAPI|const float \\*) ?"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let at=source.indexOf("{",m.index)+1,depth=1;for(;depth;++at){assert(at<source.length);if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return source.slice(m.index,at);}
const recordNames=["render_state","texture_stage_state","sampler_state","transform","material","clip_plane","npatch_mode","stream_frequency","viewport","scissor","constant"].map(n=>"state_block_record_"+n),names=[...recordNames,...(source.includes("static HRESULT device_set_live_transform(")?["device_set_live_transform"]:[]),"state_block_transform_source","sampler_state_slot","constant_range_valid",...(source.includes("static BOOL constant_vector_equal(")?["constant_vector_equal"]:[]),...(source.includes("static HRESULT device_set_live_viewport(")?["device_set_live_viewport"]:[]),"set_constant_f","set_constant_i","set_constant_b",...['transform','viewport','render_state','texture_stage_state','material','clip_plane','sampler_state','scissor_rect','stream_source_freq'].flatMap(n=>['device_set_'+n,'device_get_'+n]),'device_multiply_transform','device_set_npatch_mode','device_get_npatch_mode'];
const fields=n=>`DWORD render_states[256],texture_stage_states[8][33],sampler_states[20][14];float transforms[${n}][16],clip_planes[6][4],npatch_segments;D3DMATERIAL9 material;D3DVIEWPORT9 viewport;RECT scissor_rect;Stream streams[16];float vs_const_f[256][4],ps_const_f[224][4];int vs_const_i[16][4],ps_const_i[16][4];BOOL vs_const_b[16],ps_const_b[16];`;
const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "${path.join(root,'d3d9proxy/d3d9_protocol.h')}"
typedef int BOOL,WINBOOL,HRESULT,D3DTRANSFORMSTATETYPE,D3DRENDERSTATETYPE,D3DTEXTURESTAGESTATETYPE,D3DSAMPLERSTATETYPE;typedef uint32_t UINT,DWORD;typedef int32_t LONG;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define TRACE_REFUSE(x) (x)
#define TRACE(...) ((void)0)
#define HOSTLOG_REFUSED(...) ((void)0)
#define PURE_DEVICE_REFUSE(...) ((void)0)
#define CopyMemory memcpy
#define SetRect(p,a,b,c,d) (*(p)=(RECT){a,b,c,d})
#define D9_MAX_RENDER_STATES 256u
#define D9_MAX_TEXTURE_STAGES 8u
#define D9_MAX_TEXTURE_STAGE_STATES 33u
#define D9_MAX_SAMPLER_STATES 14u
#define D9_MAX_SAMPLERS 20u
#define D9_MAX_TRANSFORMS 512u
#define D9_MAX_CLIP_PLANES 6u
#define D9_MAX_STREAMS 16u
#define D9_MAX_VS_CONST_F 256u
#define D9_MAX_PS_CONST_F 224u
#define D9_MAX_CONST_I 16u
#define D9_MAX_CONST_B 16u
#define D9_PATCH_MAX_SEGMENTS 64u
#define D9FOURCC_NVDB 0x4244564eu
#define D9_STREAMSOURCE_FREQUENCY_MASK 0x3fffffffu
#define D3DSTREAMSOURCE_INDEXEDDATA 0x40000000u
#define D3DSTREAMSOURCE_INSTANCEDATA 0x80000000u
#define D3DVERTEXTEXTURESAMPLER0 257u
enum {D3DSBT_ALL=1,D3DSBT_PIXELSTATE=2,D3DSBT_VERTEXSTATE=3,${Object.entries(ref.sdkEnums).map(([n,v])=>n+'='+v+'u').join(',')}};
#define D9WG_STATE_BLOCK_API 9
#include "${path.join(root,'runtime/d9wg_state_block_scope.h')}"
typedef struct D3DMATRIX {float m[16];} D3DMATRIX;
typedef struct D3DVIEWPORT9 {DWORD X,Y,Width,Height;float MinZ,MaxZ;} D3DVIEWPORT9;
typedef struct Color {float r,g,b,a;} Color;
typedef struct D3DMATERIAL9 {Color Diffuse,Ambient,Specular,Emissive;float Power;} D3DMATERIAL9;
typedef struct RECT {LONG left,top,right,bottom;} RECT;
typedef struct Stream {void *buffer;UINT frequency;} Stream;
typedef struct D9Surface {UINT width,height;} D9Surface;
typedef struct D9StateBlock {${fields(266)}BOOL has_render_state[256],has_texture_stage_state[8][33],has_sampler_state[20][14],has_transform[266],has_material,has_clip_plane[6],has_npatch_mode,has_stream_frequency[16],has_viewport,has_scissor,has_vs_const_f[256],has_ps_const_f[224],has_vs_const_i[16],has_ps_const_i[16],has_vs_const_b[16],has_ps_const_b[16];} D9StateBlock;
typedef struct D9Device {${fields(512)}BOOL scissor_set;struct {UINT BackBufferWidth,BackBufferHeight;} present;D9StateBlock *recording_result;uint32_t handle;D9Surface *render_target_surfaces[1];struct {UINT Width,Height;} display_mode;} D9Device;
typedef D9Device IDirect3DDevice9;
static D9Device *device_from_iface(D9Device *d){return d;}
static unsigned emissions;
static BOOL emit_command(uint32_t op,const void *p,uint32_t bytes){(void)op;(void)p;(void)bytes;emissions++;return TRUE;}
static BOOL emit_shader_constants(D9Device *d,uint32_t op,UINT start,UINT count,const void *p,UINT bytes){(void)d;(void)op;(void)start;(void)count;(void)p;(void)bytes;emissions++;return TRUE;}
${names.filter(n=>n!=="state_block_transform_source"||source.includes("static const float *state_block_transform_source(")).map(extract).join('\n')}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"D3D9 scalar recording line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
int main(void){(void)g_pixel_state_render_states;(void)g_vertex_state_render_states;D9Device device={0};D9StateBlock block={0};device.display_mode.Width=device.display_mode.Height=64;device.viewport=(D3DVIEWPORT9){0,0,64,64,0,1};for(unsigned i=0;i<16;++i)device.streams[i].frequency=1;D3DMATRIX identity={{0}},got;for(unsigned i=0;i<4;++i)identity.m[i*5]=1;for(unsigned i=0;i<512;++i)memcpy(device.transforms[i],&identity,64);device.recording_result=&block;D9Device before=device;DWORD value;UINT frequency;
 for(unsigned i=0;i<32;++i){CHECK(device_set_render_state(&device,7,i)==0&&device_get_render_state(&device,7,&value)==0&&value==0);CHECK(block.has_render_state[7]&&block.render_states[7]==i);}
 CHECK(device_set_texture_stage_state(&device,3,9,123)==0&&device_get_texture_stage_state(&device,3,9,&value)==0&&value==0);CHECK(block.has_texture_stage_state[3][9]&&block.texture_stage_states[3][9]==123);
 CHECK(device_set_sampler_state(&device,260,1,17)==0&&device_get_sampler_state(&device,260,1,&value)==0&&value==0);CHECK(block.has_sampler_state[19][1]&&block.sampler_states[19][1]==17);CHECK(device_set_sampler_state(&device,256,1,3)==-1);
 D3DVIEWPORT9 vp={1,2,32,30,.25f,.75f},read_vp;CHECK(device_set_viewport(&device,&vp)==0&&device_get_viewport(&device,&read_vp)==0&&read_vp.Width==64&&block.viewport.Width==32);vp.Width=64;CHECK(device_set_viewport(&device,&vp)==-1&&block.viewport.Width==32);
 D3DMATERIAL9 material={0},read_material;material.Diffuse.r=.125f;CHECK(device_set_material(&device,&material)==0&&device_get_material(&device,&read_material)==0&&read_material.Diffuse.r==0&&block.material.Diffuse.r==.125f);
 RECT rect={1,2,30,31},read_rect;CHECK(device_set_scissor_rect(&device,&rect)==0&&device_get_scissor_rect(&device,&read_rect)==0&&read_rect.left==0&&block.scissor_rect.left==1);
 float plane[4]={1,-2,.5f,3},read_plane[4];CHECK(device_set_clip_plane(&device,5,plane)==0&&device_get_clip_plane(&device,5,read_plane)==0&&read_plane[0]==0&&!memcmp(block.clip_planes[5],plane,16));CHECK(device_set_clip_plane(&device,6,plane)==-1);
 CHECK(device_set_npatch_mode(&device,100)==0&&device_get_npatch_mode(&device)==0&&block.npatch_segments==64);
 CHECK(device_set_stream_source_freq(&device,3,D3DSTREAMSOURCE_INSTANCEDATA|7)==0&&device_get_stream_source_freq(&device,3,&frequency)==0&&frequency==1&&block.streams[3].frequency==(D3DSTREAMSOURCE_INSTANCEDATA|7));CHECK(device_set_stream_source_freq(&device,0,7)==-1);
 for(unsigned slot=0;slot<266;++slot){uint32_t state=d9wg_state_block_transform_state(slot);D3DMATRIX m=identity;m.m[0]=slot+1;CHECK(device_set_transform(&device,state,&m)==0&&device_get_transform(&device,state,&got)==0&&!memcmp(&got,&identity,64));CHECK(block.has_transform[slot]&&block.transforms[slot][0]==slot+1);}
 /* SetTransform is recorded, but MultiplyTransform reads and writes live state.
  * Noncommutative scale/translation fixes order: pMatrix * current State. */
 CHECK(!emissions&&!memcmp(&device,&before,sizeof(device)));
 D3DMATRIX scale=identity;scale.m[0]=2;
 D9StateBlock saved_block=block;
 CHECK(device_multiply_transform(&device,511,&scale)==0&&device.transforms[511][0]==2);
 CHECK(device_multiply_transform(&device,511,&scale)==0&&device.transforms[511][0]==4);
 CHECK(!memcmp(&block,&saved_block,sizeof(block))&&emissions==2);
 D3DMATRIX translation=identity;translation.m[12]=3;
 CHECK(device_multiply_transform(&device,511,&translation)==0);
 CHECK(device_get_transform(&device,511,&got)==0&&got.m[0]==4&&got.m[12]==12);
 CHECK(!memcmp(&block,&saved_block,sizeof(block))&&emissions==3);
 before=device;
 const UINT invalid[]={0,1,4,15,24,255,512,0xffffffffu};for(unsigned i=0;i<8;++i){CHECK(device_set_transform(&device,invalid[i],&scale)==-1&&device_multiply_transform(&device,invalid[i],&scale)==-1);memset(&got,0xcc,64);CHECK(device_get_transform(&device,invalid[i],&got)==-1);for(unsigned b=0;b<64;++b)CHECK(((uint8_t*)&got)[b]==0xcc);}
 float floats[2][4]={{.125f,.25f,.5f,.75f},{-.125f,-.25f,-.5f,1}};int ints[2][4]={{-1,2,-3,4},{5,-6,7,-8}};WINBOOL bools[2]={7,0};for(unsigned pixel=0;pixel<2;++pixel){UINT tail=pixel?222:254;CHECK(set_constant_f(&device,pixel,tail,&floats[0][0],2)==0);CHECK((pixel?block.ps_const_f:block.vs_const_f)[tail+1][3]==1);CHECK(set_constant_f(&device,pixel,tail+1,&floats[0][0],1)==0);CHECK(set_constant_i(&device,pixel,14,&ints[0][0],2)==0);CHECK(set_constant_b(&device,pixel,14,bools,2)==0);CHECK(set_constant_f(&device,pixel,tail+1,&floats[0][0],2)==-1&&set_constant_i(&device,pixel,15,&ints[0][0],2)==-1);CHECK((pixel?block.has_ps_const_f: block.has_vs_const_f)[tail+1]&&((pixel?block.ps_const_f:block.vs_const_f)[tail+1][3]==.75f));CHECK((pixel?block.ps_const_b:block.vs_const_b)[14]==TRUE&&(pixel?block.ps_const_b:block.vs_const_b)[15]==FALSE);CHECK(!memcmp((pixel?block.ps_const_i:block.vs_const_i)[15],ints[1],16));}
 CHECK(block.has_material&&block.has_viewport&&block.has_scissor&&block.has_clip_plane[5]&&block.has_npatch_mode&&block.has_stream_frequency[3]);CHECK(emissions==3&&!memcmp(&device,&before,sizeof(device)));return 0;}
`;
const directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-record-scalar-"));try{const file=path.join(directory,"record.c"),binary=path.join(directory,"record");fs.writeFileSync(file,code);const built=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",file,"-o",binary],{encoding:"utf8"});assert.equal(built.status,0,built.stderr||String(built.error));const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr||String(run.error));console.log("PASS actual D3D9 scalar C recording: live queries, all 266 matrices, ordered Multiply, RS/TSS/sampler/viewport/material/planes/scissor/NPatch/frequency, six constant banks, invalid atomicity and live Multiply emissions");}finally{fs.rmSync(directory,{recursive:true,force:true});}
