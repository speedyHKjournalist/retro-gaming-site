"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const root = path.resolve(__dirname, ".."), directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-status-"));
const reference = JSON.parse(fs.readFileSync(path.join(__dirname, "fixtures/d9wg_d3d7_native_clip_status_reference.json")));
try {
    fs.writeFileSync(path.join(directory, "windows.h"), `#include <stdlib.h>
static __inline void *GetProcessHeap(void){return 0;}
static __inline void *HeapAlloc(void *heap,unsigned flags,size_t bytes){(void)heap;(void)flags;return malloc(bytes);}
static __inline int HeapFree(void *heap,unsigned flags,void *p){(void)heap;(void)flags;free(p);return 1;}
`);
    const input = path.join(directory, "status.c"), binary = path.join(directory, "status");
    const caseSource = `#include <string.h>
#ifdef _WIN32
#define STATUS_ERROR(...) ((void)0)
#else
#include <stdio.h>
#define STATUS_ERROR(...) fprintf(stderr,__VA_ARGS__)
#endif
#include ${JSON.stringify(process.env.D9WG_VERTEX_WINDOWS_HEADER || path.join(root, "runtime/d9wg_vertex_windows.h"))}
static const uint32_t expected[100][8]={${reference.rows.map(r => "{" + r.words.map(x => x + "u").join(",") + "}").join(",")}};
static const uint32_t flag_results[8]={${reference.setFlags.map(r => r.hr + "u").join(",")}};
static D9WGClipLegacyStatus legacy;
static D9WGProcessLayout source_layout,output_layout;
static D9WGProcessViewport viewport={7,11,100,80,.25f,.75f};
static uint32_t states[256],row;
static float identity[16],planes[24];
#define CHECK(c) do{if(!(c)){STATUS_ERROR("clip status mismatch line %d row %u\\n",__LINE__,row);return __LINE__;}}while(0)
static int query(uint32_t flags){uint32_t actual[8];memset(actual,0xcc,sizeof(actual));actual[0]=flags;d9wg_clip_legacy_get(&legacy,actual);
 for(unsigned c=0;c<8;++c)if(actual[c]!=expected[row][c]){STATUS_ERROR("clip status mismatch row %u component %u %08x != %08x\\n",row,c,actual[c],expected[row][c]);return 1;}
 ++row;return 0;}
static int process(const float vertices[3][3],unsigned operation){
 D9WGClipBatch *batch=0;float output[3][4];D9WGVertexState state;d9wg_vertex_state_init(&state,states);static D9WGTNLLighting lighting;
 if(operation){uint32_t bytes=d9wg_clip_batch_bytes(3);batch=malloc(bytes);if(!batch)return 1;
  if(!d9wg_clip_batch_init(batch,bytes,0,16,3,&viewport))return 1;
  state.clip_vertices=batch->vertex;
  state.clip_guard_band[0]=state.clip_guard_band[1]=-4096;state.clip_guard_band[2]=state.clip_guard_band[3]=4095;}
 if(!d9wg_process_vertices(&source_layout,vertices,36,0,12,0,&output_layout,output,sizeof(output),0,3,identity,identity,&viewport,&lighting,1,&state))return 1;
 d9wg_clip_legacy_processed(&legacy,batch,output,16,3,operation==2,&viewport);free(batch);return 0;
}
static int draw(const float vertices[3][3],unsigned clipping){
 states[136]=clipping;D9WGClipDrawSource source={0};source.vertices=(const uint8_t *)vertices;source.length=36;source.stride=12;
 source.layout=&source_layout;source.count=source.vertex_count=3;
 D9WGClipStatus status={legacy.status&4095u,legacy.status>>12&4095u};
 if(d9wg_vertex_windows_clip_status(&source,states,identity,1,0,identity,identity,planes,&status))return 1;
 legacy.status=(legacy.status&0xff000000u)|status.clip_union|(status.clip_intersection<<12);return 0;
}
static int draw_screen(const float vertices[3][4],unsigned clipping,unsigned mask){
 states[136]=clipping;states[152]=mask;D9WGClipDrawSource source={0};source.vertices=(const uint8_t *)vertices;source.length=48;source.stride=16;
 source.layout=&output_layout;source.count=source.vertex_count=3;source.status_screen=1;source.viewport=viewport;
 D9WGClipStatus status={legacy.status&4095u,legacy.status>>12&4095u};
 if(d9wg_vertex_windows_clip_status(&source,states,identity,1,0,identity,identity,planes,&status))return 1;
 legacy.status=(legacy.status&0xff000000u)|status.clip_union|(status.clip_intersection<<12);return 0;
}
static int clip_status_cases(void){
 row=0;memset(states,0,sizeof(states));memset(planes,0,sizeof(planes));memset(identity,0,sizeof(identity));
 viewport=(D9WGProcessViewport){7,11,100,80,.25f,.75f};
 CHECK(d9wg_process_fvf(7,2,&source_layout)&&d9wg_process_fvf(7,4,&output_layout));for(unsigned i=0;i<4;++i)identity[i*5]=1;
 d9wg_clip_legacy_init(&legacy);for(unsigned flags=0;flags<8;++flags)CHECK(!query(flags));
 static const float vertices[2][3][3]={{{-2,-.5f,.5f},{-3,.5f,.5f},{-4,0,.5f}},{{-2,-.5f,.5f},{.5f,-.5f,.5f},{0,.5f,.5f}}};
 for(unsigned test=0;test<12;++test){unsigned object=test%2u,clipping=test/2u%2u,operation=test/4u;
  D9WGClipLegacyStatus initial={3,0x00fff000u,10000,-10000,10000,-10000,10000,-10000};
  CHECK(d9wg_clip_legacy_set(&legacy,&initial)&&!query(3));CHECK(!process(vertices[object],operation)&&!query(3));CHECK(!query(3));
  CHECK(d9wg_clip_legacy_set(&legacy,&initial)&&!query(3));CHECK(d9wg_clip_legacy_set(&legacy,&initial));CHECK(!draw(vertices[object],clipping)&&!query(3));
 }
 for(unsigned flags=0;flags<8;++flags){D9WGClipLegacyStatus initial={flags,0x01234567u,10,20,30,40,50,60};
  uint32_t hr=d9wg_clip_legacy_set(&legacy,&initial)?0:0x80070057u;CHECK(hr==flag_results[flags]&&!query(7));}
 D9WGClipLegacyStatus initial={1,0x01fff800u,0,0,0,0,0,0};CHECK(d9wg_clip_legacy_set(&legacy,&initial));
 for(unsigned object=0;object<2;++object){float v[3][3];memcpy(v,vertices[0],sizeof(v));if(object)for(unsigned i=0;i<3;++i)v[i][0]=-v[i][0];CHECK(!process(v,1)&&!query(3));}
 CHECK(!draw(vertices[1],1)&&!query(3));
 for(unsigned edge=0;edge<6;++edge){float v[3][3]={{-.5f,-.5f,.5f},{.5f,-.5f,.5f},{0,.5f,.5f}};
  for(unsigned i=0;i<3;++i){if(edge<2)v[i][0]+=edge?3:-3;else if(edge<4)v[i][1]+=edge==2?-3:3;else if(edge==5){v[i][0]=.137f;v[i][1]=-.173f;}}
  initial=(D9WGClipLegacyStatus){3,0x01fff000u,10000,-10000,10000,-10000,0,0};CHECK(d9wg_clip_legacy_set(&legacy,&initial)&&!process(v,2)&&!query(3));}

 initial=(D9WGClipLegacyStatus){3,0x00fff000u,10000,-10000,10000,-10000,0,0};
 float all[5][3]={{-2,-.5f,.5f},{-3,.5f,.5f},{-4,0,.5f},{0,0,.5f},{3,0,.5f}};
 uint16_t selections[3][3]={{0,1,2},{3,3,3},{2,0,1}};
 for(unsigned selection=0;selection<3;++selection){
  CHECK(d9wg_clip_legacy_set(&legacy,&initial));D9WGClipDrawSource source={0};source.vertices=(const uint8_t *)all;source.length=sizeof(all);source.stride=12;
  source.layout=&source_layout;source.count=3;source.vertex_count=5;source.indices=(const uint8_t *)selections[selection];source.index_stride=2;source.index_bytes=6;
  D9WGClipStatus codes={legacy.status&4095u,legacy.status>>12&4095u};states[136]=1;
  CHECK(!d9wg_vertex_windows_clip_status(&source,states,identity,1,0,identity,identity,planes,&codes));
  legacy.status=codes.clip_union|(codes.clip_intersection<<12);CHECK(!query(3));
 }
 for(unsigned clipping=0;clipping<2;++clipping){CHECK(d9wg_clip_legacy_set(&legacy,&initial)&&!draw((const float (*)[3])all,clipping)&&!query(3));}
 float screen[3][4]={{-5,40,.5f,1},{-10,60,.5f,1},{-15,50,.5f,1}};
 for(unsigned clipping=0;clipping<2;++clipping){CHECK(d9wg_clip_legacy_set(&legacy,&initial)&&!draw_screen(screen,clipping,0)&&!query(3));}
 planes[0]=1;screen[0][0]=32;screen[1][0]=55;screen[2][0]=42;
 CHECK(d9wg_clip_legacy_set(&legacy,&initial)&&!draw_screen(screen,1,1)&&!query(3));
 CHECK(d9wg_clip_legacy_set(&legacy,&initial)&&!query(3));
 planes[0]=-1;CHECK(d9wg_clip_legacy_set(&legacy,&initial)&&!draw_screen(screen,1,1)&&!query(3));
 const float depths[5]={.1f,.9f,-.1f,1.1f,.5f};
 for(unsigned depth=0;depth<5;++depth){for(unsigned i=0;i<3;++i)screen[i][2]=depths[depth];if(depth==4)viewport.min_z=viewport.max_z=0;
  CHECK(d9wg_clip_legacy_set(&legacy,&initial)&&!draw_screen(screen,1,0)&&!query(3));}
 CHECK(row==100);
 /* Boolean per-plane histories are an independent oracle for OR/AND state. */
 uint32_t random=0x391a864bu,hits[12]={0};D9WGClipStatus status;d9wg_clip_status_init(&status);CHECK(status.clip_union==0&&status.clip_intersection==0xffffffffu);
 for(uint32_t n=1;n<=100000;++n){random=random*1664525u+1013904223u;uint32_t code=random&4095u,any=0,every=0;d9wg_clip_status_merge(&status,code,code);
  for(unsigned p=0;p<12;++p){hits[p]+=(code>>p)&1u;if(hits[p])any|=1u<<p;if(hits[p]==n)every|=1u<<p;}CHECK(status.clip_union==any&&status.clip_intersection==every);}
 d9wg_clip_status_processed(&status,0);CHECK(status.clip_union==0&&status.clip_intersection==0);
 /* All vertices in the declared processing range contribute; offsets and signed bases
  * are checked before the first accumulated bit changes. */
 float data[7][3]={{4,0,.5f},{3,0,.5f},{-2,0,.5f},{-3,0,.5f},{-4,0,.5f},{0,3,.5f},{0,0,.5f}};
 uint16_t indices[3]={3,4,5};D9WGClipDrawSource s={0};s.vertices=(const uint8_t *)data;s.length=sizeof(data);s.base=12;s.stride=12;s.layout=&source_layout;
 s.indices=(const uint8_t *)indices;s.index_stride=2;s.index_bytes=6;s.count=3;s.min_vertex=3;s.vertex_count=6;s.base_vertex=-2;
 D9WGVertexState state;d9wg_vertex_state_init(&state,states);d9wg_clip_status_init(&status);
 CHECK(d9wg_vertex_clip_codes(&s,identity,&state,&status)&&status.clip_union==1&&status.clip_intersection==1);
 indices[0]=2;CHECK(!d9wg_vertex_clip_codes(&s,identity,&state,&status)&&status.clip_union==1&&status.clip_intersection==1);
 indices[0]=6;CHECK(!d9wg_vertex_clip_codes(&s,identity,&state,&status)&&status.clip_union==1&&status.clip_intersection==1);
 float independent_screen[3][4]={{10,10,.5f,1},{20,20,.5f,1},{30,10,.5f,1}};
 s=(D9WGClipDrawSource){0};s.vertices=(const uint8_t *)independent_screen;s.length=sizeof(independent_screen);s.stride=16;s.layout=&output_layout;s.count=s.vertex_count=3;
 status=(D9WGClipStatus){0x800u,0xffffffffu};states[136]=0;
 CHECK(!d9wg_vertex_windows_clip_status(&s,states,identity,1,0,identity,identity,planes,&status)&&status.clip_union==0x800u&&status.clip_intersection==0xffffffffu);
 s.vertices=(const uint8_t *)data;s.length=sizeof(data);s.stride=12;s.layout=&source_layout;
 status=(D9WGClipStatus){0x800u,0xffffffffu};
 CHECK(!d9wg_vertex_windows_clip_status(&s,states,identity,1,0,identity,identity,planes,&status)&&status.clip_union==0&&status.clip_intersection==0);
 float inside[3][3]={{-.5f,-.5f,.5f},{-.25f,.5f,.5f},{-.75f,0,.5f}},world[16],user[6][4]={{1,0,0,0}};memcpy(world,identity,sizeof(world));world[12]=.5f;
 d9wg_vertex_clip_planes_init(&state,1,user,world);s.vertices=(const uint8_t *)inside;s.length=sizeof(inside);s.stride=12;s.layout=&source_layout;
 d9wg_clip_status_init(&status);CHECK(d9wg_vertex_clip_codes(&s,identity,&state,&status)&&status.clip_union==64&&status.clip_intersection==0);
 s.base=0xffffffffu;CHECK(!d9wg_vertex_clip_codes(&s,identity,&state,&status)&&status.clip_union==64&&status.clip_intersection==0);
 return 0;
 }
#undef CHECK
#undef STATUS_ERROR
`;
    if (process.env.D9WG_STATUS_HEADER_OUT) {
        fs.writeFileSync(process.env.D9WG_STATUS_HEADER_OUT, caseSource);
        console.log("Prepared portable ClipStatus reference cases");
    } else {
    fs.writeFileSync(input, caseSource + `
#include <stdio.h>
int main(void){int error=clip_status_cases();if(error){fprintf(stderr,"clip status mismatch row %u\\n",row);return 1;}
puts("PASS C ClipStatus: 100 bit-exact native queries, eight flag HRESULTs, untouched Z fields, extents, 100000 histories and atomic indexed bounds");return 0;}
`);
    const sources = ["math", "clip", "blend", "vertex", "fvf", "lighting", "tnl_math"].map(name => process.env["D9WG_" + name.toUpperCase() + "_SOURCE"] || path.join(root, "runtime/d9wg_" + name + ".c"));
    const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror", "-I", directory, "-I", root, "-I", path.join(root, "runtime"), input, ...sources, "-o", binary], { encoding: "utf8" });
    assert.equal(built.status, 0, built.stderr.slice(0, 3000) || String(built.error));
    const run = spawnSync(binary, [], { encoding: "utf8" });assert.equal(run.status, 0, run.stderr.slice(0, 1500));process.stdout.write(run.stdout);
    }
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
