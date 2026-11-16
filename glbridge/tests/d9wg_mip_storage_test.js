"use strict";
// Execute the production allocator with independent offset arithmetic and ASan.
const assert = require("node:assert/strict"), fs = require("node:fs"), os = require("node:os"), path = require("node:path"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = fs.readFileSync(process.env.D9WG_MIP_SOURCE || root + "/d3d9proxy/d3d9_proxy.c", "utf8");
const start = source.indexOf("static BYTE *allocate_mip_shadows("), end = source.indexOf("\n}", start) + 2;
assert(start >= 0 && end > start);
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-mip-storage-"));
try {
    fs.writeFileSync(dir + "/mip.c", `#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "${root}/runtime/d9wg_cpu_texture_layout.h"
typedef uint32_t UINT,D3DFORMAT;typedef unsigned char BYTE;
typedef struct{UINT width,height,byte_count;BYTE *shadow;int shadow_valid;}D9TextureLevel;
#define HEAP_ZERO_MEMORY 8
#define TRUE 1
static unsigned allocations,fail;static size_t allocated_bytes;
static void *GetProcessHeap(void){return NULL;}
static void *HeapAlloc(void *h,unsigned flags,size_t bytes){(void)h;if(flags!=8)abort();allocations++;allocated_bytes=bytes;return fail?NULL:calloc(1,bytes);}
${source.slice(start, end)}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Mip storage line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){unsigned checks=0,cases=0;
 const unsigned formats[]={21,28,50,0x31545844u,0x35545844u,0x31495441u,0x32495441u},dims[]={1,2,7,16,257};
 for(unsigned api=8;api<=9;api++)for(unsigned f=0;f<7;f++)for(unsigned dim=0;dim<5;dim++)for(unsigned faces=1;faces<=6;faces+=5)for(unsigned inject=0;inject<2;inject++){
  if(api==8&&f>=5)continue;
  D9TextureLevel levels[54]={0};unsigned width=dims[dim],count=1;for(unsigned w=width;w>1;w>>=1)count++;
  unsigned sizes[9]={0},offsets[54]={0},total=0,extent=0;
  for(unsigned face=0;face<faces;face++){
   for(unsigned mip=0;mip<count;mip++){
    unsigned size=width>>mip,pitch,rows,bytes;if(!size)size=1;
    CHECK(d9wg_cpu_texture_level_layout(api,formats[f],size,size,&pitch,&rows,&bytes));
    unsigned expected_pitch=f==0?size*4:f<3?size:((size+3)/4)*(f==3||f==5?8:16);
    unsigned expected_rows=f<3?size:(size+3)/4;
    unsigned physical=expected_pitch*expected_rows,span=f>=5?size*size:physical;
    unsigned allocated=span>physical?span:physical;
    CHECK(pitch==expected_pitch&&rows==expected_rows&&bytes==allocated);
    sizes[mip]=span;offsets[face*count+mip]=total;
    if(total+allocated>extent)extent=total+allocated;
    total+=span;
    levels[face*count+mip].width=levels[face*count+mip].height=size;
    levels[face*count+mip].byte_count=bytes;
   }
   total=(total+15u)/16u*16u;
  }
  if(extent>total)total=extent;
  D9TextureLevel before[54];memcpy(before,levels,sizeof(levels));fail=inject;unsigned old=allocations;
  BYTE *storage=allocate_mip_shadows(levels,count,faces,api,formats[f]);
  CHECK(allocations==old+1&&allocated_bytes==total);
  if(inject){CHECK(!storage&&!memcmp(before,levels,sizeof(levels)));cases++;continue;}
  CHECK(storage);
  for(unsigned face=0;face<faces;face++)for(unsigned mip=0;mip<count;mip++){
   D9TextureLevel *level=&levels[face*count+mip];CHECK(level->shadow==storage+offsets[face*count+mip]&&level->shadow_valid);
   CHECK(level->shadow+level->byte_count<=storage+total);
   for(unsigned b=0;b<level->byte_count;b++)CHECK(level->shadow[b]==0);
  }
  /* Public ATI mip spans are contiguous even though the final compressed
   * block can overlap the next span; reserve the complete physical tail. */
  for(unsigned face=0;face<faces;face++)for(unsigned mip=0;mip<count;mip++)memset(levels[face*count+mip].shadow,1+face*9+mip,sizes[mip]);
  for(unsigned face=0;face<faces;face++)for(unsigned mip=0;mip<count;mip++)for(unsigned b=0;b<sizes[mip];b++)CHECK(levels[face*count+mip].shadow[b]==1+face*9+mip);
  free(storage);cases++;
 }
 const unsigned huge[]={UINT32_MAX,UINT32_MAX-7,UINT32_MAX-16};
 for(unsigned i=0;i<3;i++){
  D9TextureLevel levels[2]={{.byte_count=huge[i],.shadow=(void*)(uintptr_t)7},{.byte_count=32,.shadow=(void*)(uintptr_t)9}},before[2];memcpy(before,levels,sizeof(levels));unsigned old=allocations;
  CHECK(!allocate_mip_shadows(levels,2,1,8,21)&&allocations==old&&!memcmp(before,levels,sizeof(levels)));cases++;
 }
 printf("PASS production mip allocator: %u checks, %u format/size/face/failure cases; API8/9 contiguous levels, ATI public spans and full physical tail bounds, 16-byte faces, zeroed isolated public writes, atomic OOM/overflow\\n",checks,cases);return 0;}
`);
    const build = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", dir + "/mip.c", "-o", dir + "/mip"], {encoding:"utf8"});
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(dir + "/mip", [], {encoding:"utf8", timeout:30000});
    assert.equal(run.status, 0, run.stdout + run.stderr);
    process.stdout.write(run.stdout);
} finally { fs.rmSync(dir, {recursive:true, force:true}); }
