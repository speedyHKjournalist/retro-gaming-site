"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-mapped-draw-"));
function extract(source,name){const m=source.match(new RegExp("static HRESULT WINAPI "+name+"\\([^;]*?\\)\\n\\{[\\s\\S]*?\\n}\\n"));assert(m,name);return m[0];}
try{for(const api of [8,9]){
 const source=fs.readFileSync(process.env.D9WG_MAPPED_API===String(api)?process.env.D9WG_MAPPED_FRONTEND_SOURCE:root+"/d3d"+api+"proxy/d3d"+api+"_proxy.c","utf8");
 const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime/d9wg_buffer_lock.h"
typedef int32_t HRESULT;typedef int BOOL;typedef uint32_t UINT,DWORD;typedef uint8_t BYTE;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define D3DLOCK_READONLY 16u
#define D3DLOCK_NOOVERWRITE 4096u
#define D3DLOCK_DISCARD 8192u
#define D8WG_TRACE_ERROR(x) (x)
#define TRACE_REFUSE(x) (x)
typedef struct Device Device;
typedef struct{unsigned marker;}Clip;
/* Different VB/IB field order catches incorrect SDK object casts. */
typedef struct VertexBuffer{Device *device;UINT handle,length,usage,pool;BYTE *shadow;UINT lock_offset,lock_size;DWORD lock_flags;BOOL locked;Clip clips;UINT guard;}VertexBuffer;
typedef struct IndexBuffer{UINT guard;BOOL locked;DWORD lock_flags;BYTE *shadow;UINT pool,lock_size,length;Device *device;UINT handle,lock_offset,usage;}IndexBuffer;
struct Device{struct{VertexBuffer *buffer;}streams[16];IndexBuffer *index_buffer;};
typedef VertexBuffer D${api}VertexBuffer,IDirect3DVertexBuffer${api};typedef IndexBuffer D${api}IndexBuffer,IDirect3DIndexBuffer${api};
static VertexBuffer *vb_from_iface(VertexBuffer*p){return p;}static IndexBuffer *ib_from_iface(IndexBuffer*p){return p;}
static void d9wg_vertex_windows_clip_release(void*c,void*p){(void)c;(void)p;}
static void d9wg_clip_buffer_free(Clip*c,void(*f)(void*,void*),void*context){(void)f;(void)context;c->marker=0;}
static struct{UINT handle,offset,size,flags;BYTE bytes[64];}updates[64];static unsigned calls,fail_at;
static BOOL emit_buffer_update(UINT handle,UINT offset,const void*data,UINT size,UINT flags){if(calls>=64||size>64)abort();updates[calls].handle=handle;updates[calls].offset=offset;updates[calls].size=size;updates[calls].flags=flags;if(size)memcpy(updates[calls].bytes,data,size);++calls;return calls!=fail_at;}
#define D9WG_MAPPED_DEVICE Device
#define D9WG_MAPPED_VERTEX VertexBuffer
#define D9WG_MAPPED_INDEX IndexBuffer
#include "${process.env.D9WG_MAPPED_BRIDGE_HEADER||"runtime/d9wg_mapped_draw_bridge.h"}"
${["vb_lock","vb_unlock","ib_lock","ib_unlock"].map(n=>extract(source,n)).join("\n")}
typedef ${api===8?"BYTE*":"void*"} LockPointer;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"mapped draw line %u API=${api}: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){
 Device device={0},other={0};VertexBuffer vb[16]={0};IndexBuffer ib={.guard=0x13579,.device=&device,.handle=201,.length=64,.usage=520};BYTE bytes[17][64];LockPointer mapped=NULL;
 for(unsigned b=0;b<17;++b)for(unsigned x=0;x<64;++x)bytes[b][x]=(BYTE)(b*7+x);
 ib.shadow=bytes[16];device.index_buffer=&ib;
 for(unsigned b=0;b<16;++b){vb[b]=(VertexBuffer){.device=&device,.handle=101+b,.length=64,.usage=520,.shadow=bytes[b],.clips={1},.guard=0x24680};device.streams[b].buffer=vb+b;CHECK(vb_lock(vb+b,4,12,&mapped,8192)==0&&(BYTE*)mapped==bytes[b]+4);memset(mapped,0x80+b,12);}
 CHECK(ib_lock(&ib,2,6,&mapped,8192)==0&&(BYTE*)mapped==bytes[16]+2);memset(mapped,0x6d,6);
 CHECK(d9wg_flush_draw_buffers(&device,TRUE)&&calls==17);
 for(unsigned b=0;b<16;++b){CHECK(vb[b].locked&&!vb[b].lock_flags&&vb[b].lock_offset==4&&vb[b].lock_size==12&&!vb[b].clips.marker&&vb[b].guard==0x24680);CHECK(updates[b].handle==101+b&&updates[b].offset==4&&updates[b].size==12&&updates[b].flags==8192);for(unsigned x=0;x<12;++x)CHECK(updates[b].bytes[x]==0x80+b);memset(bytes[b]+4,0x40+b,12);}
 CHECK(ib.locked&&!ib.lock_flags&&ib.lock_offset==2&&ib.lock_size==6&&ib.guard==0x13579);CHECK(updates[16].handle==201&&updates[16].offset==2&&updates[16].size==6&&updates[16].flags==8192);memset(bytes[16]+2,0x7e,6);
 CHECK(d9wg_flush_draw_buffers(&device,TRUE)&&calls==34);
 for(unsigned b=0;b<16;++b){CHECK(!updates[b+17].flags);for(unsigned x=0;x<12;++x)CHECK(updates[b+17].bytes[x]==0x40+b);CHECK(vb_unlock(vb+b)==0&&!vb[b].locked&&!vb[b].lock_offset&&!vb[b].lock_size&&!vb[b].lock_flags);}
 CHECK(ib_unlock(&ib)==0&&!ib.locked&&!ib.lock_offset&&!ib.lock_size&&!ib.lock_flags&&calls==51);for(unsigned b=34;b<51;++b)CHECK(!updates[b].flags);
 /* Duplicate stream bindings, genuine READONLY and non-indexed IB isolation. */
 memset(device.streams,0,sizeof(device.streams));device.streams[0].buffer=device.streams[15].buffer=vb;calls=0;
 CHECK(vb_lock(vb,7,9,&mapped,8192)==0);ib.usage=0;CHECK(ib_lock(&ib,2,6,&mapped,16)==0);
 CHECK(d9wg_flush_draw_buffers(&device,TRUE)&&calls==1&&updates[0].offset==7&&updates[0].size==9&&updates[0].flags==8192&&vb->locked&&!vb->lock_flags&&ib.locked&&ib.lock_flags==16);
 CHECK(vb_unlock(vb)==0&&calls==2);CHECK(d9wg_flush_draw_buffers(&device,FALSE)&&calls==2&&ib.locked);CHECK(ib_unlock(&ib)==0&&calls==2);
 calls=0;CHECK(vb_lock(vb,1,5,&mapped,4096)==0);CHECK(d9wg_flush_draw_buffers(&device,FALSE)&&calls==1&&updates[0].flags==4096&&vb->lock_flags==4096&&vb->locked);CHECK(vb_unlock(vb)==0&&updates[1].flags==4096);
 calls=0;vb->usage=0;vb->clips.marker=1;CHECK(vb_lock(vb,2,3,&mapped,8192)==0);CHECK(d9wg_flush_draw_buffers(&device,FALSE)&&calls==1&&!updates[0].flags&&vb->clips.marker==1);CHECK(vb_unlock(vb)==0);
 calls=0;vb->usage=8;CHECK(vb_lock(vb,2,3,&mapped,16)==0&&vb->lock_flags==0);CHECK(d9wg_flush_draw_buffers(&device,FALSE)&&calls==1);CHECK(vb_unlock(vb)==0);
 /* Every descriptor is checked before the first upload. */
 calls=0;vb[0].usage=vb[1].usage=520;device.streams[1].buffer=vb+1;CHECK(vb_lock(vb,0,12,&mapped,8192)==0&&vb_lock(vb+1,0,12,&mapped,8192)==0);vb[1].lock_offset=65;
 CHECK(!d9wg_flush_draw_buffers(&device,TRUE)&&calls==0&&vb[0].locked&&vb[0].lock_flags==8192&&vb[1].lock_flags==8192);vb[1].lock_offset=0;
 vb[1].device=&other;CHECK(!d9wg_flush_draw_buffers(&device,TRUE)&&!calls&&vb[0].lock_flags==8192);vb[1].device=&device;
 /* A failed second upload keeps the first receipt, without unlocking either. */
 fail_at=2;CHECK(!d9wg_flush_draw_buffers(&device,FALSE)&&calls==2&&!vb[0].lock_flags&&vb[1].lock_flags==8192&&vb[0].locked&&vb[1].locked);fail_at=0;calls=0;
 CHECK(d9wg_flush_draw_buffers(&device,FALSE)&&calls==2&&!updates[0].flags&&updates[1].flags==8192&&!vb[1].lock_flags);CHECK(vb_unlock(vb)==0&&vb_unlock(vb+1)==0);
 calls=0;ib.usage=520;CHECK(ib_lock(&ib,0,8,&mapped,8192)==0);CHECK(d9wg_flush_draw_buffers(&device,FALSE)&&calls==0&&ib.locked&&ib.lock_flags==8192);CHECK(d9wg_flush_draw_buffers(&device,TRUE)&&calls==1&&!ib.lock_flags&&ib.locked);CHECK(ib_unlock(&ib)==0&&!updates[1].flags);
 calls=0;CHECK(vb_lock(vb,64,0,&mapped,8192)==0&&(BYTE*)mapped==bytes[0]+64);CHECK(d9wg_flush_draw_buffers(&device,FALSE)&&calls==1&&updates[0].size==0&&updates[0].flags==8192&&vb->locked&&!vb->lock_flags);CHECK(vb_unlock(vb)==0&&calls==2&&!updates[1].flags&&!vb->locked);
 D9WGMappedBuffer maps[2]={{1,bytes[0],301,64,3,5,8192,7},{1,bytes[0],301,64,4,5,8192,7}};calls=0;
 CHECK(!d9wg_flush_mapped_buffers(maps,2,d9wg_emit_mapped_buffer,NULL)&&calls==0&&maps[0].uploaded==7&&maps[1].uploaded==7);
 maps[1]=maps[0];CHECK(d9wg_flush_mapped_buffers(maps,2,d9wg_emit_mapped_buffer,NULL)&&calls==1&&maps[0].uploaded==1&&maps[1].uploaded==0);
 CHECK(d9wg_flush_mapped_buffers(NULL,0,NULL,NULL));CHECK(!d9wg_flush_mapped_buffers(maps,18,d9wg_emit_mapped_buffer,NULL));
 printf("PASS actual API ${api} mapped draw bridge and Lock/Unlock: 17 buffers, exact ranges/data/order, duplicate bindings, repeat writes, readonly/static/conflicting hints, one DISCARD per map, partial transport receipts, zero-size maps, owner/range atomicity and retained locks\\n");return 0;}
`;
 fs.writeFileSync(dir+"/map.c",code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-I",root,"-I",root+"/runtime",dir+"/map.c",process.env.D9WG_MAPPED_POLICY_SOURCE||root+"/runtime/d9wg_buffer_lock.c","-o",dir+"/map"],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+"/map",[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);
}}finally{fs.rmSync(dir,{recursive:true,force:true});}
