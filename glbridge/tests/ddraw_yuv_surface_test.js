"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.DDRAW_YUV_SOURCE||root+"/ddrawproxy/ddraw_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-yuv-"));
function extract(name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let at=source.indexOf("{",m.index)+1,depth=1;for(;depth;at++){if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return source.slice(m.index,at);}
const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ddrawproxy/ddraw_protocol.h"
#include "runtime/d9wg_format_layouts.h"
#include "runtime/d9wg_runtime.h"
typedef uint32_t UINT,DWORD;typedef int32_t LONG;typedef uint8_t BYTE;typedef int BOOL;
typedef struct{LONG left,top,right,bottom;}RECT;
typedef struct{BOOL gpu_dirty;}Memory;
typedef struct{Memory*memory;BYTE*shadow;UINT width,height,pitch,bytes_per_pixel,format,handle,mip_level,cube_face;BOOL upload_pending;}DDSurface;
#define TRUE 1
#define FALSE 0
#define DDWG_TRACE_CHECKPOINT(...)
#define CopyMemory(d,s,n) memcpy(d,s,n)
#define ZeroMemory(p,n) memset(p,0,n)
static unsigned checks,uploads;static BOOL fail_storage,fail_upload;static D9WGTextureRows observed;static BYTE uploaded[256];
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"DDraw YUV line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
static BOOL ensure_surface_storage(DDSurface*s){(void)s;return !fail_storage;}
static BOOL emit_texture_rows(const D9WGTextureRows*r){observed=*r;uploads++;for(UINT y=0;y<r->row_count;y++)memcpy(uploaded+y*r->row_bytes,r->source+y*r->source_pitch,r->row_bytes);return !fail_upload;}
${["format_bytes_per_pixel","format_is_yuv","format_compressed_block_bytes","surface_pitch_for","emit_surface_upload","surface_fill_yuv"].map(extract).join("\n")}
int main(void){for(unsigned fmt=0;fmt<2;fmt++){for(UINT width=1;width<12;width++){for(unsigned part=0;part<2;part++){for(unsigned dirty=0;dirty<2;dirty++){
 BYTE bytes[256],before[256];memset(bytes,0x5a,sizeof(bytes));Memory memory={dirty};UINT format=fmt?DDWG_FMT_UYVY:DDWG_FMT_YUY2;CHECK(format_bytes_per_pixel(format)==2&&format_is_yuv(format));UINT pitch=surface_pitch_for(width,2);CHECK(pitch>=((width+1)/2)*4&&pitch%8==0);DDSurface s={.memory=&memory,.shadow=bytes+8,.width=width,.height=3,.pitch=pitch,.bytes_per_pixel=2,.format=format,.handle=7};RECT area=part&&width>2?(RECT){1,1,(LONG)width-1,2}:(RECT){0,0,(LONG)width,3};BOOL full=!area.left&&!area.top&&area.right==(LONG)width&&area.bottom==3;memcpy(before,bytes,256);unsigned old=uploads;DWORD value=0xdeadbeef;BOOL accepted=!dirty||full;
 CHECK(surface_fill_yuv(&s,&area,value)==accepted);CHECK(uploads==old+accepted&&memory.gpu_dirty==(accepted?FALSE:(BOOL)dirty));
 if(!accepted){CHECK(!memcmp(bytes,before,256));continue;}
 for(UINT i=0;i<256;i++){BYTE expected=before[i];for(LONG y=area.top;y<area.bottom;y++){for(LONG x=area.left;x<area.right;x++)for(UINT b=0;b<2;b++)if(i==8+y*pitch+x*2+b)expected=((BYTE*)&value)[(x&1)*2+b];if((width&1)&&area.right==(LONG)width)for(UINT b=0;b<2;b++)if(i==8+y*pitch+width*2+b)expected=((BYTE*)&value)[2+b];}CHECK(bytes[i]==expected);}
 UINT left=(UINT)area.left&~1u,right=((UINT)area.right+1)&~1u;if(right>width)right=width;UINT row_bytes=((right+1)/2-left/2)*4;CHECK(observed.x==left&&observed.y==(UINT)area.top&&observed.width==right-left&&observed.height==(UINT)(area.bottom-area.top)&&observed.row_bytes==row_bytes&&observed.row_count==(UINT)(area.bottom-area.top)&&observed.source_pitch==pitch);for(UINT y=0;y<observed.row_count;y++)CHECK(!memcmp(uploaded+y*row_bytes,bytes+8+(area.top+y)*pitch+left*2,row_bytes));
 s.upload_pending=TRUE;CHECK(emit_surface_upload(&s,&area)&&!s.upload_pending&&observed.x==0&&observed.y==0&&observed.width==width&&observed.height==3&&observed.row_bytes==((width+1)/2)*4);
 fail_storage=TRUE;memcpy(before,bytes,256);old=uploads;CHECK(!surface_fill_yuv(&s,&area,0)&&uploads==old&&!memcmp(bytes,before,256));fail_storage=FALSE;fail_upload=TRUE;CHECK(!surface_fill_yuv(&s,&area,value)&&s.upload_pending&&!memory.gpu_dirty);fail_upload=FALSE;
 }}}}
printf("PASS actual DDraw packed YUV: %u checks; both layouts, odd/even widths and lock edges, exact raw fills, neighbouring bytes and padding, upload metadata, dirty refusal, allocation/upload failure and retry debt\\n",checks);return 0;}
`;
try{fs.writeFileSync(dir+"/test.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer","-I",root,dir+"/test.c","-o",dir+"/test"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/test",[],{encoding:"utf8"});assert.equal(r.status,0,r.stderr);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
