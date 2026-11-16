"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), { execFileSync } = require("node:child_process");
const source = fs.readFileSync(process.env.V86GL_PROXY_SOURCE || path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const implementation = source.slice(source.indexOf("/* Three displayable RGBA8 formats."), source.indexOf("__declspec(dllexport)\nHGLRC APIENTRY wglCreateLayerContext"));
const code = `
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "gl_protocol.h"
#include "../tests/gl_api_fixture.h"
typedef int BOOL;typedef uint32_t DWORD,UINT;typedef float FLOAT;typedef void* HDC;typedef void* HWND;typedef void* HANDLE;
typedef struct {uint16_t nSize,nVersion;DWORD dwFlags;uint8_t iPixelType,cColorBits,cRedBits,cRedShift,cGreenBits,cGreenShift,cBlueBits,cBlueShift,cAlphaBits,cAlphaShift,cAccumBits,cAccumRedBits,cAccumGreenBits,cAccumBlueBits,cAccumAlphaBits,cDepthBits,cStencilBits,cAuxBuffers,iLayerType,bReserved;DWORD dwLayerMask,dwVisibleMask,dwDamageMask;} PIXELFORMATDESCRIPTOR;
#define APIENTRY
#define __declspec(x)
#define TRUE 1
#define FALSE 0
#define OBJ_DC 3
#define OBJ_MEMDC 10
#define ERROR_INVALID_HANDLE 6
#define ERROR_INVALID_PARAMETER 87
#define ERROR_INVALID_PIXEL_FORMAT 2000
#define PFD_DOUBLEBUFFER 1u
#define PFD_STEREO 2u
#define PFD_DRAW_TO_WINDOW 4u
#define PFD_DRAW_TO_BITMAP 8u
#define PFD_SUPPORT_GDI 16u
#define PFD_SUPPORT_OPENGL 32u
#define PFD_DOUBLEBUFFER_DONTCARE 0x40000000u
#define PFD_STEREO_DONTCARE 0x80000000u
#define PFD_TYPE_RGBA 0
#define PFD_MAIN_PLANE 0
#define ZeroMemory(p,n) memset(p,0,n)
static DWORD last_error;static int properties[4],property_writes,fail_property;
static void SetLastError(DWORD error){last_error=error;}
static DWORD GetObjectType(HDC dc){uintptr_t n=(uintptr_t)dc;return n==10?OBJ_MEMDC:n>=1&&n<=6?OBJ_DC:0;}
static HWND WindowFromDC(HDC dc){uintptr_t n=(uintptr_t)dc;return (HWND)(uintptr_t)(n==1||n==2?101:n==3?102:n==5?103:n==6?104:0);}
static BOOL IsWindow(HWND window){return (uintptr_t)window>=101&&(uintptr_t)window<=104;}
static HANDLE GetPropW(HWND window,const wchar_t* key){assert(!wcscmp(key,L"V86GL.PixelFormat"));return (HANDLE)(uintptr_t)properties[(uintptr_t)window-101];}
static BOOL SetPropW(HWND window,const wchar_t* key,HANDLE value){assert(!wcscmp(key,L"V86GL.PixelFormat"));if(fail_property){SetLastError(8);return FALSE;}properties[(uintptr_t)window-101]=(int)(uintptr_t)value;property_writes++;return TRUE;}
${implementation}
int main(void){
 HDC dc=(HDC)(uintptr_t)1;PIXELFORMATDESCRIPTOR pfd={0},out;assert(sizeof(pfd)==40);pfd.nSize=sizeof(pfd);pfd.nVersion=1;pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pfd.cColorBits=32;pfd.cDepthBits=64;
 assert(wglChoosePixelFormat(dc,&pfd)==1);pfd.dwFlags&=~PFD_DOUBLEBUFFER;assert(wglChoosePixelFormat(dc,&pfd)==3);pfd.dwFlags|=PFD_DOUBLEBUFFER_DONTCARE;assert(wglChoosePixelFormat(dc,&pfd)==1);
 pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_STEREO;assert(!wglChoosePixelFormat(dc,&pfd));pfd.dwFlags|=PFD_STEREO_DONTCARE;assert(wglChoosePixelFormat(dc,&pfd)==3);
 pfd.iPixelType=1;assert(!wglChoosePixelFormat(dc,&pfd));pfd.iPixelType=0;pfd.dwFlags=PFD_DRAW_TO_BITMAP;assert(!wglChoosePixelFormat(dc,&pfd));pfd.dwFlags=PFD_SUPPORT_GDI;assert(!wglChoosePixelFormat(dc,&pfd));
 pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL;assert(!valid_wgl_dc((HDC)(uintptr_t)10,FALSE,NULL,NULL)&&last_error==2000);
 assert(!wglChoosePixelFormat(NULL,&pfd)&&last_error==6);assert(!wglChoosePixelFormat((HDC)(uintptr_t)10,&pfd)&&last_error==2000);assert(!wglChoosePixelFormat((HDC)(uintptr_t)4,&pfd)&&last_error==2000);
 assert(!wglChoosePixelFormat(dc,NULL)&&last_error==87);pfd.nSize=0;assert(!wglChoosePixelFormat(dc,&pfd));pfd.nSize=sizeof(pfd);pfd.nVersion=2;assert(!wglChoosePixelFormat(dc,&pfd));pfd.nVersion=1;
 assert(wglDescribePixelFormat(dc,0,0,NULL)==3);memset(&out,0xA5,sizeof(out));assert(!wglDescribePixelFormat(dc,1,39,&out)&&out.nSize==0xA5A5);assert(!wglDescribePixelFormat(dc,4,40,&out)&&out.nSize==0xA5A5);
 assert(wglDescribePixelFormat(dc,1,40,&out)==3&&out.nSize==40&&out.cColorBits==24&&out.cAlphaBits==8&&out.cRedShift==0&&out.cGreenShift==8&&out.cBlueShift==16&&out.cAlphaShift==24);
 assert(!wglGetPixelFormat(dc));assert(!valid_wgl_dc(dc,TRUE,NULL,NULL)&&last_error==2000);assert(wglSetPixelFormat(dc,1,NULL));assert(property_writes==1&&wglGetPixelFormat((HDC)(uintptr_t)2)==1);assert(wglSetPixelFormat(dc,1,NULL)&&property_writes==1);assert(!wglSetPixelFormat(dc,3,NULL)&&wglGetPixelFormat(dc)==1);assert(!wglSetPixelFormat(dc,0,NULL));
 assert(wglSetPixelFormat((HDC)(uintptr_t)3,3,NULL));assert(window_format_flags((HWND)(uintptr_t)102)==V86GL_FORMAT_SINGLE_BUFFER);assert(wglSetPixelFormat((HDC)(uintptr_t)5,2,NULL));assert(window_format_flags((HWND)(uintptr_t)103)==(4u<<V86GL_FORMAT_SAMPLES_SHIFT));
 fail_property=1;assert(!wglSetPixelFormat((HDC)(uintptr_t)6,1,NULL)&&last_error==8&&!properties[3]);fail_property=0;
 int formats[5]={-99,-99,-99,-99,-99};UINT count=99;assert(wglChoosePixelFormatARB(dc,NULL,NULL,5,formats,&count)&&count==3&&formats[0]==1&&formats[1]==3&&formats[2]==2&&formats[3]==-99);
 int double_buffer[]={0x2011,1,0};assert(wglChoosePixelFormatARB(dc,double_buffer,NULL,5,formats,&count)&&count==2&&formats[0]==1&&formats[1]==2);int single[]={0x2011,0,0};assert(wglChoosePixelFormatARB(dc,single,NULL,5,formats,&count)&&count==1&&formats[0]==3);
 int samples[]={0x2041,1,0x2042,4,0};assert(wglChoosePixelFormatARB(dc,samples,NULL,5,formats,&count)&&count==1&&formats[0]==2);samples[1]=0;assert(wglChoosePixelFormatARB(dc,samples,NULL,5,formats,&count)&&!count);
 int min_depth[]={0x2022,16,0};assert(wglChoosePixelFormatARB(dc,min_depth,NULL,5,formats,&count)&&count==3);int high_depth[]={0x2022,32,0};assert(wglChoosePixelFormatARB(dc,high_depth,NULL,5,formats,&count)&&!count);int high_color[]={0x2014,25,0};assert(wglChoosePixelFormatARB(dc,high_color,NULL,5,formats,&count)&&!count);
 FLOAT fractional[]={0x2022,24.5f,0};assert(wglChoosePixelFormatARB(dc,NULL,fractional,5,formats,&count)&&!count);FLOAT float_single[]={0x2011,0,0};assert(wglChoosePixelFormatARB(dc,NULL,float_single,5,formats,&count)&&count==1&&formats[0]==3);
 assert(wglChoosePixelFormatARB(dc,NULL,NULL,0,NULL,&count)&&!count);assert(!wglChoosePixelFormatARB(dc,NULL,NULL,1,NULL,&count));
 int bad[]={0x202D,1,0};formats[0]=-99;assert(!wglChoosePixelFormatARB(dc,bad,NULL,5,formats,&count)&&!count&&formats[0]==-99&&last_error==87);FLOAT bad_float[]={0.5f,1,0};assert(!wglChoosePixelFormatARB(dc,NULL,bad_float,5,formats,&count)&&!count);
 int attrs[]={0x2000,0x2001,0x2002,0x2003,0x2007,0x2014,0x201B,0x2022,0x2023,0x2041,0x2042},values[11];FLOAT floats[11];
 assert(wglGetPixelFormatAttribivARB(dc,2,0,11,attrs,values));int expected[]={3,1,0,0x2027,0x202A,24,8,24,8,1,4};assert(!memcmp(values,expected,sizeof(values)));
 assert(wglGetPixelFormatAttribfvARB(dc,2,0,11,attrs,floats));for(int i=0;i<11;i++)assert(floats[i]==expected[i]);
 int number=0x2000,value=-99;assert(wglGetPixelFormatAttribivARB(dc,0,99,1,&number,&value)&&value==3);assert(wglGetPixelFormatAttribivARB(dc,-9,-9,1,&number,&value)&&value==3);
 int color=0x2014;value=-99;assert(!wglGetPixelFormatAttribivARB(dc,0,0,1,&color,&value)&&value==-99);assert(!wglGetPixelFormatAttribivARB(dc,1,1,1,&color,&value)&&value==-99);
 int invalid_attrs[]={0x2014,0x7FFF},sentinels[]={-99,-99};assert(!wglGetPixelFormatAttribivARB(dc,1,0,2,invalid_attrs,sentinels)&&sentinels[0]==-99&&sentinels[1]==-99);
 assert(!wglGetPixelFormatAttribivARB(NULL,1,0,1,&color,&value)&&value==-99);assert(wglGetPixelFormatAttribivARB(dc,0,0,0,NULL,NULL));return 0;
}`;
module.exports = { code };
if (require.main === module) {
const directory = fs.mkdtempSync(path.join(os.tmpdir(), "gl-wgl-formats-"));
try {
    const file = path.join(directory, "test.c"), binary = path.join(directory, "test");fs.writeFileSync(file, code);
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I", path.join(__dirname, "../openglproxy"), file, "-o", binary]);execFileSync(binary);
    console.log("PASS actual C WGL formats: window-scoped immutable format, memory/invalid DC refusal, RGB24+alpha8, single/MSAA, exact/minimum/float ARB matching, atomic queries and errors");
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
}
