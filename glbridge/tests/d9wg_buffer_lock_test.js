"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-buffer-lock-"));
function extract(source,name){const m=source.match(new RegExp("static HRESULT WINAPI "+name+"\\([^;]*?\\)\\n\\{[\\s\\S]*?\\n}\\n"));assert(m,name);return m[0];}
try{for(const api of [8,9]){
 const source=readComparisonSource(process.env.D9WG_BUFFER_LOCK_API===String(api)?process.env.D9WG_BUFFER_FRONTEND_SOURCE:root+"/d3d"+api+"proxy/d3d"+api+"_proxy.c","utf8");
 const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "runtime/d9wg_buffer_lock.h"
typedef int32_t HRESULT;typedef int BOOL;typedef uint32_t UINT,DWORD;typedef uint8_t BYTE;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL ((HRESULT)0x8876086cu)
#define D3DERR_DRIVERINTERNALERROR ((HRESULT)0x88760827u)
#define D3DLOCK_READONLY 16u
#define D3DLOCK_NOOVERWRITE 4096u
#define D3DLOCK_DISCARD 8192u
#define D3DUSAGE_DYNAMIC 512u
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE_REFUSE(v) (v)
#define ZeroMemory(p,n) memset(p,0,n)
typedef struct{unsigned marker;}Clip;
typedef struct{uint32_t handle,length,usage,pool;BOOL locked;uint32_t lock_offset,lock_size,lock_flags,lock_count;void*device;BYTE *shadow;Clip clips;}Buffer;
typedef Buffer D${api}VertexBuffer,D${api}IndexBuffer,IDirect3DVertexBuffer${api},IDirect3DIndexBuffer${api};
typedef ${api===8?"BYTE*":"void*"} LockPointer;
static Buffer *vb_from_iface(IDirect3DVertexBuffer${api}*p){return p;}static Buffer *ib_from_iface(IDirect3DIndexBuffer${api}*p){return p;}
static unsigned releases,uploads;static uint32_t upload_handle,upload_offset,upload_size,upload_flags;static uint8_t uploaded[64];static int fail_emit;
static void d9wg_vertex_windows_clip_release(void*c,void*p){(void)c;(void)p;}
static void d9wg_clip_buffer_free(Clip*c,void(*release)(void*,void*),void*context){(void)release;(void)context;releases++;c->marker=0;}
static BOOL emit_buffer_update(uint32_t h,uint32_t off,const void*p,uint32_t size,uint32_t flags){uploads++;upload_handle=h;upload_offset=off;upload_size=size;upload_flags=flags;if(size>sizeof(uploaded))abort();memcpy(uploaded,p,size);return !fail_emit;}
${api===9?"static unsigned compatibility_api=9;static unsigned device_compatibility_api(void*p){(void)p;return compatibility_api;}":""}
${["vb_lock","vb_unlock","ib_lock","ib_unlock"].map(n=>extract(source,n)).join("\n")}
static HRESULT lock(Buffer*b,int ib,UINT off,UINT size,LockPointer*out,DWORD flags){return ib?ib_lock(b,off,size,out,flags):vb_lock(b,off,size,out,flags);}static HRESULT unlock(Buffer*b,int ib){return ib?ib_unlock(b):vb_unlock(b);}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"buffer lock line %u API=${api}: %s\\n",__LINE__,#v);return 1;}}while(0)
#include "tests/native/d3d_buffer_lock_reference.h"
int main(void){unsigned cases=0;
 for(unsigned c=0;c<sizeof(contexts)/sizeof(*contexts);++c)for(unsigned f=0;f<8;++f)for(unsigned a=0;a<sizeof(advisory)/sizeof(*advisory);++a)for(int ib=0;ib<2;++ib){
  uint8_t bytes[64],before[64];for(unsigned i=0;i<64;++i)bytes[i]=(uint8_t)(i*7+3);memcpy(before,bytes,64);Buffer b={.handle=41,.length=64,.usage=contexts[c].usage,.pool=contexts[c].pool,.shadow=bytes,.clips={1}};uint32_t requested=flags[f]|advisory[a],effective=contexts[c].effective[f];LockPointer mapped=(LockPointer)(uintptr_t)0x1234;uploads=releases=0;
  CHECK(d9wg_buffer_lock_flags(b.usage,b.pool,requested)==effective);CHECK(lock(&b,ib,7,13,&mapped,requested)==D3D_OK);CHECK((BYTE*)mapped==bytes+7&&b.locked&&b.lock_offset==7&&b.lock_size==13&&b.lock_flags==effective);CHECK(releases==(!ib&&!!(effective&D3DLOCK_DISCARD)));CHECK(b.clips.marker==(ib||!(effective&D3DLOCK_DISCARD)));
  if(!(effective&D3DLOCK_DISCARD))CHECK(!memcmp(bytes,before,64));if(!(effective&D3DLOCK_READONLY))memset(mapped,0x6d,13);CHECK(unlock(&b,ib)==D3D_OK);CHECK(!b.locked&&!b.lock_offset&&!b.lock_size&&!b.lock_flags);CHECK(uploads==!(effective&D3DLOCK_READONLY));if(uploads){CHECK(upload_handle==41&&upload_offset==7&&upload_size==13&&upload_flags==effective);for(unsigned i=0;i<13;++i)CHECK(uploaded[i]==0x6d);}else CHECK(!memcmp(bytes,before,64));if(!(effective&D3DLOCK_DISCARD)){CHECK(!memcmp(bytes,before,7)&&!memcmp(bytes+20,before+20,44));}CHECK(unlock(&b,ib)==D3DERR_INVALIDCALL);cases++;
 }
 for(int ib=0;ib<2;++ib)for(unsigned f=0;f<sizeof(nativeFlags)/sizeof(*nativeFlags);++f){BYTE bytes[64]={0};Buffer b={.length=64,.usage=520,.pool=0,.shadow=bytes};LockPointer mapped=NULL;CHECK(lock(&b,ib,0,0,&mapped,nativeFlags[f])==D3D_OK&&mapped!=NULL);CHECK(unlock(&b,ib)==D3D_OK);}
 for(int ib=0;ib<2;++ib){BYTE bytes[64]={0};Buffer b={.length=64,.usage=512,.pool=0,.shadow=bytes},saved=b;LockPointer mapped=(LockPointer)(uintptr_t)0x1234;CHECK(lock(&b,ib,0,1,NULL,8192)==D3DERR_INVALIDCALL&&!memcmp(&b,&saved,sizeof(b)));const uint32_t bad[][2]={{65,0},{UINT32_MAX,1},{7,UINT32_MAX},{63,2}};for(unsigned i=0;i<4;++i){CHECK(lock(&b,ib,bad[i][0],bad[i][1],&mapped,8192)==D3DERR_INVALIDCALL);CHECK(mapped==(LockPointer)(uintptr_t)0x1234&&!memcmp(&b,&saved,sizeof(b)));}CHECK(lock(&b,ib,7,0,&mapped,0)==D3D_OK&&b.lock_size==57&&(BYTE*)mapped==bytes+7);CHECK(unlock(&b,ib)==D3D_OK);CHECK(lock(&b,ib,64,0,&mapped,0)==D3D_OK&&b.lock_size==0&&(BYTE*)mapped==bytes+64);CHECK(unlock(&b,ib)==D3D_OK);CHECK(lock(&b,ib,0,13,&mapped,0)==D3D_OK);fail_emit=1;CHECK(unlock(&b,ib)==D3DERR_DRIVERINTERNALERROR&&!b.locked&&!b.lock_flags&&!b.lock_size);fail_emit=0;}
 long page=sysconf(_SC_PAGESIZE);size_t reserve=16u*1024u*1024u;BYTE *guard=mmap(NULL,reserve,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(guard!=MAP_FAILED&&page>0&&mprotect(guard,(size_t)page,PROT_READ|PROT_WRITE)==0);for(int ib=0;ib<2;++ib){Buffer b={.length=(UINT)reserve,.usage=512,.pool=0,.shadow=guard};LockPointer mapped=NULL;fputs("buffer lock guard: DISCARD must not clear unrequested shadow pages\\n",stderr);CHECK(lock(&b,ib,0,13,&mapped,8192)==D3D_OK&&(BYTE*)mapped==guard);CHECK(unlock(&b,ib)==D3D_OK);}CHECK(munmap(guard,reserve)==0);
 ${api===9?`/* Production core's API8 nested-map contract; ordinary9 still refuses. */
 const UINT hints[]={0,16,4096,8192};
 for(UINT api=8;api<=9;api++)for(int ib=0;ib<2;ib++)for(UINT a=0;a<4;a++)for(UINT b=0;b<4;b++)for(UINT c=0;c<2;c++){
  compatibility_api=api;BYTE bytes[64];memset(bytes,0xa5,64);Buffer buffer={.handle=41,.length=64,.usage=512,.shadow=bytes,.clips={1}};void *first,*second,*third;uploads=0;
  CHECK(lock(&buffer,ib,7,13,&first,hints[a])==D3D_OK&&buffer.lock_count==1);
  Buffer before=buffer;void*sentinel=(void*)(uintptr_t)99;
  CHECK(lock(&buffer,ib,63,2,&sentinel,0)==D3DERR_INVALIDCALL&&sentinel==(void*)(uintptr_t)99&&!memcmp(&buffer,&before,sizeof(buffer)));
  HRESULT hr=lock(&buffer,ib,2,5,&second,hints[b]);
  if(api==9){CHECK(hr==D3DERR_INVALIDCALL&&!memcmp(&buffer,&before,sizeof(buffer)));CHECK(unlock(&buffer,ib)==D3D_OK);cases++;continue;}
  CHECK(hr==D3D_OK&&buffer.lock_count==2&&second==bytes+2);
  CHECK(lock(&buffer,ib,40,7,&third,c?16:0)==D3D_OK&&buffer.lock_count==3&&third==bytes+40);
  if(hints[a]!=16)memset(first,0x11,13);if(hints[b]!=16)memset(second,0x22,5);if(!c)memset(third,0x33,7);
  CHECK(buffer.lock_offset==2&&buffer.lock_size==45);
  CHECK(unlock(&buffer,ib)==D3D_OK&&buffer.lock_count==2&&buffer.locked&&!uploads);
  CHECK(unlock(&buffer,ib)==D3D_OK&&buffer.lock_count==1&&buffer.locked&&!uploads);
  CHECK(unlock(&buffer,ib)==D3D_OK&&!buffer.lock_count&&!buffer.locked);
  BOOL readonly=hints[a]==16&&hints[b]==16&&c;CHECK(uploads==!readonly);
  if(!readonly)CHECK(upload_offset==2&&upload_size==45&&!upload_flags&&!memcmp(uploaded,bytes+2,45));
  CHECK(unlock(&buffer,ib)==D3DERR_INVALIDCALL);cases++;
 }
 compatibility_api=8;BYTE bytes[8]={0};Buffer overflow={.length=8,.shadow=bytes,.locked=TRUE,.lock_count=UINT32_MAX},old=overflow;void*sentinel=(void*)(uintptr_t)99;
 CHECK(vb_lock(&overflow,0,8,&sentinel,0)==D3DERR_INVALIDCALL&&sentinel==(void*)(uintptr_t)99&&!memcmp(&overflow,&old,sizeof(old)));compatibility_api=9;
 `:""}
 printf("PASS actual API ${api} VB/IB Lock: %u frozen policy cases, seven XP HRESULT observations, ignored/combined hints, exact uploads/read-only preservation, range/failure atomicity bounded DISCARD shadow access and production API8 nested range/readonly/last-unlock semantics\\n",cases);return 0;}
`;
 const input=dir+"/d3d"+api+".c",bin=dir+"/d3d"+api;fs.writeFileSync(input,code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-Wno-misleading-indentation",...(process.env.D9WG_SANITIZE?["-fsanitize=address,undefined","-fno-omit-frame-pointer"]:[]),"-I",root,"-I",root+"/runtime",input,process.env.D9WG_BUFFER_LOCK_POLICY_SOURCE||root+"/runtime/d9wg_buffer_lock.c","-o",bin],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(bin,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr+" native signal="+run.signal);process.stdout.write(run.stdout);
}}finally{fs.rmSync(dir,{recursive:true,force:true});}
