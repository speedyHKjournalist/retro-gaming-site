"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-lod-"));
function extract(source,name){const m=new RegExp("static [^;{}]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
try{for(const api of [8,9]){const source=fs.readFileSync(process.env['D'+api+'WG_LOD_SOURCE']||root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8'),header=process.env.D9WG_LOD_HEADER||root+'/runtime/d9wg_texture_lod_bridge.h';fs.writeFileSync(dir+"/lod.c",`#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
typedef uint32_t DWORD;typedef int D3DPOOL;
#define D3DPOOL_MANAGED 1
#define WINAPI
typedef struct{uint32_t handle;}Device;
typedef struct{Device*device;uint32_t handle,level_count;D3DPOOL pool;DWORD lod;}Texture;
typedef Texture D${api}Texture,D${api}CubeTexture,D${api}VolumeTexture,IDirect3DTexture${api},IDirect3DCubeTexture${api},IDirect3DVolumeTexture${api};
static unsigned count,fail;static D9WGSetTextureLOD command;
static int emit_command(uint16_t op,const void*p,uint32_t size){assert(op==D9WG_OP_SET_TEXTURE_LOD&&size==sizeof(command));memcpy(&command,p,size);count++;return !fail;}
static Texture*texture_from_iface(Texture*t){return t;}
static Texture*cube_from_iface(Texture*t){return t;}
static Texture*volume_texture_from_iface(Texture*t){return t;}
#include "${header}"
${['texture_set_lod','texture_get_lod','cube_set_lod','cube_get_lod','volume_texture_set_lod','volume_texture_get_lod'].map(n=>extract(source,n)).join('\n')}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"LOD API ${api} line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){Device d={.handle=0x12345678};DWORD(*set[3])(Texture*,DWORD)={texture_set_lod,cube_set_lod,volume_texture_set_lod};DWORD(*get[3])(Texture*)={texture_get_lod,cube_get_lod,volume_texture_get_lod};unsigned cases=0;uint32_t levels[]={1,3,17,32},requests[]={0,1,2,3,16,31,32,0xfffffffeu,0xffffffffu};
 for(unsigned kind=0;kind<3;kind++)for(int pool=0;pool<4;pool++)for(unsigned l=0;l<4;l++){Texture t={.device=&d,.handle=0x87654321,.level_count=levels[l],.pool=pool};uint32_t expected=0;for(unsigned r=0;r<9;r++){unsigned before=count;DWORD old=set[kind](&t,requests[r]);CHECK(old==expected);if(pool==1){expected=requests[r]<levels[l]?requests[r]:levels[l]-1;CHECK(count==before+1&&command.device_handle==d.handle&&command.resource_handle==t.handle&&command.lod==expected&&!command.reserved);}else CHECK(count==before);CHECK(get[kind](&t)==expected&&t.lod==expected);cases++;}}
 for(unsigned kind=0;kind<3;kind++){Texture t={.device=&d,.handle=kind+1,.level_count=3,.pool=1};unsigned before=count;fail=1;CHECK(!set[kind](&t,1)&&get[kind](&t)==1&&count==before+1&&command.lod==1);fail=0;CHECK(set[kind](&t,1)==1&&get[kind](&t)==1&&count==before+2&&command.lod==1);cases++;}
 printf("PASS actual API ${api} texture LOD: %u 2D/cube/volume managed and ignored pools, clamped DWORD requests, exact wire, old values and same-value transport retry cases\\n",cases);return 0;}
`);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-I",root,dir+"/lod.c","-o",dir+"/lod"],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+"/lod",[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr+" signal="+run.signal);process.stdout.write(run.stdout);}}finally{fs.rmSync(dir,{recursive:true,force:true});}
