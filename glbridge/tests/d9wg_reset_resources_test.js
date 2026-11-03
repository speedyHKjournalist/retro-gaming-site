'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-reset-graph-'));
function extract(s,name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(s);assert(m,name);let end=s.indexOf('{',m.index)+1,depth=1;for(;depth;end++){if(s[end]==='{')depth++;if(s[end]==='}')depth--;}return s.slice(m.index,end);}
try{for(const api of[8,9]){const source=fs.readFileSync(process.env['D'+api+'WG_RESET_FRONTEND']||root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8');fs.writeFileSync(dir+'/reset.c',`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
#include "${process.env.D9WG_RESET_POLICY||root+'/runtime/d9wg_reset.h'}"
typedef uint32_t UINT;typedef int32_t LONG;typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define D3DPOOL_DEFAULT 0
#define D8WG_MAX_STREAMS 16
#define D8WG_MAX_TEXTURE_STAGES 8
#define D9_MAX_STREAMS 16
#define D9_MAX_TEXTURE_BINDINGS 20
#define D9_MAX_RENDER_TARGETS 4
typedef struct{void*lpVtbl;}Interface;typedef struct{BOOL locked;}Level;typedef struct Resource Resource;typedef struct Surface Surface;
struct Resource{Interface iface;LONG refcount;UINT pool,level_count;BOOL locked;Level*levels;Resource*next_device_resource;};
typedef struct Chain{LONG refcount;UINT handle;struct Chain*next_device_chain;}D9SwapChain;
struct Surface{D9SwapChain*swap_chain;LONG refcount;BOOL locked,texture_child,auto_depth_stencil;Resource*texture,*cube_texture;};
typedef struct{Resource*buffer;}Stream;

typedef struct{BOOL in_scene;void*recording_state_block,*recording_result;D9SwapChain*additional_swap_chains;UINT additional_swap_chain_count,implicit_surface_count;Resource*vertex_buffers,*index_buffers,*texture_resources,*cube_resources,*cube_textures,*volume_resources,*volume_textures;Stream streams[16];Resource*index_buffer,*textures[20],*cube_bindings[20],*volume_bindings[20];Resource*render_target_texture,*render_target_cube;Surface*depth_surface,*implicit_back_buffer,*implicit_depth_stencil,*render_target_surfaces[4],*depth_stencil_surface;}Device;
typedef Device D${api}Device;typedef Resource D${api}VertexBuffer,D${api}IndexBuffer,D${api}Texture,D${api}CubeTexture,D${api}VolumeTexture;typedef Surface D${api}Surface;
static LONG InterlockedCompareExchange(LONG*p,LONG value,LONG compare){LONG old=*p;if(old==compare)*p=value;return old;}
${['reset_buffer_owned_refs',...(api===9?['surface_uses_private_bindings','reset_bound_surface','reset_surface_owned_refs']:[]),'reset_texture_owned_refs','device_has_reset_blockers'].map(n=>extract(source,n)).join('\n')}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Reset resource API ${api} line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
static void root_resource(Device*d,Resource*r,UINT kind){if(kind==0)d->vertex_buffers=r;else if(kind==1)d->index_buffers=r;else if(kind==2)d->texture_resources=r;else if(kind==3)d->cube_${api===8?'resources':'textures'}=r;else d->volume_${api===8?'resources':'textures'}=r;}
static UINT bind_resource(Device*d,Resource*r,Surface*view,UINT kind,UINT mode){UINT refs=0;if(!mode)return 0;
if(kind==0){d->streams[0].buffer=r;refs++;if(mode==2){d->streams[15].buffer=r;refs++;}}
else if(kind==1){d->index_buffer=r;refs++;}
else{UINT last=${api===8?7:19};${api===8?'d->textures[0]=r;':'if(kind==2)d->textures[0]=r;else if(kind==3)d->cube_bindings[0]=r;else d->volume_bindings[0]=r;'}refs++;if(mode==2){${api===8?'d->textures[last]=r;':'if(kind==2)d->textures[last]=r;else if(kind==3)d->cube_bindings[last]=r;else d->volume_bindings[last]=r;'}refs++;
if(kind<4){view->texture_child=kind==2;view->texture=kind==2?r:NULL;view->cube_texture=kind==3?r:NULL;
${api===8?'if(kind==2){d->render_target_texture=r;d->depth_surface=view;refs+=2;}else{d->render_target_cube=r;refs++;}':'d->render_target_surfaces[0]=view;d->render_target_surfaces[3]=view;refs+=2;if(kind==2){d->depth_stencil_surface=view;refs++;}'}
}}}return ${api===9?"kind==2||kind==3?0:refs":"refs"};}
int main(void){unsigned cases=0;Device d={0};CHECK(!device_has_reset_blockers(&d));cases++;
/* Wine Windows cases: CPU pools survive (including locked SYSTEMMEM/SCRATCH),
 * referenced DEFAULT objects block, and target bindings alone allow Reset. */
for(UINT kind=0;kind<5;kind++)for(UINT pool=0;pool<4;pool++)for(UINT mode=0;mode<3;mode++)for(UINT external=0;external<3;external++)for(UINT locked=0;locked<2;locked++){
Level levels[12]={0};Resource r={.pool=pool,.level_count=2,.levels=levels};Surface view={0};d=(Device){0};UINT owned=bind_resource(&d,&r,&view,kind,mode);if(!owned&&!external&&!mode)continue;r.refcount=(LONG)(owned+external);r.locked=locked;levels[kind==3?11:1].locked=locked;root_resource(&d,&r,kind);BOOL expected=pool==0&&(external>0||locked);Device before=d;Resource before_r=r;CHECK(device_has_reset_blockers(&d)==expected&&!memcmp(&d,&before,sizeof(d))&&!memcmp(&r,&before_r,sizeof(r)));cases++;
if(!expected){Resource tail={.pool=0,.refcount=1};r.next_device_resource=&tail;CHECK(device_has_reset_blockers(&d));cases++;}
}
${api===9?`D9SwapChain tail={.refcount=1},head={.next_device_chain=&tail};d=(Device){.additional_swap_chains=&head};CHECK(device_has_reset_blockers(&d));tail.refcount=0;CHECK(!device_has_reset_blockers(&d));head.next_device_chain=NULL;CHECK(!device_has_reset_blockers(&d));cases+=3;`:''}
d=(Device){.in_scene=1};CHECK(!device_has_reset_blockers(&d));d=(Device){.${api===8?'recording_state_block':'recording_result'}=(void*)(uintptr_t)1};CHECK(!device_has_reset_blockers(&d));d=(Device){.${api===8?'additional_swap_chain_count':'additional_swap_chains'}=${api===8?'1':'&(D9SwapChain){.refcount=1}'}};CHECK(device_has_reset_blockers(&d));cases++;
D9SwapChain implicit_chain={0};Surface back={0},depth={0};for(UINT own=0;own<2;own++)for(UINT external=0;external<3;external++){d=(Device){.implicit_back_buffer=&back,.implicit_depth_stencil=&depth};back=(Surface){.swap_chain=&implicit_chain};depth=(Surface){.refcount=(LONG)external,.auto_depth_stencil=1};${api===8?'d.implicit_surface_count=depth.refcount>0;d.depth_surface=own?&depth:NULL;':'d.depth_stencil_surface=own?&depth:NULL;'}CHECK(device_has_reset_blockers(&d)==(external>0));cases++;}
d=(Device){.implicit_back_buffer=&back};back=(Surface){.refcount=1};${api===8?'d.implicit_surface_count=1;':''}CHECK(device_has_reset_blockers(&d));cases++;
${api===8?'d=(Device){.implicit_surface_count=1};CHECK(device_has_reset_blockers(&d));cases++;':`
/* A standalone surface holds one backing-texture ref, even when multiple
 * bindings retain the same surface. External surface/container refs block. */
for(UINT legacy=0;legacy<2;legacy++)for(UINT bindings=1;bindings<=2;bindings++)for(UINT external_surface=0;external_surface<2;external_surface++)for(UINT external_texture=0;external_texture<2;external_texture++){
Level level={0};Resource r={.pool=0,.refcount=(LONG)(1+external_texture),.level_count=1,.levels=&level};D9SwapChain legacy_chain={.handle=7};Surface target={.swap_chain=legacy?&legacy_chain:NULL,.refcount=(LONG)external_surface,.texture=&r};d=(Device){.texture_resources=&r};d.render_target_surfaces[0]=&target;if(bindings==2)d.render_target_surfaces[3]=&target;CHECK(device_has_reset_blockers(&d)==(external_surface||external_texture));cases++;
}
`}
printf("PASS actual API ${api} Reset resource graph: %u observations; five resource families, four pools, mapped CPU resources, all binding classes/last slots, device-only targets, external implicit refs, tails and no preflight mutation\\n",cases);return 0;}
`);const b=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-I',root,dir+'/reset.c','-o',dir+'/reset'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+'/reset',[],{encoding:'utf8',timeout:10000});assert.equal(r.status,0,r.stderr+' signal='+r.signal);process.stdout.write(r.stdout);}}finally{fs.rmSync(dir,{recursive:true,force:true});}
