'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-plain-lock-'));
try{
const s=fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8'),a=s.indexOf('static HRESULT texture_lock_level('),z=s.indexOf('static HRESULT texture_unlock_level(',a);assert(a>0&&z>a);
fs.writeFileSync(dir+'/test.c',`#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "${root}/d3d9proxy/d3d9_protocol.h"
typedef int HRESULT,BOOL,INT;typedef unsigned UINT,DWORD;typedef int32_t LONG;typedef unsigned char BYTE;
typedef struct{LONG left,top,right,bottom;}RECT;typedef struct{int Pitch;void*pBits;}D3DLOCKED_RECT;
typedef struct{UINT handle;}Device;
typedef struct{UINT width,height,row_pitch,row_count,byte_count;BYTE*shadow;BOOL locked,shadow_valid;RECT lock_rect;UINT lock_flags;}D9TextureLevel;
typedef struct{Device*device;UINT pool,usage,format,handle,level_count;BOOL private_back_buffer;D9TextureLevel*levels;}D9Texture;
typedef struct{int unused;}D9WGReadbackDiagnostic;
#define TRUE 1
#define FALSE 0
#define D3DPOOL_DEFAULT 0
#define D3DUSAGE_DYNAMIC 0x200
#define D3DUSAGE_RENDERTARGET 1
#define D3DUSAGE_DEPTHSTENCIL 2
#define D3DLOCK_READONLY 0x10
#define D3DLOCK_NO_DIRTY_UPDATE 0x8000
#define D3DLOCK_DISCARD 0x2000
#define D3DERR_INVALIDCALL -1
#define D3DERR_DRIVERINTERNALERROR -2
#define D9WG_READBACK_INVALID 1
#define E_OUTOFMEMORY -3
#define D3D_OK 0
#define TRACE_REFUSE(x) (x)
#define ZeroMemory(p,n) memset(p,0,n)
#define SetRect(r,l,t,x,b) (*(r)=(RECT){l,t,x,b})
static unsigned reads,dirty;static int failure;static void*g_process;static int submit_readback_batch_locked;
static unsigned device_compatibility_api(Device*d){(void)d;return 9;}
static int d9wg_cpu_texture_broken_pitch(unsigned api,unsigned format){(void)api;(void)format;return 0;}
static int texture_levels_mapped(D9TextureLevel*l,unsigned n,int v){(void)l;(void)n;(void)v;return 0;}
static int d9wg_format_layout(unsigned api,unsigned f,unsigned*w,unsigned*h,unsigned*b){(void)api;(void)f;*w=*h=1;*b=4;return 1;}
static int texture_mark_dirty(D9Texture*t,unsigned l,const RECT*r){(void)t;(void)l;(void)r;dirty++;return 1;}
static int d9wg_surface_readback(void*p,D9WGReadbackSurface*r,unsigned pitch,unsigned rows,BYTE*dst,unsigned n,int submit,D9WGReadbackDiagnostic*d){
 (void)p;(void)submit;(void)d;assert(r->device_handle==7&&r->texture_handle==8&&r->format==21&&!r->level&&r->width==4&&r->height==4&&r->destination_pitch==16);assert(pitch==16&&rows==4&&n==64);reads++;if(failure)return failure;memset(dst,0x42,n);return 0;}
${s.slice(a,z)}
int main(void){unsigned checks=0;Device device={7};BYTE storage[64];RECT rects[]={{1,1,3,3},{-1,0,2,2}};
for(unsigned valid=0;valid<2;valid++)for(unsigned discard=0;discard<2;discard++)for(unsigned bad=0;bad<5;bad++)for(unsigned read_failure=0;read_failure<3;read_failure++){
 memset(storage,0x55,64);D9TextureLevel level={4,4,16,4,64,storage,0,valid,{0,0,0,0},0};D9Texture texture={&device,0,0,21,8,1,1,&level};D3DLOCKED_RECT lock={-99,(void*)(uintptr_t)0x1234};
 if(bad==1)level.locked=1;if(bad==2)texture.private_back_buffer=0;if(bad==3)texture.usage=1;
 reads=dirty=0;failure=read_failure;unsigned flags=D3DLOCK_READONLY|(discard?D3DLOCK_DISCARD:0);
 int expected=bad?-1:!valid&&!discard&&read_failure?(read_failure==1?-1:-2):0;
 int hr=texture_lock_level(&texture,0,&lock,bad==4?&rects[1]:&rects[0],flags);assert(hr==expected);
 assert(reads==(unsigned)(!bad&&!valid&&!discard));assert(!dirty);
 if(hr==0){assert(lock.Pitch==16&&lock.pBits==storage+20&&level.locked);assert(storage[0]==(!valid&&!discard?0x42:0x55));}
 else{assert(lock.Pitch==-99&&lock.pBits==(void*)(uintptr_t)0x1234);assert(level.locked==(bad==1));}
 checks++;
}
printf("PASS plain-surface LockRect coherence: %u combinations; lazy refresh, valid cache, DISCARD, invalid input and failed readback preserve lock state\\n",checks);}
`);
const cc=spawnSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(cc.status,0,cc.stderr);
const run=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(run.status,0,run.stdout+run.stderr);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
