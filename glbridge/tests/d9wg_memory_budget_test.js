'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(process.env.D9WG_MEMORY_SOURCE||root+'/d3d9proxy/d3d9_proxy.c','utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-memory-'));
function extract(name){const m=new RegExp('^static[^;{}]*\\b'+name+'\\([^;]*?\\)\\s*\\{','m').exec(source);assert(m,name);let e=source.indexOf('{',m.index)+1,d=1;for(;d;e++){if(source[e]==='{')d++;if(source[e]==='}')d--;}return source.slice(m.index,e);}
try{fs.writeFileSync(dir+'/memory.c',`#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include "d9wg_format_layouts.h"
#include "d9wg_format_queries.h"
typedef uint32_t UINT,DWORD,D3DFORMAT;
#define WINAPI
#define TRACE(...)
#define D3DPOOL_MANAGED 1u
#define D3DUSAGE_AUTOGENMIPMAP 0x400u
#define D3DRTYPE_TEXTURE 3u
#define MIB (1024u*1024u)
typedef struct Texture{UINT width,height,depth,edge_length,level_count,format,usage,pool,multisample;struct Texture*next_device_resource;}D9Texture,D9CubeTexture,D9VolumeTexture;
typedef struct Buffer{UINT length,pool;struct Buffer*next_device_resource;}D9VertexBuffer,D9IndexBuffer;
typedef struct{D9Texture*texture_resources;D9CubeTexture*cube_textures;D9VolumeTexture*volume_textures;D9VertexBuffer*vertex_buffers;D9IndexBuffer*index_buffers;}D9Device,IDirect3DDevice9;
static D9Device*device_from_iface(IDirect3DDevice9*d){return d;}
${['texture_memory_estimate','device_get_available_texture_mem'].map(extract).join('\n')}
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"Memory budget line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
int main(void){unsigned checks=0;D9Device d={0},other={0};D9Texture textures[64]={0};
 CHECK(device_get_available_texture_mem(&d)==256u*MIB);
 for(UINT pool=0;pool<4;pool++){
  for(UINT i=0;i<20;i++)textures[i]=(D9Texture){.width=1024,.height=1024,.level_count=1,.format=22,.pool=pool,.next_device_resource=i?&textures[i-1]:NULL};
  d.texture_resources=&textures[19];CHECK(device_get_available_texture_mem(&d)==(pool<2?176u:256u)*MIB);
  d.texture_resources=&textures[9];CHECK(device_get_available_texture_mem(&d)==(pool<2?216u:256u)*MIB);
  d.texture_resources=NULL;CHECK(device_get_available_texture_mem(&d)==256u*MIB);
 }
 for(UINT i=0;i<64;i++)textures[i]=(D9Texture){.width=8192,.height=8192,.level_count=1,.format=22,.next_device_resource=i?&textures[i-1]:NULL};
 d.texture_resources=&textures[63];CHECK(device_get_available_texture_mem(&d)==0);CHECK(device_get_available_texture_mem(&other)==256u*MIB);d.texture_resources=NULL;
 CHECK(texture_memory_estimate(4,4,1,3,22,0,1)==84);
 CHECK(texture_memory_estimate(4,4,1,1,22,0x400,1)==84);
 CHECK(texture_memory_estimate(4,4,4,3,22,0,1)==292);
 CHECK(texture_memory_estimate(8,8,1,4,0x31545844u,0,1)==56);
 CHECK(texture_memory_estimate(1,1,1,1,0x31545844u,0,1)==8);
 CHECK(texture_memory_estimate(1024,1024,1,1,22,0,4)==16u*MIB);
 D9CubeTexture cube={.edge_length=1024,.level_count=1,.format=22};D9VolumeTexture volume={.width=128,.height=128,.depth=64,.level_count=1,.format=22};
 D9VertexBuffer vb={.length=2u*MIB};D9IndexBuffer ib={.length=MIB};d.cube_textures=&cube;d.volume_textures=&volume;d.vertex_buffers=&vb;d.index_buffers=&ib;
 CHECK(device_get_available_texture_mem(&d)==225u*MIB);
 cube.pool=volume.pool=vb.pool=ib.pool=2;CHECK(device_get_available_texture_mem(&d)==256u*MIB);
 cube.pool=0;cube.usage=0x400;CHECK(device_get_available_texture_mem(&d)==224u*MIB);
 cube.pool=3;ib.pool=1;ib.length=1;CHECK(device_get_available_texture_mem(&d)==255u*MIB);
 printf("PASS actual memory budget: %u checks; live 20-target allocation/release, four pools, cube/volume/mips/BC/MSAA, buffers, >4GiB saturation, device isolation and MiB rounding\\n",checks);return 0;}
`);let r=spawnSync('cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+root+'/runtime',dir+'/memory.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);r=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(r.status,0,r.stdout+r.stderr);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
