"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const root = path.resolve(__dirname, ".."), directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-clip-mask-"));
try {
    fs.writeFileSync(path.join(directory, "windows.h"), `#include <stdlib.h>
static int allocation_limit=-1,allocations=0;
static void *GetProcessHeap(void){return 0;}
static void *HeapAlloc(void *heap,unsigned flags,size_t bytes){(void)heap;(void)flags;if(allocation_limit==0)return 0;if(allocation_limit>0)--allocation_limit;void *p=malloc(bytes);if(p)++allocations;return p;}
static int HeapFree(void *heap,unsigned flags,void *p){(void)heap;(void)flags;if(p){--allocations;free(p);}return 1;}
`);
    const input = path.join(directory, "mask.c"), binary = path.join(directory, "mask");
    fs.writeFileSync(input, `#include <stdio.h>
#include ${JSON.stringify(process.env.D9WG_VERTEX_WINDOWS_HEADER || path.join(root, "runtime/d9wg_vertex_windows.h"))}
#define CHECK(c) do{if(!(c)){fprintf(stderr,"clip mask mismatch line %d\\n",__LINE__);return 1;}}while(0)
typedef struct Context{uint32_t mask,emits,changes;int fail_emit,fail_suspend,fail_restore;}Context;
static int set_mask(void *context,uint32_t mask){Context *c=context;++c->changes;if((!mask&&c->fail_suspend)||(mask&&c->fail_restore))return 0;c->mask=mask;return 1;}
static int emit(void *context,uint32_t type,const void *vertices,uint32_t count,uint32_t stride){Context *c=context;(void)vertices;if(c->mask || type!=4 || count!=6 || stride!=16)return 0;++c->emits;return !c->fail_emit;}
int main(void){
 float positions[3][4]={{-.5f,-.5f,.5f,1},{.5f,-.5f,.5f,1},{.5f,.5f,.5f,1}},vertices[3][4];
 D9WGProcessViewport vp={0,0,128,128,0,1};float guard[4]={-4096,-4096,4095,4095},planes[24]={1};
 uint32_t bytes=d9wg_clip_batch_bytes(3);D9WGClipBatch *batch=HeapAlloc(0,0,bytes);
 CHECK(d9wg_clip_batch_init(batch,bytes,0,16,3,&vp));
 for(unsigned v=0;v<3;++v){d9wg_clip_vertex(positions[v],&vp,guard,1,&batch->vertex[v]);d9wg_clip_user_vertex(&batch->vertex[v],positions[v],planes,1);__builtin_memcpy(vertices[v],batch->vertex[v].stored,16);}
 D9WGClipBuffer buffer={0};d9wg_clip_buffer_commit(&buffer,batch,0,sizeof(vertices),d9wg_vertex_windows_clip_release,0);
 D9WGProcessLayout layout={0};layout.position_transformed=1;layout.position_floats=4;layout.position_type=3;layout.stride=16;layout.diffuse=layout.specular=layout.normal=layout.psize=-1;for(unsigned i=0;i<8;++i)layout.texcoord[i]=-1;
 D9WGClipDrawSource source={0};source.clips=&buffer;source.vertices=(const uint8_t *)vertices;source.length=sizeof(vertices);source.stride=16;source.count=source.vertex_count=3;source.primitive_type=4;source.layout=&layout;source.viewport=vp;
 Context c={32,0,0,0,0,0};CHECK(d9wg_vertex_windows_clip_draw_planes(&source,emit,set_mask,32,&c)==1 && c.mask==32 && c.emits==1 && c.changes==2 && allocations==1);
 c=(Context){32,0,0,1,0,0};CHECK(d9wg_vertex_windows_clip_draw_planes(&source,emit,set_mask,32,&c)==-2 && c.mask==32 && c.emits==1 && c.changes==2 && allocations==1);
 c=(Context){32,0,0,0,1,0};CHECK(d9wg_vertex_windows_clip_draw_planes(&source,emit,set_mask,32,&c)==-2 && c.mask==32 && !c.emits && c.changes==1 && allocations==1);
 c=(Context){32,0,0,0,0,1};CHECK(d9wg_vertex_windows_clip_draw_planes(&source,emit,set_mask,32,&c)==-2 && c.mask==0 && c.emits==1 && c.changes==2 && allocations==1);
 for(int i=0;i<2;++i){allocation_limit=i;c=(Context){32,0,0,0,0,0};CHECK(d9wg_vertex_windows_clip_draw_planes(&source,emit,set_mask,32,&c)==-3 && c.mask==32 && !c.emits && !c.changes && allocations==1);}
 allocation_limit=-1;d9wg_clip_buffer_free(&buffer,d9wg_vertex_windows_clip_release,0);CHECK(!allocations);
 CHECK(d9wg_vertex_windows_clip_draw_planes(&source,emit,set_mask,32,&c)==0 && !c.changes);
 puts("PASS temporary user-plane state: restore after success/emission failure, visible state failures, both allocation failures and balanced frees");return 0;
}
`);
    const sources = ["clip_draw", "clip", "math"].map(name => process.env["D9WG_" + name.toUpperCase() + "_SOURCE"] || path.join(root, "runtime/d9wg_" + name + ".c"));
    const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror", "-I", directory, "-I", root, "-I", path.join(root, "runtime"), input, ...sources, "-o", binary], { encoding: "utf8" });
    assert.equal(built.status, 0, built.stderr.slice(0, 3000) || String(built.error));
    const run = spawnSync(binary, [], { encoding: "utf8" });assert.equal(run.status, 0, run.stderr.slice(0, 1500));process.stdout.write(run.stdout);
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
