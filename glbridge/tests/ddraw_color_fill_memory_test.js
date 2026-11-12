"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.DDRAW_COLOR_FILL_SOURCE||root+"/ddrawproxy/ddraw_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-color-memory-"));
function extract(name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let at=source.indexOf("{",m.index)+1,depth=1;for(;depth;at++){if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return source.slice(m.index,at);}
const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "${root}/ddrawproxy/ddraw_protocol.h"
#include "${root}/runtime/d9wg_format_layouts.h"
typedef uint32_t DWORD,UINT;typedef int32_t LONG;typedef unsigned char BYTE;typedef int BOOL;
typedef struct{LONG left,top,right,bottom;}RECT;
typedef struct{BOOL gpu_dirty;DWORD content_version;BYTE bits[160];}Memory;
typedef Memory DDSurfaceMemory;
#define InterlockedIncrement(p) (++*(p))
${fs.readFileSync(root+"/ddrawproxy/ddraw_proxy.c","utf8").match(/static void surface_memory_changed\([^)]*\)\s*\{[\s\S]*?\n}/)[0]}

typedef struct{DWORD device_handle;}DDrawObject;
typedef struct{DDrawObject*owner;Memory*memory;BYTE*shadow;UINT format,width,height,pitch,bytes_per_pixel,handle,mip_level,cube_face;BOOL upload_pending;struct{struct{DWORD dwCaps;}ddsCaps;}desc;}DDSurface;
#define TRUE 1
#define FALSE 0
#define DDSCAPS_SYSTEMMEMORY 0x800u
#define CopyMemory(d,s,n) memcpy(d,s,n)
#define ZeroMemory(p,n) memset(p,0,n)
#define HOSTLOG_REFUSED(...)
static unsigned checks,emits,reads,prepares;static BOOL fail_emit,fail_read,fail_prepare;static BYTE gpu[160];static D9WGDDBlt command;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"color fill memory line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
static BOOL surface_readback(DDSurface*s){reads++;if(fail_read)return FALSE;memcpy(s->shadow,gpu,160);s->memory->gpu_dirty=FALSE;return TRUE;}
static BOOL ensure_device(DDrawObject*d){return !!d;}
static BOOL sync_external_surface_for_read(DDSurface*s){MUST(s);return TRUE;}
static BOOL ensure_surface_storage(DDSurface*s){MUST(s);return TRUE;}
static BOOL prepare_surface_for_host(DDSurface*s){MUST(s);prepares++;return !fail_prepare;}
static BOOL emit_surface_upload(DDSurface*s,const RECT*r){(void)s;(void)r;MUST(0);return FALSE;}
static BOOL emit_command(UINT op,const void*p,UINT bytes){MUST(op==D9WG_OP_DD_BLT&&bytes==sizeof(command));emits++;command=*(D9WGDDBlt*)p;return !fail_emit;}
${["format_compressed_block_bytes","format_is_compressed","format_is_depth","format_is_yuv","surface_depth_mask","surface_mirror_depth_fill","surface_fill_yuv","surface_can_mirror_color_fill","surface_prepare_color_fill","surface_mirror_color_fill","emit_blt"].map(extract).join("\n")}
static DDSurface make(DDrawObject*o,Memory*m,unsigned format,unsigned bytes){return(DDSurface){.owner=o,.memory=m,.shadow=m->bits,.format=format,.width=7,.height=3,.pitch=((7*bytes+7)&~7u)+8,.bytes_per_pixel=bytes,.handle=9,.desc={.ddsCaps={.dwCaps=DDSCAPS_SYSTEMMEMORY}}};}
static void pixels(void){const UINT formats[]={41,23,20,22,21},bytes[]={1,2,3,4,4};const DWORD fills[]={0,0xff,0x0000ff00,0xdeadbeef,0xffffffff};for(unsigned round=0;round<16;round++)for(unsigned f=0;f<5;f++)for(unsigned partial=0;partial<2;partial++)for(unsigned dirty=0;dirty<2;dirty++)for(unsigned color=0;color<5;color++){DDrawObject owner={77};Memory memory={.gpu_dirty=dirty};for(unsigned i=0;i<160;i++){memory.bits[i]=(BYTE)(0xa5^i^round);gpu[i]=(BYTE)(0x3b+i+round);}DDSurface s=make(&owner,&memory,formats[f],bytes[f]),alias=s;RECT area=partial?(RECT){1,1,6,2}:(RECT){0,0,7,3};BYTE expected[160];memcpy(expected,dirty&&partial?gpu:memory.bits,160);DWORD value=fills[color];for(LONG y=area.top;y<area.bottom;y++)for(LONG x=area.left;x<area.right;x++)for(UINT b=0;b<s.bytes_per_pixel;b++)expected[y*s.pitch+x*s.bytes_per_pixel+b]=(BYTE)(value>>(b*8));unsigned old_emits=emits,old_reads=reads;MUST(emit_blt(&s,partial?&area:NULL,NULL,NULL,D9WG_DDBLT_COLOR_FILL,value,0));MUST(memory.content_version==1);MUST(emits==old_emits+1&&reads==old_reads+(dirty&&partial)&&!memory.gpu_dirty&&!memcmp(memory.bits,expected,160));MUST(command.fill_color==value&&command.flags==D9WG_DDBLT_COLOR_FILL&&command.destination_handle==9&&command.device_handle==77);MUST(!alias.memory->gpu_dirty&&!memcmp(alias.shadow,expected,160));memory.gpu_dirty=TRUE;for(unsigned i=0;i<160;i++)gpu[i]^=0xff;MUST(surface_readback(&alias)&&!memcmp(alias.shadow,gpu,160));}}
static void failures(void){DDrawObject owner={77};Memory m={.gpu_dirty=TRUE};DDSurface s=make(&owner,&m,22,4);memset(m.bits,0x7a,160);memset(gpu,0x31,160);BYTE before[160];memcpy(before,m.bits,160);RECT part={1,1,6,2};surface_mirror_color_fill(&s,&part,0,TRUE);MUST(m.gpu_dirty&&!memcmp(m.bits,before,160));unsigned old_emits=emits,old_prepares=prepares;fail_read=TRUE;MUST(!emit_blt(&s,&part,NULL,NULL,D9WG_DDBLT_COLOR_FILL,0,0)&&emits==old_emits&&prepares==old_prepares&&m.gpu_dirty&&!memcmp(m.bits,before,160));fail_read=FALSE;fail_emit=TRUE;MUST(!emit_blt(&s,NULL,NULL,NULL,D9WG_DDBLT_COLOR_FILL,0,0)&&m.gpu_dirty&&!memcmp(m.bits,before,160));fail_emit=FALSE;fail_prepare=TRUE;MUST(!emit_blt(&s,NULL,NULL,NULL,D9WG_DDBLT_COLOR_FILL,0,0)&&m.gpu_dirty&&!memcmp(m.bits,before,160));fail_prepare=FALSE;RECT bad[]={{-1,0,7,3},{0,-1,7,3},{0,0,8,3},{0,0,7,4},{2,1,1,2},{1,2,2,1},{1,1,1,2}};for(unsigned i=0;i<7;i++){old_emits=emits;MUST(!emit_blt(&s,&bad[i],NULL,NULL,D9WG_DDBLT_COLOR_FILL,0,0)&&emits==old_emits&&m.gpu_dirty&&!memcmp(m.bits,before,160));}MUST(emit_blt(&s,NULL,NULL,NULL,D9WG_DDBLT_COLOR_FILL,0x0000ff00,0)&&!m.gpu_dirty);for(unsigned i=0;i<3;i++)for(unsigned x=0;x<7;x++){DWORD pixel;memcpy(&pixel,m.bits+i*s.pitch+x*4,4);MUST(pixel==0x0000ff00);}}
static void exclusions(void){DDrawObject owner={77};Memory m={0};DDSurface s=make(&owner,&m,22,4);s.desc.ddsCaps.dwCaps=0;MUST(!surface_can_mirror_color_fill(&s));s.desc.ddsCaps.dwCaps=DDSCAPS_SYSTEMMEMORY;const UINT formats[]={75,DDWG_FMT_YUY2,DDWG_FMT_DXT1};for(unsigned i=0;i<3;i++){s.format=formats[i];MUST(!surface_can_mirror_color_fill(&s));}const UINT compressed[]={DDWG_FMT_DXT1,DDWG_FMT_DXT2,DDWG_FMT_DXT3,DDWG_FMT_DXT4,DDWG_FMT_DXT5};for(unsigned f=0;f<5;f++)for(unsigned op=0;op<3;op++){s.format=compressed[f];DDSurface input=s;input.format=op==1?s.format:22;unsigned old_emits=emits,old_reads=reads,old_prepares=prepares;DWORD version=m.content_version;MUST(!emit_blt(&s,NULL,op==2?NULL:&input,NULL,op==2?D9WG_DDBLT_COLOR_FILL:0,0,0));MUST(emits==old_emits&&reads==old_reads&&prepares==old_prepares&&m.content_version==version);}s.format=22;for(unsigned bytes=0;bytes<7;bytes++){s.bytes_per_pixel=bytes;MUST(surface_can_mirror_color_fill(&s)==(bytes>=1&&bytes<=4));}s.bytes_per_pixel=4;const DWORD flags[]={D9WG_DDBLT_COLOR_FILL|D9WG_DDBLT_KEY_DESTINATION,D9WG_DDBLT_COLOR_FILL|D9WG_DDBLT_MIRROR_X,0};DDSurface source=s;for(unsigned i=0;i<3;i++){memset(m.bits,0x6c,160);BYTE before[160];memcpy(before,m.bits,160);m.gpu_dirty=FALSE;MUST(emit_blt(&s,NULL,i==2?&source:NULL,NULL,flags[i],0,0)&&m.gpu_dirty&&!memcmp(before,m.bits,160));}}
int main(void){pixels();failures();exclusions();printf("PASS actual packed color-fill C: %u checks, P8/16/24/XRGB/ARGB exact fill bytes, full/partial dirty readback rules, unchanged outside bytes and padding, alias observation, later GPU invalidation, failed readback/prepare/emission isolation and keyed/noncolor exclusions\\n",checks);return 0;}
`;
try{const file=dir+"/fill.c",binary=dir+"/fill";fs.writeFileSync(file,code);const built=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer",file,"-o",binary],{encoding:"utf8"});assert.equal(built.status,0,built.stderr);const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
