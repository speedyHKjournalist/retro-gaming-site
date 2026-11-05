"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.D8WG_DEVICE_COMMON_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8");
function extract(name){const start=source.indexOf("HRESULT WINAPI "+name+"(" );assert(start>=0);let end=source.indexOf("{",start)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(start,end);}
const dir=fs.mkdtempSync(path.join(os.tmpdir(),"device8-common-"));
try{
fs.writeFileSync(dir+"/test.c",`
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef uint32_t UINT,DWORD;typedef int HRESULT,BOOL;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define E_OUTOFMEMORY (-2)
#define D3DSBT_ALL 1
#define D3DSBT_PIXELSTATE 2
#define D3DSBT_VERTEXSTATE 3
#define D9_MAX_LIGHTS 8
#define D9_STATE_BLOCK_TRANSFORMS 266
#define CopyMemory memcpy
typedef struct D9WGLightState{struct D9WGLightState*next;UINT index;BOOL set,light_mask,enable_mask;}D9WGLightState;
typedef struct D9StateBlock{
 BOOL has_render_state[256],has_texture_stage_state[8][33],has_sampler_state[20][14],has_transform[266],has_texture[20];
 BOOL has_palette,has_material,has_clip_plane[6],has_light[8],has_light_enable[8],has_viewport;
 BOOL has_vertex_shader,has_pixel_shader,has_vs_const_f[256],has_ps_const_f[224],has_vertex_format,has_stream[16],has_indices;
 D9WGLightState*extra_lights;
}D9StateBlock;
typedef struct IDirect3DDevice9{const void*lpVtbl;UINT api,reset_epoch;BOOL set[8];struct{BOOL EnableAutoDepthStencil;}present;D9StateBlock*recording_result;D9WGLightState*extra_lights;}IDirect3DDevice9;
typedef IDirect3DDevice9 D9Device;
static int g_device_vtbl;static D9StateBlock original;static UINT captures,allocations,fail_at;
static D9Device*device_from_iface(IDirect3DDevice9*d){return d;}
static UINT device_compatibility_api(D9Device*d){return d->api;}
static BOOL device_light_is_set(D9Device*d,UINT i){return d->set[i];}
static D9StateBlock*state_block_handle8(D9Device*d,DWORD token){return d->api==8&&token==71?&original:NULL;}
static D9WGLightState*d9wg_light_get(D9WGLightState**head,UINT i,BOOL create){
 for(D9WGLightState*p=*head;p;p=p->next)if(p->index==i)return p;
 if(!create||++allocations==fail_at)return NULL;
 D9WGLightState*p=calloc(1,sizeof(*p));assert(p);p->index=i;p->next=*head;*head=p;return p;
}
static void state_block_capture(D9StateBlock*b){assert(b);++captures;}
${extract("d9wg_d3d9_core_record_common_state8")}
${extract("d9wg_d3d9_core_device_state8")}
static void clear(D9WGLightState**p){while(*p){D9WGLightState*n=(*p)->next;free(*p);*p=n;}}
int main(void){UINT checks=0;
 for(UINT seed=0;seed<128;++seed)for(UINT type=0;type<=3;++type)for(UINT incremental=0;incremental<2;++incremental){
  D9StateBlock recorded={0},expected={0};memset(&original,0,sizeof(original));
  D9Device d={.lpVtbl=&g_device_vtbl,.api=8,.recording_result=&recorded};
  for(UINT i=0;i<8;++i)d.set[i]=(seed>>i)&1;
  D9WGLightState live[3]={{.index=37,.set=TRUE},{.index=700,.set=FALSE},{.index=UINT32_MAX,.set=TRUE}};
  live[0].next=live+1;live[1].next=live+2;d.extra_lights=live;
  if(incremental){
   BOOL*words=(BOOL*)&original;UINT n=(UINT)((char*)(&original.has_indices+1)-(char*)&original)/sizeof(BOOL);
   for(UINT i=0;i<n;++i)words[i]=(i*17+seed)%11==0;
   expected=original;
   D9WGLightState*p=d9wg_light_get(&original.extra_lights,123,TRUE);p->light_mask=TRUE;
  }
  if(type==1||type==2){for(UINT i=0;i<8;++i)expected.has_ps_const_f[i]=TRUE;expected.has_pixel_shader=TRUE;}
  if(type==1||type==3){for(UINT i=0;i<96;++i)expected.has_vs_const_f[i]=TRUE;expected.has_vertex_shader=expected.has_vertex_format=TRUE;for(UINT i=0;i<8;++i)expected.has_light[i]=expected.has_light_enable[i]=d.set[i];}
  if(type==1){for(UINT i=0;i<8;++i)expected.has_texture[i]=TRUE;for(UINT i=0;i<16;++i)expected.has_stream[i]=TRUE;for(UINT i=0;i<266;++i)expected.has_transform[i]=TRUE;for(UINT i=0;i<6;++i)expected.has_clip_plane[i]=TRUE;expected.has_palette=expected.has_material=expected.has_viewport=expected.has_indices=TRUE;}
  UINT before=captures;assert(!d9wg_d3d9_core_record_common_state8(&d,type,incremental?71:0));assert(captures==before+1);
  assert(!memcmp(&recorded,&expected,(char*)&recorded.extra_lights-(char*)&recorded));
  UINT count=0;for(D9WGLightState*p=recorded.extra_lights;p;p=p->next){++count;if(p->index==123)assert(p->light_mask&&!p->enable_mask);else assert((p->index==37||p->index==UINT32_MAX)&&p->light_mask&&p->enable_mask);}
  assert(count==incremental+((type==1||type==3)?2u:0u));
  clear(&recorded.extra_lights);clear(&original.extra_lights);checks+=5;
 }
 D9StateBlock recorded={0};D9Device d={.lpVtbl=&g_device_vtbl,.api=8,.recording_result=&recorded,.reset_epoch=99,.present={TRUE}};
 UINT epoch=1;BOOL recording=FALSE,depth=FALSE;assert(!d9wg_d3d9_core_device_state8(&d,&epoch,&recording,&depth)&&epoch==99&&recording&&depth);
 for(UINT invalid=0;invalid<5;++invalid){D9Device bad=d;if(invalid==0)bad.api=9;if(invalid==1)bad.lpVtbl=NULL;if(invalid==2)bad.recording_result=NULL;UINT before=captures;
  assert(d9wg_d3d9_core_record_common_state8(&bad,invalid==3?4:1,invalid==4?72:0)==D3DERR_INVALIDCALL&&captures==before);checks++;
 }
 D9WGLightState light={.index=123,.set=TRUE};d.extra_lights=&light;fail_at=allocations+1;UINT before=captures;
 assert(d9wg_d3d9_core_record_common_state8(&d,3,0)==E_OUTOFMEMORY&&captures==before&&!recorded.extra_lights);
 d.api=9;epoch=7;recording=depth=FALSE;assert(d9wg_d3d9_core_device_state8(&d,&epoch,&recording,&depth)==D3DERR_INVALIDCALL&&epoch==7&&!recording&&!depth);
 printf("PASS actual Device8 common snapshot selection: %u checks, 1024 predefined/incremental masks, sparse lights/OOM, 96VS/8PS scopes, Reset epoch/recording observation; shared capture engine separately covered\\n",checks);
}
`);
const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/test.c","-o",dir+"/test"],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);
const run=spawnSync(dir+"/test",[],{encoding:"utf8",timeout:30000});assert.equal(run.status,0,run.stderr+" signal="+run.signal);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
