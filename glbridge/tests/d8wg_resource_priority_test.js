"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8-priority-"));
function extract(name){const match=new RegExp("static DWORD WINAPI "+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(match,name);let end=source.indexOf("{",match.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(match.index,end);}
try {
 const kinds=[["vb","VertexBuffer"],["ib","IndexBuffer"],["texture","Texture"],["cube","CubeTexture"],["volume_texture","VolumeTexture"]];
 const c=`#include <stdint.h>
#include <stdio.h>
#include <assert.h>
typedef uint32_t DWORD,UINT;
#define WINAPI
#define D3DPOOL_MANAGED 1
typedef struct {UINT api;} Device;
typedef struct {Device *device;UINT pool,priority;} Resource;
static UINT device_compatibility_api(Device*d){return d->api;}
${kinds.map(([prefix,type])=>`typedef Resource D9${type},IDirect3D${type}9;
static Resource *${prefix}_from_iface(Resource*p){return p;}
${extract(prefix+'_set_priority')}
${extract(prefix+'_get_priority')}`).join('\n')}
int main(void){DWORD(*set[])(Resource*,DWORD)={${kinds.map(([p])=>p+'_set_priority').join(',')}};DWORD(*get[])(Resource*)={${kinds.map(([p])=>p+'_get_priority').join(',')}};UINT cases=0;
for(UINT kind=0;kind<5;kind++)for(UINT api=8;api<=9;api++)for(UINT pool=0;pool<4;pool++){
 Device device={api};Resource resource={&device,pool,0};DWORD previous=0;const DWORD values[]={1,0xffffffffu,0x80000000u,17,0};
 for(UINT i=0;i<5;i++){assert(get[kind](&resource)==previous);assert(set[kind](&resource,values[i])==previous);previous=api==8&&pool!=1?0:values[i];assert(get[kind](&resource)==previous);cases++;}}
printf("PASS actual resource priority: %u cases, five resource types, API8 managed-only, API9 preserved, full DWORD values\\n",cases);return 0;}`;
 fs.writeFileSync(dir+"/test.c",c);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Wall","-Wextra","-Werror",dir+"/test.c","-o",dir+"/test"],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);
 const run=spawnSync(dir+"/test",[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
