'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-getdc-'));
function extract(n){const m=new RegExp('^static[^;{}]*\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(source);assert(m,n);let b=source.indexOf('{',m.index)+1,d=1;for(;d;b++){if(source[b]==='{')d++;if(source[b]==='}')d--;}return source.slice(m.index,b);}
try {
const code=`#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint32_t UINT,DWORD,D3DFORMAT;typedef int32_t LONG,BOOL,HRESULT,INT;typedef uint16_t WORD;typedef unsigned char BYTE;typedef void *HDC,*HBITMAP;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define E_OUTOFMEMORY (-2)
#define FAILED(x) ((x)<0)
#define TRACE_MARK_ENTER(...)
#define TRACE_MARK_EXIT(...)
#define TRACE(...)
#define HOSTLOG_REFUSED(...)
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory(d,s,n) memcpy(d,s,n)
#define D3DFMT_R8G8B8 20
#define D3DFMT_A8R8G8B8 21
#define D3DFMT_X8R8G8B8 22
#define D3DFMT_R5G6B5 23
#define D3DFMT_X1R5G5B5 24
#define D3DFMT_A1R5G5B5 25
#define BI_RGB 0
#define BI_BITFIELDS 3
#define DIB_RGB_COLORS 0
struct header{DWORD biSize;LONG biWidth,biHeight;WORD biPlanes,biBitCount;DWORD biCompression,biSizeImage;LONG biXPelsPerMeter,biYPelsPerMeter;DWORD biClrUsed,biClrImportant;};
typedef struct header BITMAPINFOHEADER;typedef struct{BITMAPINFOHEADER header;DWORD masks[3];}BITMAPINFO;
typedef struct{BOOL locked,dc_active;BYTE bytes[512];}D9TextureLevel;
typedef struct{UINT level_count;D9TextureLevel*levels;}Texture;
typedef struct Surface{Texture *texture,*cube_texture;UINT level,cube_face,width,height,format;HDC dc;HBITMAP dib,dc_previous;void *dib_bits,*dc_bits;UINT dib_pitch,dc_pitch,dc_pixel_bytes;D9TextureLevel plain;}D9Surface,IDirect3DSurface9;
typedef struct{INT Pitch;void*pBits;}D3DLOCKED_RECT;
static D9Surface*surface_from_iface(IDirect3DSurface9*s){return s;}
static unsigned bitmaps,dcs,uploads,flushed,fail_step;static BITMAPINFO captured;
static HBITMAP CreateDIBSection(HDC d,const BITMAPINFO*i,UINT u,void**bits,void*h,DWORD off){(void)d;(void)u;(void)h;(void)off;captured=*i;if(fail_step==1)return NULL;*bits=calloc(1,((i->header.biWidth*i->header.biBitCount/8+3)&~3)*abs(i->header.biHeight));assert(*bits);bitmaps++;return *bits;}
static HDC CreateCompatibleDC(HDC d){(void)d;if(fail_step==2)return NULL;dcs++;return malloc(1);}
static HBITMAP SelectObject(HDC d,HBITMAP b){assert(d&&b);return (void*)(uintptr_t)1;}
static void DeleteObject(HBITMAP b){assert(bitmaps);bitmaps--;free(b);}
static void DeleteDC(HDC d){assert(dcs);dcs--;free(d);}
static void GdiFlush(void){flushed++;}
static D9TextureLevel*level_for(D9Surface*s){if(s->texture)return &s->texture->levels[s->level];if(s->cube_texture)return &s->cube_texture->levels[s->cube_face*s->cube_texture->level_count+s->level];return &s->plain;}
${['texture_levels_mapped','surface_dc_format_bytes'].map(extract).join('\n')}
static HRESULT surface_lock_rect(IDirect3DSurface9*s,D3DLOCKED_RECT*out,const void*r,DWORD flags){(void)r;(void)flags;D9TextureLevel*l=level_for(s);if(l->locked)return -1;UINT bytes;WORD bits;assert(surface_dc_format_bytes(s->format,&bytes,&bits));out->Pitch=s->width*bytes+8;out->pBits=l->bytes;l->locked=1;return 0;}
static HRESULT surface_unlock_rect(IDirect3DSurface9*s){D9TextureLevel*l=level_for(s);assert(l->locked&&!l->dc_active);l->locked=0;uploads++;return 0;}
${['surface_get_dc','surface_release_dc'].map(extract).join('\n')}
int main(void){unsigned cases=0;
for(UINT kind=0;kind<3;kind++)for(UINT format=20;format<27;format++)for(UINT failure=0;failure<3;failure++){
 D9TextureLevel levels[18]={0};Texture t={3,levels};D9Surface s={.width=5,.height=3,.format=format,.cube_face=4,.level=2};if(kind==1)s.texture=&t;if(kind==2)s.cube_texture=&t;D9TextureLevel*l=level_for(&s);memset(l->bytes,0x65,sizeof(l->bytes));HDC dc=(void*)(uintptr_t)0x1234;fail_step=failure;unsigned before=uploads;HRESULT hr=surface_get_dc(&s,&dc);
 if(format==26||failure){assert(hr<0&&dc==(void*)(uintptr_t)0x1234&&!bitmaps&&!dcs&&!l->locked&&!l->dc_active);}
 else {
  assert(!hr&&dc==s.dc&&bitmaps==1&&dcs==1&&l->locked&&(!kind||l->dc_active));UINT b=format==20?3:format<23?4:2;assert(captured.header.biBitCount==b*8&&captured.header.biCompression==(b==2?3:0));if(b==2){assert(captured.masks[0]==(format==23?0xf800:0x7c00));assert(captured.masks[1]==(format==23?0x7e0:0x3e0));assert(captured.masks[2]==0x1f);}
  HDC sentinel=(void*)(uintptr_t)0x1234;assert(surface_get_dc(&s,&sentinel)==-1&&sentinel==(void*)(uintptr_t)0x1234);assert(surface_release_dc(&s,sentinel)==-1&&l->locked);
  for(UINT y=0;y<3;y++){for(UINT x=0;x<5*b;x++)assert(((BYTE*)s.dib_bits)[y*s.dib_pitch+x]==0x65);memset((BYTE*)s.dib_bits+y*s.dib_pitch,0x10+y,5*b);}
  if(kind){D9Surface other=s;other.level=0;other.dc=NULL;assert(surface_get_dc(&other,&sentinel)==-1&&sentinel==(void*)(uintptr_t)0x1234);assert(texture_levels_mapped(levels,kind==2?18:3,FALSE));}
  assert(!surface_release_dc(&s,dc)&&!bitmaps&&!dcs&&!l->locked&&!l->dc_active&&uploads==before+1&&flushed);assert(surface_release_dc(&s,dc)==-1);
  for(UINT y=0;y<3;y++){for(UINT x=0;x<5*b;x++)assert(l->bytes[y*(5*b+8)+x]==0x10+y);for(UINT x=5*b;x<5*b+8;x++)assert(l->bytes[y*(5*b+8)+x]==0x65);}
 }
 cases++;
}
for(UINT kind=1;kind<=2;kind++){D9TextureLevel levels[18]={0};Texture t={3,levels};D9Surface s={.width=5,.height=3,.format=21,.level=2};if(kind==1)s.texture=&t;else s.cube_texture=&t;levels[0].locked=1;HDC out=(void*)(uintptr_t)99;assert(surface_get_dc(&s,&out)==-1&&out==(void*)(uintptr_t)99&&!level_for(&s)->locked&&!bitmaps&&!dcs);cases++;}
printf("PASS actual GetDC/ReleaseDC: %u cases; six formats, pixels/padding, parent exclusion, sentinel preservation, GDI failure cleanup and release ownership\\n",cases);return 0;}
`;
fs.writeFileSync(dir+'/test.c',code);let r=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);r=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(r.status,0,r.stdout+r.stderr);process.stdout.write(r.stdout);
} finally{fs.rmSync(dir,{recursive:true,force:true});}
