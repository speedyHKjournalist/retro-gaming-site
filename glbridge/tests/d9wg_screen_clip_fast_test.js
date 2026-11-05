"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-screen-clip-"));
try {
    const source=fs.readFileSync(root+"/runtime/d9wg_vertex.c","utf8");
    const call="d9wg_clip_unproject(position, &s->viewport, clip)";assert.equal(source.split(call).length,2);
    fs.writeFileSync(dir+"/vertex.c",'#include "d9wg_vertex.h"\nunsigned observed_unprojects;\nstatic void observed_unproject(const float *s,const D9WGProcessViewport *v,float *c){++observed_unprojects;d9wg_clip_unproject(s,v,c);}\n'+source.replace(call,"observed_unproject(position, &s->viewport, clip)"));
    fs.writeFileSync(dir+"/test.c",`#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "runtime/d9wg_vertex.h"
#include "runtime/d9wg_clip_draw.h"
extern unsigned observed_unprojects;
static uint32_t seed=0x1379abcd;
static uint32_t random_word(void){seed=seed*1664525u+1013904223u;return seed;}
static float from_bits(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"screen clipping mismatch line %d case %u\\n",__LINE__,case_id);return 1;}}while(0)
int main(void){
 unsigned case_id=0;D9WGProcessLayout layout;CHECK(d9wg_process_fvf(7,4,&layout));
 float position[4],identity[16]={0};for(unsigned i=0;i<4;++i)identity[i*5]=1;
 D9WGVertexState state={0};D9WGClipDrawSource source={0};
 source.layout=&layout;source.vertices=(const uint8_t *)position;source.length=source.stride=16;
 source.count=source.vertex_count=source.status_screen=1;
 D9WGProcessViewport views[]={ {0,0,800,600,0,1},{7,11,100,80,.25f,.75f},
  {-1048576,-1048576,1048576,1048576,0,1},{1048576,1048576,1048576,1048576,0,1},
  {-.125f,.125f,3,3,0,1},{0,0,2,2,0,1},{2000000,-2000000,3000000,3000000,0,1} };
 const uint32_t special[]={0,0x80000000u,1,0x80000001u,0x007fffffu,0x00800000u,0x1f7fffffu,0x1f800000u,0x1f800001u,
  0x3f7fffffu,0x3f800000u,0x3f800001u,0x5f7fffffu,0x5f800000u,0x5f800001u,0x7f7fffffu,0x7f800000u,0xff800000u,0x7fc00001u};
 for(case_id=0;case_id<250000;++case_id){
  source.viewport=views[case_id%7];const D9WGProcessViewport *v=&source.viewport;
  position[0]=v->x+((int)(random_word()%4097)-512)/3072.0f*v->width;
  position[1]=v->y+((int)(random_word()%4097)-512)/3072.0f*v->height;
  position[2]=((int)(random_word()%1537)-256)/1024.0f;
  position[3]=from_bits(((random_word()%161+47)<<23) | (random_word()&0x7fffffu));
  if(case_id%3==0)position[case_id%4]=from_bits(special[(case_id/3)%(sizeof(special)/sizeof(special[0]))]);
  if(case_id%5==0){unsigned axis=case_id%2;float edge=axis?v->y:v->x;
   if(case_id%10==0)edge+=axis?v->height:v->width;uint32_t bits;memcpy(&bits,&edge,4);
   position[axis]=from_bits(bits+(case_id%3)-1u);}
  float clip[4];d9wg_clip_unproject(position,v,clip);uint32_t expected=d9wg_clip_outcode(clip);
  D9WGClipStatus status={0x800u,0xffffffffu};
  CHECK(d9wg_vertex_clip_codes(&source,identity,&state,&status));
  CHECK(status.clip_union==(0x800u|expected)&&status.clip_intersection==expected);
 }
 source.viewport=views[0];position[0]=400;position[1]=300;position[2]=.5f;position[3]=1;
 unsigned before=observed_unprojects;D9WGClipStatus status={0,0xffffffffu};
 CHECK(d9wg_vertex_clip_codes(&source,identity,&state,&status)&&!status.clip_union&&!status.clip_intersection&&observed_unprojects==before);
 position[0]=0;CHECK(d9wg_vertex_clip_codes(&source,identity,&state,&status)&&observed_unprojects==before+1);
 puts("PASS screen clipping fast path: 250000 differential vectors, shifted/extreme viewports, adjacent boundary floats, non-finite/subnormal inputs; interior avoids unprojection and edges retain it");return 0;
}
`);
    const sources=["math","clip","blend","fvf","lighting","tnl_math"].map(n=>root+"/runtime/d9wg_"+n+".c");
    const built=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-ffp-contract=off","-Wall","-Wextra","-Werror",...(process.env.D9WG_SANITIZE?["-fsanitize=address,undefined","-fno-omit-frame-pointer"]:[]),"-I",root,"-I",root+"/runtime",dir+"/test.c",dir+"/vertex.c",...sources,"-o",dir+"/test"],{encoding:"utf8"});assert.equal(built.status,0,built.stderr);
    const run=spawnSync(dir+"/test",[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
