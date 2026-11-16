"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"depth-layout8-"));
try{const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "${process.env.D9WG_DEPTH_LAYOUT_HEADER||root+'/runtime/d9wg_depth_surface_layout8.h'}"
static uint32_t checks,cases;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Depth layout8 line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
static uint32_t bytes_per_pixel(uint32_t f){return f==73||f==80?2:f==71||f==75||f==77||f==79?4:0;}
int main(void){const uint32_t dims[]={0,1,2,3,4,5,17,8192,8193,0x40000000u,0x80000000u,0xffffffffu};for(uint32_t f=0;f<122+sizeof(d9wg_format_layouts)/sizeof(*d9wg_format_layouts);f++)for(uint32_t x=0;x<12;x++)for(uint32_t y=0;y<12;y++)for(uint32_t null_out=0;null_out<4;null_out++){
 const uint32_t format=f<122?f:d9wg_format_layouts[f-122].format,b=bytes_per_pixel(format),w=dims[x],h=dims[y];__uint128_t pitch=((__uint128_t)w*b+3)/4*4,total=pitch*h;int good=b&&w&&h&&pitch<=UINT32_MAX&&total<=UINT32_MAX&&!null_out;struct{uint32_t pre,pitch,bytes,post;}out={0x1234abcd,123,456,0x87654321},before=out;
 CHECK(d9wg_depth_surface_layout8(format,w,h,null_out&1?NULL:&out.pitch,null_out&2?NULL:&out.bytes)==good);if(good)CHECK(out.pitch==(uint32_t)pitch&&out.bytes==(uint32_t)total&&out.pre==before.pre&&out.post==before.post);else CHECK(!memcmp(&out,&before,sizeof(out)));cases++;
}
printf("PASS pure depth layout8: %u checks,%u cases;six formats,2/4-byte pixels,four-byte row alignment,MSAA-independent logical size,128-bit oracle,overflow/null/zero/atomic output guards\\n",checks,cases);return 0;}
`;fs.writeFileSync(dir+"/layout.c",code);const b=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror',dir+'/layout.c','-o',dir+'/layout'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+'/layout',[],{encoding:'utf8'});assert.equal(r.status,0,r.stdout+r.stderr+' signal='+r.signal);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
