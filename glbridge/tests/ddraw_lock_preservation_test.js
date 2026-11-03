"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = fs.readFileSync(process.env.DDRAW_LOCK_SOURCE || root + "/ddrawproxy/ddraw_proxy.c", "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "ddraw-lock-"));
function extract(name) {const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;} return source.slice(m.index, end);}
const code = `#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t DWORD,UINT;typedef int32_t LONG,HRESULT;typedef int BOOL;typedef unsigned char BYTE;typedef void *HANDLE;
typedef struct {LONG left,top,right,bottom;} RECT;typedef RECT *LPRECT;
typedef struct {DWORD dwSize,dwFlags,dwWidth,dwHeight;LONG lPitch;void *lpSurface;} DDSURFACEDESC2,*LPDDSURFACEDESC2;
typedef struct {BOOL gpu_dirty;BYTE bits[64];} Memory;
typedef struct DDSurface {Memory *memory;BYTE *shadow;void *dc,*owner,*flip_next;UINT width,height,pitch,format,bytes_per_pixel,handle;DWORD lock_depth,lock_flags;RECT locked_rect;BOOL primary;DDSURFACEDESC2 desc;} DDSurface;
typedef DDSurface IDirectDrawSurface7;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define DD_OK 0
#define DDERR_INVALIDPARAMS (-1)
#define DDERR_GENERIC (-2)
#define DDERR_SURFACEBUSY (-3)
#define DDERR_INVALIDRECT (-4)
#define DDERR_NOTLOCKED (-5)
#define DDLOCK_READONLY 0x10u
#define DDLOCK_WRITEONLY 0x20u
#define DDLOCK_WAIT 1u
#define DDSD_LPSURFACE 0x800u
#define DDSD_PITCH 8u
#define DDWG_TRACE_ERROR(x) (x)
#define DDWG_TRACE_RESULT(x) (x)
#define DDWG_TRACE_CHECKPOINT(...)
#define DDWG_TRACE(...)
static unsigned checks,reads,uploads,presents,fail_read;static BYTE gpu[64];static RECT uploaded;
static DDSurface *surface_from_iface(IDirectDrawSurface7*p){return p;}
static BOOL format_is_compressed(UINT f){return f==0x31545844u;}
static UINT format_compressed_block_bytes(UINT f){(void)f;return 8;}
static BOOL surface_readback(DDSurface*s){reads++;if(fail_read){fail_read=0;return FALSE;}memcpy(s->shadow,gpu,64);s->memory->gpu_dirty=FALSE;return TRUE;}
static void emit_surface_upload(DDSurface*s,RECT*r){uploads++;uploaded=*r;for(LONG y=r->top;y<r->bottom;y++)for(LONG x=r->left;x<r->right;x++)memcpy(gpu+y*s->pitch+x*4,s->shadow+y*s->pitch+x*4,4);}
static BOOL emit_blt_to_screen(DDSurface*s,const RECT*r){(void)s;(void)r;presents++;return TRUE;}
static BOOL emit_present_and_flush(void*p){(void)p;presents++;return TRUE;}
${source.includes("static BOOL present_changed_primary(") ? extract("present_changed_primary") : ""}
${extract("surface_Lock")}
${extract("surface_Unlock")}
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"DDraw lock line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
static DDSurface make(Memory*m){DDSurface s={0};s.memory=m;s.shadow=m->bits;s.width=4;s.height=4;s.pitch=16;s.bytes_per_pixel=4;s.handle=9;s.desc=(DDSURFACEDESC2){.dwSize=sizeof(DDSURFACEDESC2),.dwWidth=4,.dwHeight=4};return s;}
static int cycles(void){
 const DWORD flags[]={0,DDLOCK_READONLY,DDLOCK_WRITEONLY};
 for(unsigned round=0;round<128;round++)for(unsigned mode=0;mode<3;mode++)for(unsigned part=0;part<2;part++){
  Memory m={.gpu_dirty=TRUE};DDSurface s=make(&m),alias=s;DWORD before[16];for(unsigned i=0;i<16;i++)before[i]=0xff204060u+round*256+i;memcpy(gpu,before,64);RECT r={1,1,3,3};DDSURFACEDESC2 d={.dwSize=sizeof(d)};unsigned old_reads=reads,old_uploads=uploads;
  CHECK(surface_Lock(&s,part?&r:NULL,&d,flags[mode]|DDLOCK_WAIT,NULL)==0);CHECK(reads==old_reads+1&&!m.gpu_dirty&&s.lock_depth==1);CHECK(d.lpSurface==m.bits+(part?20:0)&&d.lPitch==16&&d.dwWidth==4&&d.dwHeight==4);CHECK(!memcmp(m.bits,before,64));
  if(mode!=1)*(DWORD*)d.lpSurface=0xffe01020u+round;
  CHECK(surface_Unlock(&s,NULL)==0&&s.lock_depth==0);CHECK(uploads==old_uploads+(mode!=1));
  if(mode!=1){CHECK(uploaded.left==(part?1:0)&&uploaded.top==(part?1:0)&&uploaded.right==(part?3:4)&&uploaded.bottom==(part?3:4));before[part?5:0]=0xffe01020u+round;}
  CHECK(!memcmp(gpu,before,64));d=(DDSURFACEDESC2){.dwSize=sizeof(d)};CHECK(!surface_Lock(&alias,NULL,&d,DDLOCK_READONLY,NULL)&&!surface_Unlock(&alias,NULL)&&reads==old_reads+1);CHECK(!presents);
 }
 return 0;
}
static int failures(void){
 Memory m={.gpu_dirty=TRUE};DDSurface s=make(&m);memset(m.bits,0x5a,64);s.lock_flags=0x100;s.locked_rect=(RECT){2,2,4,4};DDSURFACEDESC2 d={.dwSize=sizeof(d)},before=d;unsigned old_reads=reads;RECT bad[]={ {-1,0,4,4},{0,-1,4,4},{0,0,5,4},{0,0,4,5},{1,0,1,4},{0,2,4,2},{2,0,1,4}};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){CHECK(surface_Lock(&s,&bad[i],&d,DDLOCK_WRITEONLY,NULL)==DDERR_INVALIDRECT);CHECK(reads==old_reads&&!s.lock_depth&&s.lock_flags==0x100&&!memcmp(&d,&before,sizeof(d)));}
 CHECK(surface_Lock(&s,NULL,NULL,0,NULL)==DDERR_INVALIDPARAMS);d.dwSize=0;CHECK(surface_Lock(&s,NULL,&d,0,NULL)==DDERR_INVALIDPARAMS);d=before;s.dc=(void*)1;CHECK(surface_Lock(&s,NULL,&d,0,NULL)==DDERR_SURFACEBUSY);s.dc=NULL;BYTE*shadow=s.shadow;s.shadow=NULL;CHECK(surface_Lock(&s,NULL,&d,0,NULL)==DDERR_GENERIC);s.shadow=shadow;CHECK(reads==old_reads);
 for(unsigned mode=0;mode<3;mode++){fail_read=1;CHECK(surface_Lock(&s,NULL,&d,(DWORD[]){0,DDLOCK_READONLY,DDLOCK_WRITEONLY}[mode],NULL)==DDERR_GENERIC);CHECK(!s.lock_depth&&s.lock_flags==0x100&&s.locked_rect.left==2&&m.gpu_dirty&&!memcmp(&d,&before,sizeof(d)));for(unsigned i=0;i<64;i++)CHECK(m.bits[i]==0x5a);}
 CHECK(surface_Unlock(&s,NULL)==DDERR_NOTLOCKED);fail_read=0;CHECK(!surface_Lock(&s,NULL,&d,DDLOCK_WRITEONLY,NULL)&&!surface_Unlock(&s,NULL));
 s=make(&m);s.format=0x31545844u;s.width=s.height=8;s.pitch=16;m.gpu_dirty=TRUE;RECT block_bad={3,0,8,8};old_reads=reads;d=before;CHECK(surface_Lock(&s,&block_bad,&d,DDLOCK_WRITEONLY,NULL)==DDERR_INVALIDRECT&&reads==old_reads);RECT block={4,4,8,8};CHECK(!surface_Lock(&s,&block,&d,DDLOCK_READONLY,NULL)&&d.lpSurface==m.bits+24&&reads==old_reads+1);CHECK(!surface_Unlock(&s,NULL));return 0;
}
int main(void){CHECK(!cycles());CHECK(!failures());printf("PASS actual DDraw Lock/Unlock: %u checks, 768 full/partial GPU-dirty locks, all write modes, shared aliases, untouched pixels, compressed offsets, invalid-call isolation and readback failure/retry\\n",checks);return 0;}
`;
try {const file = dir + "/lock.c", binary = dir + "/lock"; fs.writeFileSync(file, code); const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", file, "-o", binary], {encoding: "utf8"}); assert.equal(built.status, 0, built.stderr); const run = spawnSync(binary, [], {encoding: "utf8"}); assert.equal(run.status, 0, run.stderr); process.stdout.write(run.stdout);} finally {fs.rmSync(dir, {recursive: true, force: true});}
