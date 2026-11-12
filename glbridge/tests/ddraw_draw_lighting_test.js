"use strict";
// Wine's original lighting_interface_versions table makes the pre-D3D7
// lighting choice depend on vertex normals / DONOTLIGHT, not LIGHTING state.
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.DDRAW_DRAW_LIGHTING_SOURCE||root+"/ddrawproxy/d3d7_proxy.inc","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-lighting-"));
function extract(name){const match=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(match,name);let end=source.indexOf("{",match.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(match.index,end);}
const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef uint32_t DWORD,UINT;typedef int32_t HRESULT;typedef int BOOL,D3DPRIMITIVETYPE;typedef uint16_t WORD;typedef unsigned char BYTE;
typedef struct{unsigned creation_version;BOOL lighting,render_target_invalid,render_target_pending;}D3D7Device,IDirect3DDevice7;
typedef void D3DDRAWPRIMITIVESTRIDEDDATA;
#define WINAPI
#define D3D_OK 0
#define D3DERR_INVALIDVERTEXFORMAT (-1)
#define DDERR_INVALIDPIXELFORMAT (-2)
#define D3DERR_INITFAILED (-3)
#define D3DRENDERSTATE_LIGHTING 137u
#define D3DFVF_NORMAL 0x10u
#define D3DDP_DONOTLIGHT 4u
#define DDWG_TRACE_ERROR(h) (h)
#define FAILED(h) ((h)<0)
static unsigned checks,sets,draws,frees,fail_set,fail_draw,fail_pack;static BOOL want;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"draw lighting line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
static D3D7Device*d3d7_device_from_iface(IDirect3DDevice7*p){return p;}
static BOOL d3d7_emit_render_target(D3D7Device*d){(void)d;MUST(0);return 0;}
static HRESULT d3d7_device_SetRenderState(IDirect3DDevice7*d,DWORD state,DWORD value){sets++;MUST(state==D3DRENDERSTATE_LIGHTING);if(fail_set)return -90;d->lighting=value;return D3D_OK;}
static BOOL d3d7_emit_draw_up(D3D7Device*d,D3DPRIMITIVETYPE type,DWORD fvf,const void*v,DWORD count){(void)type;(void)fvf;MUST(v&&count==4&&d->lighting==want);draws++;return !fail_draw;}
static BOOL d3d7_emit_draw_indexed_up(D3D7Device*d,D3DPRIMITIVETYPE type,DWORD fvf,const void*v,DWORD count,const WORD*indices,DWORD n){MUST(indices&&n==6);return d3d7_emit_draw_up(d,type,fvf,v,count);}
static BYTE*d3d7_pack_strided(DWORD fvf,void*data,DWORD count,UINT*bytes){(void)fvf;MUST(data&&count==4);*bytes=128;return fail_pack?NULL:malloc(128);}
static void heap_free(void*p){MUST(p);frees++;free(p);}
${["d3d7_prepare_legacy_draw","d3d7_device_DrawPrimitive","d3d7_device_DrawIndexedPrimitive","d3d7_device_DrawPrimitiveStrided","d3d7_device_DrawIndexedPrimitiveStrided"].map(extract).join("\n")}
static HRESULT draw(D3D7Device*d,unsigned path,DWORD fvf,DWORD flags){BYTE data[128]={0};WORD indices[]={0,1,2,2,1,3};if(path==0)return d3d7_device_DrawPrimitive(d,5,fvf,data,4,flags);if(path==1)return d3d7_device_DrawIndexedPrimitive(d,4,fvf,data,4,indices,6,flags);if(path==2)return d3d7_device_DrawPrimitiveStrided(d,5,fvf,data,4,flags);return d3d7_device_DrawIndexedPrimitiveStrided(d,4,fvf,data,4,indices,6,flags);}
int main(void){const DWORD fvfs[]={0x112u,0x1e2u,0x1c4u,0x142u},flags[]={0,4,1,5,0x80000004u};for(unsigned round=0;round<64;round++)for(unsigned version=1;version<=7;version++){if(version>3&&version<7)continue;D3D7Device d={.creation_version=version};for(unsigned f=0;f<4;f++)for(unsigned mode=0;mode<5;mode++)for(unsigned initial=0;initial<2;initial++)for(unsigned path=0;path<4;path++){d.lighting=initial;want=version==7?initial:(f==0&&!(flags[mode]&4));unsigned old_sets=sets,old_draws=draws,old_frees=frees;MUST(draw(&d,path,fvfs[f],flags[mode])==D3D_OK&&draws==old_draws+1&&d.lighting==want);MUST(sets==old_sets+(version<7)&&frees==old_frees+(path>=2));fail_set=1;d.lighting=initial;old_draws=draws;MUST(draw(&d,path,fvfs[f],flags[mode])==(version==7?D3D_OK:-90));MUST(draws==old_draws+(version==7)&&d.lighting==(BOOL)initial);fail_set=0;fail_draw=1;MUST(draw(&d,path,fvfs[f],flags[mode])==D3DERR_INVALIDVERTEXFORMAT);fail_draw=0;}}
printf("PASS actual legacy draw lighting C: %u checks, lit/prelit/transformed formats, DONOTLIGHT and unrelated flags, four UP/strided draw entry points, unchanged D3D7 state, failed state submission stops drawing, packed memory freed\\n",checks);return 0;}
`;
try{const file=dir+"/lighting.c",binary=dir+"/lighting";fs.writeFileSync(file,code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer",file,"-o",binary],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
