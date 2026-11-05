"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-clip-cache-"));
try {
    const source=fs.readFileSync(root+"/ddrawproxy/d3d7_proxy.inc","utf8");
    const start=source.indexOf("static BOOL d3d7_draw_clip_status_locked("),end=source.indexOf("static BOOL d3d7_emit_draw_up_raw(",start);
    assert(start>0&&end>start);
    fs.writeFileSync(dir+"/windows.h",`#include <stdlib.h>
static int fail_allocation;
static inline void *GetProcessHeap(void){return 0;}
static inline void *HeapAlloc(void *heap,unsigned flags,size_t bytes){(void)heap;(void)flags;return fail_allocation?0:malloc(bytes);}
static inline int HeapFree(void *heap,unsigned flags,void *p){(void)heap;(void)flags;free(p);return 1;}
`);
    const code=`#include <stdio.h>
#include <string.h>
#include "runtime/d9wg_vertex_windows.h"
#include "runtime/d9wg_fvf.h"
#include "runtime/d9wg_screen_clip_cache.h"
typedef int BOOL;typedef uint32_t DWORD;typedef uint16_t WORD;
#define TRUE 1
#define FALSE 0
#define D3DRENDERSTATE_CLIPPING 136
#define D3DTRANSFORMSTATE_WORLD 1
#define D3DTRANSFORMSTATE_VIEW 2
#define D3DTRANSFORMSTATE_PROJECTION 3
#define ZeroMemory(p,n) memset(p,0,n)
#define heap_free(p) free(p)
static unsigned lock_depth;
static int g_transport_lock;
#define EnterCriticalSection(p) ((void)(p),++lock_depth)
#define LeaveCriticalSection(p) ((void)(p),--lock_depth)
typedef struct {uint32_t dwX,dwY,dwWidth,dwHeight;float dvMinZ,dvMaxZ;} Viewport;
typedef struct {uint32_t render_states[256];Viewport viewport;float transforms[8][16],clip_planes[6][4];D9WGScreenClipCache screen_clip_cache;} D3D7Device;
static unsigned computations;
static uint32_t counted_clip_status(const D9WGClipDrawSource *s,const void *rs,const void *w,uint32_t wc,uint32_t gap,const void *v,const void *p,const void *planes,D9WGClipStatus *status){
 if(lock_depth!=1)abort();
 ++computations;return d9wg_vertex_windows_clip_status(s,rs,w,wc,gap,v,p,planes,status);
}
${source.slice(start,end).replace("d9wg_vertex_windows_clip_status(","counted_clip_status(")}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"cache assertion line %d\\n",__LINE__);return 1;}}while(0)
static int compare(D3D7Device *d,float vertices[8][8],uint32_t count,WORD *indices,uint32_t index_count){
 D9WGClipLegacyStatus actual={0},expected={0};actual.status=expected.status=0x01fff800u;
 D3D7Device fresh=*d;memset(&fresh.screen_clip_cache,0,sizeof(fresh.screen_clip_cache));
 int a=d3d7_draw_clip_status(d,0x1c4,vertices,count,indices,index_count,1,&actual);
 int b=d3d7_draw_clip_status(&fresh,0x1c4,vertices,count,indices,index_count,1,&expected);
 free(fresh.screen_clip_cache.positions);return a==b&&!memcmp(&actual,&expected,sizeof(actual));
}
int main(void){
 D3D7Device d={0};d.render_states[136]=1;d.viewport=(Viewport){7,11,800,600,0,1};
 for(unsigned m=0;m<8;++m)for(unsigned i=0;i<4;++i)d.transforms[m][i*5]=1;
 float vertices[8][8]={{0}};for(unsigned i=0;i<8;++i){vertices[i][0]=100+i;vertices[i][1]=200+i;vertices[i][2]=.5f;vertices[i][3]=1;}
 WORD indices[3]={0,1,2};D9WGClipLegacyStatus status={0};status.status=0x01fff000u;
 CHECK(d3d7_draw_clip_status(&d,0x1c4,vertices,8,indices,3,1,&status)&&computations==1);
 for(unsigned i=0;i<1000;++i)CHECK(d3d7_draw_clip_status(&d,0x1c4,vertices,8,indices,3,1,&status));
 CHECK(computations==1&&!lock_depth); /* repeated draws really avoid all floating-point classification */
 vertices[7][0]=-100;CHECK(compare(&d,vertices,8,indices,3));
 CHECK(d.screen_clip_cache.status.clip_union&1); /* unreferenced vertex participates */
 unsigned before=computations;vertices[7][7]=123;CHECK(d3d7_draw_clip_status(&d,0x1c4,vertices,8,indices,3,1,&status)&&computations==before);
 for(unsigned component=0;component<4;++component){vertices[7][component]=-2.5f;CHECK(compare(&d,vertices,8,indices,3));}
 d.viewport.dwX=300;CHECK(compare(&d,vertices,8,indices,3));
 d.viewport.dwWidth=0;CHECK(compare(&d,vertices,8,indices,3));d.viewport.dwWidth=800;
 CHECK(compare(&d,vertices,4,indices,3));CHECK(compare(&d,vertices,8,indices,3));
 for(unsigned i=0;i<8;++i)for(unsigned c=0;c<4;++c){
  uint32_t bits[]={0,0x80000000u,0x7fc00001u,0x7f800000u,0xff800000u,1,0x7f7fffffu};
  for(unsigned b=0;b<sizeof(bits)/sizeof(bits[0]);++b){memcpy(&vertices[i][c],bits+b,4);CHECK(compare(&d,vertices,8,indices,3));}
 }
 status.status=0x01234567u;D9WGClipLegacyStatus saved=status;indices[2]=8;
 CHECK(!d3d7_draw_clip_status(&d,0x1c4,vertices,8,indices,3,1,&status)&&!memcmp(&status,&saved,sizeof(status)));
 indices[2]=2;CHECK(!d3d7_draw_clip_status(&d,0x1c4,vertices,8,indices,0,1,&status));
 CHECK(!d3d7_draw_clip_status(&d,0x1c4,vertices,0,indices,3,1,&status)&&!lock_depth);
 free(d.screen_clip_cache.positions);memset(&d.screen_clip_cache,0,sizeof(d.screen_clip_cache));fail_allocation=1;
 CHECK(compare(&d,vertices,8,indices,3)&&!d.screen_clip_cache.valid);fail_allocation=0;
 puts("PASS actual DDraw screen ClipStatus cache: 1000 reuse hits, mutable/unreferenced positions, viewport/count invalidation, non-finite bit patterns, invalid-index atomicity and allocation fallback");return 0;
}
`;
    fs.writeFileSync(dir+"/test.c",code);
    const sources=["math","clip","blend","vertex","fvf","lighting","tnl_math"].map(n=>root+"/runtime/d9wg_"+n+".c");
    const built=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-ffp-contract=off","-Wall","-Wextra","-Werror",...(process.env.D9WG_SANITIZE?["-fsanitize=address,undefined","-fno-omit-frame-pointer"]:[]),"-I",dir,"-I",root,"-I",root+"/runtime",dir+"/test.c",...sources,"-o",dir+"/test"],{encoding:"utf8"});
    assert.equal(built.status,0,built.stderr);
    const run=spawnSync(dir+"/test",[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
