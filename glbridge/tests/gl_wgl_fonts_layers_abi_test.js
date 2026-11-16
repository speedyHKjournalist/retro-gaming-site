"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), { execFileSync } = require("node:child_process"), { body, GL } = require("./gl_cpu_abi_harness.js"), { code: formats } = require("./gl_wgl_formats_abi_test.js");
const source = fs.readFileSync(process.env.V86GL_PROXY_SOURCE || path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const constants = ["COMPILE", "CLIENT_PIXEL_STORE_BIT", "PIXEL_UNPACK_BUFFER", "UNPACK_ALIGNMENT", "UNPACK_LSB_FIRST", "UNPACK_ROW_LENGTH", "UNPACK_SKIP_ROWS", "UNPACK_SKIP_PIXELS", "LIST_INDEX"].map(name => "#define GL_" + name + " " + (GL[name] ?? { LIST_INDEX: 0x0B33 }[name])).join("\n");
const code = formats.slice(0, formats.indexOf("int main(void){")) + `
#include <stdlib.h>
#define WINAPI
#define GL_FALSE 0
#define ERROR_DC_NOT_FOUND 1425
#define ERROR_BUSY 170
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_GEN_FAILURE 31
#define ERROR_INVALID_DATA 13
#define ERROR_CALL_NOT_IMPLEMENTED 120
#define V86GL_ATTRIB_STACK_DEPTH 16
#define GGO_BITMAP 1
#define GGI_MARK_NONEXISTING_GLYPHS 1
typedef uint16_t WORD,WCHAR;
#define GDI_ERROR 0xFFFFFFFFu
#define WGL_FONT_LINES 0
#define WGL_FONT_POLYGONS 1
#define LPD_SUPPORT_OPENGL 32
#define LPD_DOUBLEBUFFER 1
#define HEAP_ZERO_MEMORY 8
#define CopyMemory memcpy
#define GetProcessHeap() 0
${constants}
typedef uint32_t GLenum,GLuint,COLORREF;typedef int32_t GLint,GLsizei,LONG;typedef uint8_t GLubyte;typedef float GLfloat;typedef size_t SIZE_T;typedef void* LPVOID;
typedef struct {uint16_t fract;int16_t value;} FIXED;typedef struct {FIXED eM11,eM12,eM21,eM22;} MAT2;
typedef struct {DWORD gmBlackBoxX,gmBlackBoxY;struct {LONG x,y;} gmptGlyphOrigin;int16_t gmCellIncX,gmCellIncY;} GLYPHMETRICS,*LPGLYPHMETRICS;
typedef struct {float values[6];} GLYPHMETRICSFLOAT,*LPGLYPHMETRICSFLOAT;
typedef struct {uint16_t nSize,nVersion;DWORD dwFlags;uint8_t iPixelType,cColorBits,cRedBits,cRedShift,cGreenBits,cGreenShift,cBlueBits,cBlueShift,cAlphaBits,cAlphaShift,cAccumBits,cAccumRedBits,cAccumGreenBits,cAccumBlueBits,cAccumAlphaBits,cDepthBits,cStencilBits,cAuxBuffers,iLayerPlane,bReserved;COLORREF crTransparent;} LAYERPLANEDESCRIPTOR,*LPLAYERPLANEDESCRIPTOR;
static DWORD GetLastError(void){return last_error;}
static unsigned live_allocations,alloc_attempts,fail_alloc,ansi_calls,wide_calls,bitmaps,new_lists,end_lists;static UINT last_code;static int glyph_failure,second_failure,current=1,compiling,query_failure,g_client_attrib_stack_depth;
static void* HeapAlloc(int heap,int flags,SIZE_T size){(void)heap;(void)flags;if(++alloc_attempts==fail_alloc)return NULL;void* p=calloc(1,size);if(p)live_allocations++;return p;}
static void HeapFree(int heap,int flags,void* p){(void)heap;(void)flags;assert(p&&live_allocations);live_allocations--;free(p);}
static DWORD glyph(HDC dc,UINT code,UINT format,LPGLYPHMETRICS metrics,DWORD size,LPVOID bytes,const MAT2* matrix){
 assert(dc&&format==1&&matrix->eM11.value==1&&matrix->eM22.value==1);last_code=code;if(glyph_failure||(second_failure&&size)){SetLastError(123);return GDI_ERROR;}
 metrics->gmBlackBoxX=code==32?0:2;metrics->gmBlackBoxY=code==32?0:2;metrics->gmptGlyphOrigin.x=1;metrics->gmptGlyphOrigin.y=3;metrics->gmCellIncX=4;metrics->gmCellIncY=2;
 if(code==32)return 0;if(bytes){assert(size==8);uint8_t data[]={0x80,0,0,0,0x40,0,0,0};memcpy(bytes,data,8);}return 8;
}
static DWORD GetGlyphIndicesW(HDC dc,const WCHAR* text,int count,WORD* indices,DWORD flags){assert(dc&&count==1&&flags==1);*indices=*text==0xFFFF?0xFFFF:1;return 1;}
static DWORD GetGlyphIndicesA(HDC dc,const char* text,int count,WORD* indices,DWORD flags){assert(dc&&count==1&&flags==1&&text);*indices=1;return 1;}
static DWORD GetGlyphOutlineA(HDC dc,UINT code,UINT format,LPGLYPHMETRICS metrics,DWORD size,LPVOID bytes,const MAT2* matrix){ansi_calls++;return glyph(dc,code,format,metrics,size,bytes,matrix);}
static DWORD GetGlyphOutlineW(HDC dc,UINT code,UINT format,LPGLYPHMETRICS metrics,DWORD size,LPVOID bytes,const MAT2* matrix){wide_calls++;return glyph(dc,code,format,metrics,size,bytes,matrix);}
static void* v86gl_current_context(void){return current?(void*)(uintptr_t)1:NULL;}
static int emit_query_state(GLenum pname,unsigned kind,void* output){assert(pname==GL_LIST_INDEX&&kind==V86GL_STATE_INTEGER);if(query_failure)return -1;*(GLint*)output=compiling;return 1;}
typedef struct {GLuint pbo;int alignment,lsb,row,skip_rows,skip_pixels;} Unpack;
static Unpack unpack={77,1,1,15,2,3},saved;static uint8_t bitmap[8];static GLfloat origin[4];static GLsizei bitmap_width,bitmap_height;
static void glPushClientAttrib(GLuint mask){assert(mask==GL_CLIENT_PIXEL_STORE_BIT);saved=unpack;g_client_attrib_stack_depth++;}
static void glPopClientAttrib(void){assert(g_client_attrib_stack_depth>0);unpack=saved;g_client_attrib_stack_depth--;}
static void glBindBuffer(GLenum target,GLuint name){assert(target==GL_PIXEL_UNPACK_BUFFER);unpack.pbo=name;}
static void glPixelStorei(GLenum pname,GLint value){switch(pname){case GL_UNPACK_ALIGNMENT:unpack.alignment=value;break;case GL_UNPACK_LSB_FIRST:unpack.lsb=value;break;case GL_UNPACK_ROW_LENGTH:unpack.row=value;break;case GL_UNPACK_SKIP_ROWS:unpack.skip_rows=value;break;case GL_UNPACK_SKIP_PIXELS:unpack.skip_pixels=value;break;default:abort();}}
static void glNewList(GLuint list,GLenum mode){assert(list>=100&&mode==GL_COMPILE);new_lists++;}
static void glEndList(void){end_lists++;}
static void glBitmap(GLsizei width,GLsizei height,GLfloat xorig,GLfloat yorig,GLfloat xmove,GLfloat ymove,const GLubyte* bytes){assert(!unpack.pbo&&unpack.alignment==4&&!unpack.lsb&&!unpack.row&&!unpack.skip_rows&&!unpack.skip_pixels);bitmaps++;bitmap_width=width;bitmap_height=height;origin[0]=xorig;origin[1]=yorig;origin[2]=xmove;origin[3]=ymove;if(bytes)memcpy(bitmap,bytes,8);}
${["wglDescribeLayerPlane", "wglSetLayerPaletteEntries", "wglGetLayerPaletteEntries", "wglRealizeLayerPalette"].map(name => body(source,name,(name.includes("Entries")?"int":"BOOL")+" APIENTRY ")).join("\n")}
${body(source,"define_gdi_bitmap_glyph","static BOOL ")}
${body(source,"use_font_bitmaps_common","static BOOL ")}
${["wglUseFontBitmapsA", "wglUseFontBitmapsW"].map(name => body(source,name,"BOOL APIENTRY ")).join("\n")}
${body(source,"use_font_outlines_common","static BOOL ")}
${["wglUseFontOutlinesA", "wglUseFontOutlinesW"].map(name => body(source,name,"BOOL APIENTRY ")).join("\n")}
int main(void){
 HDC dc=(HDC)(uintptr_t)1;assert(wglSetPixelFormat(dc,1,NULL));assert(!window_format_flags((HWND)(uintptr_t)101));LAYERPLANEDESCRIPTOR layer;memset(&layer,0xA5,sizeof(layer));assert(!wglDescribeLayerPlane(dc,1,1,sizeof(layer),&layer)&&layer.nSize==0xA5A5);assert(wglDescribeLayerPlane(dc,1,0,sizeof(layer),&layer)&&layer.cColorBits==24&&layer.cAlphaBits==8&&layer.cStencilBits==8&&(layer.dwFlags&1));assert(wglDescribeLayerPlane(dc,3,0,sizeof(layer),&layer)&&!(layer.dwFlags&1));assert(!wglDescribeLayerPlane(dc,1,0,sizeof(layer)-1,&layer));
 COLORREF palette[]={0xA5A5A5A5};assert(!wglSetLayerPaletteEntries(dc,0,0,1,palette)&&last_error==2000);assert(!wglGetLayerPaletteEntries(dc,0,0,1,palette)&&palette[0]==0xA5A5A5A5&&last_error==2000);assert(!wglRealizeLayerPalette(dc,0,TRUE)&&last_error==2000);
 Unpack before=unpack;assert(wglUseFontBitmapsA(dc,65,1,100)&&ansi_calls==2&&!wide_calls&&last_code==65);assert(bitmaps==1&&bitmap_width==2&&bitmap_height==2&&bitmap[0]==0x40&&bitmap[4]==0x80&&origin[0]==-1&&origin[1]==-1&&origin[2]==4&&origin[3]==2);assert(!memcmp(&unpack,&before,sizeof(unpack))&&!g_client_attrib_stack_depth&&!live_allocations);
 assert(wglUseFontBitmapsW((HDC)(uintptr_t)10,0x4E2D,1,101)&&wide_calls==2&&ansi_calls==2&&last_code==0x4E2D);assert(wglUseFontBitmapsW(dc,32,1,102)&&bitmap_width==0&&bitmap_height==0&&origin[2]==4&&origin[3]==2);
 unsigned bitmaps_before=bitmaps;assert(wglUseFontBitmapsW(dc,0xFFFF,1,103)&&bitmaps==bitmaps_before);assert(!wglUseFontBitmapsW(dc,0x10000,1,103)&&last_error==87);
 unsigned lists_before=new_lists;glyph_failure=1;assert(!wglUseFontBitmapsW(dc,65,1,103)&&last_error==123&&new_lists==lists_before);glyph_failure=0;second_failure=1;assert(!wglUseFontBitmapsW(dc,65,1,103)&&last_error==123&&!live_allocations);second_failure=0;
 for(unsigned i=1;i<=2;i++){fail_alloc=alloc_attempts+i;assert(!wglUseFontBitmapsW(dc,65,1,103)&&last_error==8&&!live_allocations);fail_alloc=0;}
 assert(!memcmp(&unpack,&before,sizeof(unpack))&&new_lists==lists_before&&new_lists==end_lists);
 current=0;assert(!wglUseFontBitmapsW(dc,65,1,103)&&last_error==1425);current=1;assert(!wglUseFontBitmapsW(NULL,65,1,103)&&last_error==6);query_failure=1;assert(!wglUseFontBitmapsW(dc,65,1,103)&&last_error==170);query_failure=0;compiling=1;assert(!wglUseFontBitmapsW(dc,65,1,103)&&last_error==170);compiling=0;g_client_attrib_stack_depth=16;assert(!wglUseFontBitmapsW(dc,65,1,103)&&last_error==8);g_client_attrib_stack_depth=0;
 assert(!wglUseFontBitmapsW(dc,UINT32_MAX,2,103)&&last_error==87);assert(!wglUseFontBitmapsW(dc,65,1,0)&&last_error==87);
 GLYPHMETRICSFLOAT metrics;memset(&metrics,0xA5,sizeof(metrics));assert(!wglUseFontOutlinesA(dc,65,1,103,0,0,0,&metrics)&&last_error==120);assert(!wglUseFontOutlinesW(dc,65,1,103,0,0,1,&metrics)&&last_error==120);assert(((uint8_t*)&metrics)[0]==0xA5&&new_lists==lists_before);assert(!wglUseFontOutlinesW(dc,65,1,103,0,0,2,&metrics)&&last_error==87);return 0;
}`;
const directory = fs.mkdtempSync(path.join(os.tmpdir(), "gl-wgl-fonts-"));
try {
    const file = path.join(directory, "test.c"), binary = path.join(directory, "test");fs.writeFileSync(file, code);
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I", path.join(__dirname, "../openglproxy"), file, "-o", binary]);execFileSync(binary);
    console.log("PASS actual C WGL fonts/layers: Unicode and ANSI glyphs, flipped rows/origins/advances, blank glyph, restored unpack/PBO state, failure cleanup and no fake outline/palette success");
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
