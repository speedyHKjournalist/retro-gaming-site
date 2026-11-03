"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-private-entry-"));
function extract(source,name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
try{for(const api of[8,9]){const source=fs.readFileSync(process.env['D'+api+'WG_COM_SOURCE']||root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8'),types=['Texture','CubeTexture','VolumeTexture'];const structures=types.map(type=>{const begin=source.indexOf('struct D'+api+type+' {'),end=source.indexOf('\n};',begin);assert(begin>=0&&end>begin);return source.slice(begin,end+3);}).join('\n');fs.writeFileSync(dir+'/layout.c',`#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "runtime/d9wg_private_data.h"
typedef uint32_t UINT,DWORD;typedef int32_t LONG;typedef int BOOL,D3DFORMAT,D3DPOOL,D3DMULTISAMPLE_TYPE,D3DTEXTUREFILTERTYPE;
typedef struct D${api}Device D${api}Device;typedef struct D${api}TextureLevel D${api}TextureLevel;typedef struct D${api}SwapChain D${api}SwapChain;
${types.map(type=>'typedef struct D'+api+type+' D'+api+type+';typedef struct{const void*lpVtbl;}IDirect3D'+type+api+';').join('\n')}
${structures}
${['texture_from_iface','cube_from_iface','volume_texture_from_iface'].map(name=>extract(source,name)).join('\n')}
#define CHECK(Type,Interface,from) do{Type object={0};object.handle=0x12345678;object.iface.lpVtbl=(void*)(uintptr_t)0x1000;Interface*iface=&object.iface;assert((void*)iface==(void*)&object);assert(from(iface)==&object&&from(iface)->handle==0x12345678);}while(0)
int main(void){CHECK(D${api}Texture,IDirect3DTexture${api},texture_from_iface);CHECK(D${api}CubeTexture,IDirect3DCubeTexture${api},cube_from_iface);CHECK(D${api}VolumeTexture,IDirect3DVolumeTexture${api},volume_texture_from_iface);printf("PASS actual API ${api} COM layouts: three production structs and conversion helpers preserve interface-at-zero and exact object identity\\n");return 0;}
`);const b=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-I',root,dir+'/layout.c','-o',dir+'/layout'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+'/layout',[],{encoding:'utf8'});assert.equal(r.status,0,r.stderr+' signal='+r.signal);process.stdout.write(r.stdout);}}finally{fs.rmSync(dir,{recursive:true,force:true});}
