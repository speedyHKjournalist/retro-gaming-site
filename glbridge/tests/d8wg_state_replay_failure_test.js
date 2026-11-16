"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"raw8-replay-"));
const source=fs.readFileSync(process.env.D8WG_REPLAY_SOURCE||root+"/d3d8proxy/d3d8_core_device.c","utf8");
function extract(name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){assert(end<source.length);depth+=(source[end]==="{")-(source[end]==="}");}return source.slice(m.index,end);}
const structure=name=>source.match(new RegExp("typedef struct "+name+" \\{[\\s\\S]*?\\} "+name+";"))[0];
const header=fs.readFileSync(root+"/d3d9proxy/d3d9_core.h","utf8").match(/enum D9WGStateBlockApplyGroup8 \{[\s\S]*?typedef HRESULT \(WINAPI \*D9WGStateBlockApply8\)[\s\S]*?;/)[0];
const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef uint32_t UINT,DWORD;typedef uint8_t BYTE;typedef int32_t HRESULT;typedef int BOOL,D3DRENDERSTATETYPE,D3DTEXTURESTAGESTATETYPE;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
#define D3DERR_INVALIDCALL ((HRESULT)0x8876086cu)
#define D3DERR_DRIVERINTERNALERROR ((HRESULT)0x88760827u)
#define E_OUTOFMEMORY ((HRESULT)0x8007000eu)
#define D9WG_TEXTURE_STAGE_STATES8 32
${header}
struct IDirect3DDevice9{int value;};struct IDirect3DVertexShader9{int value;};struct IDirect3DPixelShader9{int value;};
typedef struct D8CoreShader {UINT refs;void*program,*declaration;} D8CoreShader;
${structure("D8CoreRawState")}
${structure("D8CoreBlock")}
typedef struct Interface {int value;} IDirect3DDevice8;
typedef struct Methods {HRESULT(*set_pixel_shader)(struct IDirect3DDevice9*,struct IDirect3DPixelShader9*);} Methods;
typedef struct D8CoreDevice {IDirect3DDevice8 iface;struct IDirect3DDevice9*backend;const Methods*methods;D8CoreRawState live;D8CoreBlock*blocks,*recording;UINT refs;} D8CoreDevice;
static D8CoreDevice*active;static UINT calls,fail_at,drop_block;static HRESULT injected;
static D8CoreDevice*core_device8(IDirect3DDevice8*p){return (D8CoreDevice*)p;}
static UINT core_device8_AddRef(IDirect3DDevice8*p){return ++core_device8(p)->refs;}
static UINT core_device8_Release(IDirect3DDevice8*p){assert(core_device8(p)->refs>1);return --core_device8(p)->refs;}
// Provider holds are explicit test shims. Production lifecycle and actual
// backend shader storage are covered by the Device8 lifetime and SDK probes.
static void core_shader8_assign(D8CoreShader**slot,D8CoreShader*next){D8CoreShader*old=*slot;if(next){assert(next->refs);next->refs++;}*slot=next;if(old){assert(old->refs);old->refs--;}}
${extract("core_raw8_release")}
${extract("core_raw8_capture")}
${extract("core_block8_link")}
static HRESULT step(void){assert(active->refs==2);return ++calls==fail_at?injected:D3D_OK;}
static HRESULT d9wg_d3d9_core_set_render_state8(struct IDirect3DDevice9*b,UINT s,DWORD v){(void)b;assert(s<256&&v);return step();}
static HRESULT d9wg_d3d9_core_set_texture_stage_state8(struct IDirect3DDevice9*b,UINT s,UINT t,DWORD v){(void)b;assert(s<8&&t<32&&v);return step();}
static HRESULT pixel(struct IDirect3DDevice9*b,struct IDirect3DPixelShader9*p){(void)b;(void)p;return step();}
static HRESULT d9wg_d3d9_core_set_vertex_shader8(struct IDirect3DDevice9*b,void*d,struct IDirect3DVertexShader9*p){(void)b;(void)d;(void)p;return step();}
static HRESULT d9wg_d3d9_core_set_fvf8(struct IDirect3DDevice9*b,DWORD f){(void)b;(void)f;return step();}
${extract("core_device8_SetRenderState")}
${extract("core_device8_SetTextureStageState")}
${extract("core_shader8_bind")}
${structure("D8CoreApplyContext")}
${extract("core_block8_apply_raw")}
static HRESULT d9wg_d3d9_core_apply_state_block_frontend8(struct IDirect3DDevice9*b,DWORD token,D9WGStateBlockApply8 apply,void*ctx){
 (void)b;assert(token==71&&active->refs==2);
 if(drop_block){D8CoreBlock*old=active->blocks;active->blocks=NULL;core_raw8_release(&old->raw);memset(old,0xdd,sizeof(*old));free(old);}
 HRESULT hr=apply(ctx,D9WG_APPLY8_RENDER,0,0);if(FAILED(hr))return hr;
 for(UINT i=0;i<8;i++){hr=apply(ctx,D9WG_APPLY8_TEXTURE_STAGE,i,0);if(FAILED(hr))return hr;}
 hr=apply(ctx,D9WG_APPLY8_VERTEX,0,0);if(FAILED(hr))return hr;
 hr=apply(ctx,D9WG_APPLY8_PIXEL,0,0);if(FAILED(hr))return hr;
 return apply(ctx,D9WG_APPLY8_INDICES,0,step());
}
${extract("core_device8_ApplyStateBlock")}
static UINT checks,cases;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"raw8 replay failure line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
int main(void){Methods methods={pixel};
 for(UINT destroy=0;destroy<2;destroy++)for(UINT error=0;error<2;error++)for(UINT stop=0;stop<=99;stop++){
  D8CoreDevice d;memset(&d,0,sizeof(d));d.refs=1;d.methods=&methods;active=&d;
  D8CoreShader vs={1,NULL,NULL},ps={1,NULL,NULL};D8CoreBlock*block=calloc(1,sizeof(*block));assert(block);block->token=71;d.blocks=block;
  for(UINT i=0;i<32;i++){block->raw.render_mask[i]=1;block->raw.render[i]=100+i;}
  for(UINT s=0;s<8;s++)for(UINT i=0;i<8;i++){block->raw.texture_mask[s][i]=1;block->raw.texture[s][i]=1000+s*32+i;}
  block->raw.vertex_mask=block->raw.pixel_mask=block->raw.indices_mask=1;
  block->raw.vertex=11;block->raw.pixel=13;block->raw.base=0xfffffffdu;block->raw.vs=&vs;block->raw.ps=&ps;
  fail_at=stop;calls=0;drop_block=destroy;injected=error?E_OUTOFMEMORY:D3DERR_DRIVERINTERNALERROR;
  HRESULT hr=core_device8_ApplyStateBlock(&d.iface,71);CHECK(hr==(stop?injected:0));CHECK(calls==(stop?stop:99));CHECK(d.refs==1);
  UINT accepted=stop?(error?stop-1:stop):99;
  for(UINT i=0;i<256;i++)CHECK(d.live.render[i]==(i<32&&i<accepted?100+i:0));
  for(UINT s=0;s<8;s++)for(UINT i=0;i<32;i++)CHECK(d.live.texture[s][i]==(i<8&&32+s*8+i<accepted?1000+s*32+i:0));
  // Shader binding commits its raw handle only after a successful paired bind.
  UINT successful=stop?stop-1:99;
  CHECK(d.live.vertex==(successful>=97?11u:0u));CHECK(d.live.pixel==(successful>=98?13u:0u));
  CHECK(d.live.base==(accepted>=99?0xfffffffdu:0u));
  CHECK(vs.refs==(!destroy)+(successful>=97));CHECK(ps.refs==(!destroy)+(successful>=98));
  if(d.blocks){core_raw8_release(&d.blocks->raw);free(d.blocks);d.blocks=NULL;}
  core_raw8_release(&d.live);CHECK(!vs.refs&&!ps.refs);cases++;
 }
 printf("PASS actual Device8 raw replay: %u checks / %u scenarios; accepted failure prefixes, shader handle commit boundaries, base bits, reentrant block deletion, detached snapshot holds and balanced device refs\\n",checks,cases);return 0;
}
`;
try{fs.writeFileSync(dir+"/test.c",code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/test.c","-o",dir+"/test"],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+"/test",[],{encoding:"utf8"});assert.equal(run.status,0,run.stdout+run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
