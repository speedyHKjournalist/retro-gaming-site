"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-index-bounds-"));
try{
 const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "runtime/d9wg_vertex.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"index bounds line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
static uint32_t seed=0x693f7a01;
static uint32_t random_word(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(void){
 D9WGVertexStream streams[16]={{0}};uint32_t frequencies[16];for(unsigned i=0;i<16;++i)frequencies[i]=1;
 D9WGVertexElement elements[3]={{15,4,2,0,5,0},{7,3,4,0,10,0},{15,20,0,0,5,1}};
 D9WGDrawIndexBounds out={0},before={31,73};
 for(unsigned test=0;test<100000;++test){
  unsigned primary=1+random_word()%24,secondary=1+random_word()%24;
  streams[15]=(D9WGVertexStream){NULL,24+(primary-1)*28,0,28};
  unsigned offset=1+random_word()%33;streams[7]=(D9WGVertexStream){NULL,7+(secondary-1)*11+offset,offset,11};
  int32_t base=(int32_t)(random_word()%49)-24;
  /* Enumerate byte addresses independently, including invalid signed starts. */
  unsigned lower=100,upper=0;for(unsigned raw=0;raw<100;++raw){int64_t physical=(int64_t)raw+base;
   if(physical>=0&&24+physical*28<=streams[15].length&&offset+7+physical*11<=streams[7].length){if(raw<lower)lower=raw;upper=raw+1;}}
  out=before;int accepted=d9wg_draw_index_bounds(elements,3,streams,frequencies,base,&out);
  CHECK(accepted==(upper>lower));if(accepted)CHECK(out.min_vertex==lower&&out.vertex_limit==upper);else CHECK(!memcmp(&out,&before,sizeof(out)));
 }
 streams[15]=(D9WGVertexStream){NULL,24,0,0};streams[7]=(D9WGVertexStream){NULL,12,5,0};
 CHECK(d9wg_draw_index_bounds(elements,3,streams,NULL,INT32_MIN,&out)&&out.min_vertex==0x80000000u&&out.vertex_limit==UINT32_MAX);
 CHECK(d9wg_draw_index_bounds(elements,3,streams,NULL,INT32_MAX,&out)&&out.min_vertex==0&&out.vertex_limit==UINT32_MAX);
 streams[15].length=23;out=before;CHECK(!d9wg_draw_index_bounds(elements,3,streams,NULL,0,&out)&&!memcmp(&out,&before,sizeof(out)));streams[15].length=24;
 streams[7]=(D9WGVertexStream){NULL,30,1,11};frequencies[0]=0x40000005u;frequencies[7]=0x80000002u;
 CHECK(d9wg_draw_index_bounds(elements,3,streams,frequencies,-1,&out)&&out.min_vertex==1&&out.vertex_limit==UINT32_MAX);
 streams[7].length=29;out=before;CHECK(!d9wg_draw_index_bounds(elements,3,streams,frequencies,0,&out)&&!memcmp(&out,&before,sizeof(out)));streams[7].length=30;
 frequencies[7]=0x80000000u;CHECK(!d9wg_draw_index_bounds(elements,3,streams,frequencies,0,&out));frequencies[7]=1;
 frequencies[0]=0x40000000u;CHECK(!d9wg_draw_index_bounds(elements,3,streams,frequencies,0,&out));frequencies[0]=1;
 elements[2].stream=16;CHECK(!d9wg_draw_index_bounds(elements,3,streams,NULL,0,&out));elements[2].stream=15;
 elements[2].type=17;CHECK(!d9wg_draw_index_bounds(elements,3,streams,NULL,0,&out));elements[2].type=0;
 CHECK(!d9wg_draw_index_bounds(elements,0,streams,NULL,0,&out));CHECK(!d9wg_draw_index_bounds(elements,65,streams,NULL,0,&out));
 uint8_t raw[13]={0};uint16_t short_indices[3]={2,4,3};memcpy(raw+1,short_indices,6);D9WGDrawIndexBounds readable={1,6};
 CHECK(d9wg_draw_status_hints(&readable,2,3,raw+1,3,2));
 CHECK(!d9wg_draw_status_hints(&readable,2,2,raw+1,3,2));
 CHECK(!d9wg_draw_status_hints(&readable,2,0,raw+1,3,2));
 CHECK(!d9wg_draw_status_hints(&readable,UINT32_MAX,2,raw+1,3,2));
 CHECK(!d9wg_draw_status_hints(&readable,0,5,raw+1,3,2));
 CHECK(!d9wg_draw_status_hints(&readable,2,5,raw+1,3,2));
 uint32_t wide_indices[3]={4,2,3};memcpy(raw+1,wide_indices,12);
 CHECK(d9wg_draw_status_hints(&readable,2,3,raw+1,3,4));
 CHECK(!d9wg_draw_status_hints(&readable,3,2,raw+1,3,4));
 CHECK(!d9wg_draw_status_hints(&readable,2,3,raw+1,3,3));
 puts("PASS shared indexed-draw bounds: 100000 independently enumerated multi-stream ranges, signed extrema, exact extents, constant/instance streams and atomic failures; optional software hints and unaligned 16/32-bit indices");return 0;
}`;
 const input=dir+"/bounds.c",bin=dir+"/bounds";fs.writeFileSync(input,code);
 const sources=["clip","blend","vertex","fvf","math","lighting","tnl_math"].map(n=>process.env["D9WG_"+n.toUpperCase()+"_SOURCE"]||root+"/runtime/d9wg_"+n+".c");
 const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-I",root,"-I",root+"/runtime",input,...sources,"-o",bin],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(bin,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
