'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(process.env.D9WG_MANAGED_BUFFER_SOURCE||root+'/d3d9proxy/d3d9_proxy.c','utf8');
function extract(n){const m=new RegExp('^static [^;{}\\n]*?\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(source);assert(m,n);let i=source.indexOf('{',m.index)+1,d=1;for(;d;i++){if(source[i]==='{')d++;if(source[i]==='}')d--;}return source.slice(m.index,i);}
const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int BOOL,HRESULT;typedef uint32_t UINT,DWORD;typedef uint8_t BYTE;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define D3DPOOL_MANAGED 1
#define D3DLOCK_READONLY 16
#define D3DLOCK_DISCARD 8192
#define TRACE_REFUSE(v) (v)
typedef struct D9Device D9Device;
typedef struct Buffer {D9Device *device;UINT handle,length,pool,usage,lock_offset,lock_size,lock_flags,lock_count;BYTE *shadow;BOOL locked,full_upload_pending;int clips;struct Buffer *next_device_resource;} Buffer;
typedef Buffer D9VertexBuffer,D9IndexBuffer,IDirect3DVertexBuffer9,IDirect3DIndexBuffer9;
typedef struct D9Texture {UINT pool;struct D9Texture *next_device_resource;} D9Texture;
struct D9Device {UINT api;struct{Buffer *buffer;}streams[16];Buffer *index_buffer,*vertex_buffers,*index_buffers;D9Texture *texture_resources;};
typedef D9Device IDirect3DDevice9;
static Buffer *vb_from_iface(Buffer*b){return b;}static Buffer *ib_from_iface(Buffer*b){return b;}
static D9Device *device_from_iface(D9Device*d){return d;}static UINT device_compatibility_api(D9Device*d){return d->api;}
static void texture_dirty_all(D9Texture*t){(void)t;}
static void d9wg_vertex_windows_clip_release(void*c,void*p){(void)c;(void)p;}
static void d9wg_clip_buffer_free(int*c,void(*f)(void*,void*),void*p){(void)c;(void)f;(void)p;}
static BYTE gpu[2][1024*1024];static UINT uploads,fail_at,last_offset,last_size;
static BOOL emit_buffer_update(UINT h,UINT off,const void *p,UINT n,UINT f){(void)f;h--;uploads++;last_offset=off;last_size=n;if(uploads==fail_at)return FALSE;if(h>=2||off+n>sizeof(gpu[h]))abort();memcpy(gpu[h]+off,p,n);return TRUE;}
#define D9WG_MAPPED_DEVICE D9Device
#define D9WG_MAPPED_VERTEX D9VertexBuffer
#define D9WG_MAPPED_INDEX D9IndexBuffer
#include "${process.env.D9WG_MANAGED_BUFFER_HEADER||root+'/runtime/d9wg_mapped_draw_bridge.h'}"
${['vb_lock','vb_unlock','ib_lock','ib_unlock','vb_preload','ib_preload','device_evict_managed_resources'].map(extract).join('\n')}
static UINT checks;
#define CHECK(v) do{checks++;if(!(v)){fprintf(stderr,"managed buffer line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){for(UINT api=8;api<=9;api++)for(UINT index=0;index<2;index++)for(UINT flags=0;flags<=16;flags+=16){
 D9Device d={.api=api};BYTE cpu[1024*1024]={0};Buffer b={.device=&d,.handle=index+1,.length=sizeof(cpu),.pool=1,.shadow=cpu,.full_upload_pending=TRUE};void *p=NULL;
 d.streams[0].buffer=index?NULL:&b;d.streams[15].buffer=index?NULL:&b;d.index_buffer=index?&b:NULL;d.vertex_buffers=index?NULL:&b;d.index_buffers=index?&b:NULL;
 memset(gpu,0,sizeof(gpu));uploads=fail_at=0;
 CHECK((index?ib_lock(&b,0,64,&p,flags):vb_lock(&b,0,64,&p,flags))==0);CHECK(p==cpu);
 memset(p,0x23,64);cpu[0xffff*16]=0x7c;
 CHECK((index?ib_unlock(&b):vb_unlock(&b))==0);CHECK(!uploads&&b.full_upload_pending);
 CHECK(d9wg_flush_draw_buffers(&d,FALSE));CHECK(index?uploads==0:uploads==1);
 CHECK(d9wg_flush_draw_buffers(&d,TRUE));CHECK(uploads==1&&!b.full_upload_pending);CHECK(gpu[index][0xffff*16]==0x7c&&gpu[index][0]==0x23&&last_offset==0&&last_size==sizeof(cpu));
 CHECK(d9wg_flush_draw_buffers(&d,TRUE));CHECK(uploads==1);
 CHECK((index?ib_lock(&b,7,5,&p,0):vb_lock(&b,7,5,&p,0))==0);memset(p,0x45,5);cpu[sizeof(cpu)-1]=0x88;
 CHECK((index?ib_unlock(&b):vb_unlock(&b))==0);CHECK(uploads==2&&last_offset==7&&last_size==5);CHECK(gpu[index][7]==0x45&&gpu[index][sizeof(cpu)-1]==0);
 CHECK(device_evict_managed_resources(&d)==0&&b.full_upload_pending);fail_at=uploads+1;
 CHECK(!d9wg_flush_draw_buffers(&d,TRUE)&&b.full_upload_pending);CHECK(gpu[index][sizeof(cpu)-1]==0);
 if(index)ib_preload(&b);else vb_preload(&b);CHECK(!b.full_upload_pending&&gpu[index][sizeof(cpu)-1]==0x88);UINT before=uploads;
 if(index)ib_preload(&b);else vb_preload(&b);CHECK(uploads==before);
 b.pool=0;CHECK(device_evict_managed_resources(&d)==0&&!b.full_upload_pending);
 /* PreLoad failure is retried; drawing while still mapped preserves locks. */
 b.full_upload_pending=TRUE;fail_at=uploads+1;if(index)ib_preload(&b);else vb_preload(&b);CHECK(b.full_upload_pending);
 CHECK((index?ib_lock(&b,1,7,&p,0):vb_lock(&b,1,7,&p,0))==0);memset(p,0x66,7);
 CHECK(d9wg_flush_draw_buffers(&d,TRUE)&&!b.full_upload_pending&&b.locked&&b.lock_count==1);CHECK(gpu[index][1]==0x66);
 CHECK((index?ib_unlock(&b):vb_unlock(&b))==0&&!b.locked);
 }
 printf("PASS actual managed VB/IB C: %u checks; both APIs, full initial residency including vertex65535, READONLY, exact later ranges, duplicate streams, non-indexed isolation, eviction, preload, failed-upload retry and mapped draw\\n",checks);return 0;}
`;
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-managed-buffer-'));try{fs.writeFileSync(dir+'/test.c',code);const r=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',root+'/runtime',dir+'/test.c',root+'/runtime/d9wg_buffer_lock.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);const run=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
