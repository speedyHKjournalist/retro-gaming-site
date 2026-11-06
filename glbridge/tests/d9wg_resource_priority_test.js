'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const source=fs.readFileSync(path.resolve(__dirname,'../d3d9proxy/d3d9_proxy.c'),'utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-priority-'));
function extract(name){const m=new RegExp('^static[^;{}]*\\b'+name+'\\([^;]*?\\)\\s*\\{','m').exec(source);assert(m,name);let b=source.indexOf('{',m.index)+1,d=1;for(;d;b++){if(source[b]==='{')d++;if(source[b]==='}')d--;}return source.slice(m.index,b);}
try{
const prefixes=['texture','cube','volume_texture','vb','ib'];
const code=`#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t DWORD;
#define WINAPI
#define D3DPOOL_MANAGED 1
struct Resource{DWORD pool,priority;};
typedef struct Resource D9Texture,D9CubeTexture,D9VolumeTexture,D9VertexBuffer,D9IndexBuffer,IDirect3DTexture9,IDirect3DCubeTexture9,IDirect3DVolumeTexture9,IDirect3DVertexBuffer9,IDirect3DIndexBuffer9;
${prefixes.map(p=>`#define ${p}_from_iface(p) (p)`).join('\n')}
${prefixes.flatMap(p=>[p+'_set_priority',p+'_get_priority']).map(extract).join('\n')}
int main(void){DWORD(*setters[])(struct Resource*,DWORD)={${prefixes.map(p=>p+'_set_priority').join(',')}};DWORD(*getters[])(struct Resource*)={${prefixes.map(p=>p+'_get_priority').join(',')}};DWORD values[]={0,1,0xffffffff,0x80000000};unsigned checks=0;for(unsigned k=0;k<5;k++)for(DWORD pool=0;pool<4;pool++){struct Resource r={pool,0};DWORD prior=0;for(unsigned i=0;i<4;i++){assert(setters[k](&r,values[i])==(pool==1?prior:0));assert(getters[k](&r)==(pool==1?values[i]:0));prior=values[i];checks+=2;}}printf("PASS resource priority: %u checks; all five resource kinds, four pools and full DWORD priorities\\n",checks);return 0;}
`;fs.writeFileSync(dir+'/test.c',code);let r=spawnSync('cc',['-std=c11','-Wall','-Wextra','-Werror',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);r=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);process.stdout.write(r.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
