'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(process.env.DDRAW_CAPS_SOURCE||root+'/ddrawproxy/ddraw_proxy.c','utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-caps-profile-'));
const native=JSON.parse(fs.readFileSync(path.resolve(root,'../../v86/tests/glbridge/fixtures/ddraw_native_factory.json')));assert.equal(require('node:crypto').createHash('sha256').update(fs.readFileSync(root+'/../'+native.source)).digest('hex'),native.sourceSha256);
function extract(name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let end=source.indexOf('{',m.index)+1,d=1;for(;d;end++){if(source[end]==='{')d++;if(source[end]==='}')d--;}return source.slice(m.index,end);}
try {
 const sdk=dir+'/sdk.c';fs.writeFileSync(sdk,'#define DIRECTDRAW_VERSION 0x700\n#include <ddraw.h>\n#include <stddef.h>\n#undef WINAPI\n#define WINAPI\n#undef ZeroMemory\n#define ZeroMemory(p,n) memset(p,0,n)\n#undef CopyMemory\n#define CopyMemory(d,s,n) memcpy(d,s,n)\n#define DDWG_TRACE_ERROR(h) (h)\n#define DDWG_TRACE_RESULT(h) (h)\n#define DDWG_TRACE_CHECKPOINT(...)\n#define DDRAW_FACTORY_DEFAULT 0\n#define DDRAW_FACTORY_HARDWARE 1\n#define DDRAW_FACTORY_EMULATION 2\nstatic int caps_begin;\n'+['ddraw_factory_kind','fill_caps','copy_caps_to_caller','ddraw_GetCaps'].map(extract).join('\n')+'\nstatic int caps_end;\n');
 const pp=spawnSync(process.env.D9WG_CC||'i686-w64-mingw32-gcc',['-E','-P',sdk],{encoding:'utf8',maxBuffer:16<<20});assert.equal(pp.status,0,pp.stderr);
 const text=pp.stdout;function type(name){const m=new RegExp('}\\s*'+name+'[ ,;]').exec(text);assert(m,name);return text.slice(text.lastIndexOf('typedef struct',m.index),text.indexOf(';',m.index)+1);}
 const body=text.split('static int caps_begin;')[1].split('static int caps_end;')[0];
 const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
typedef uint32_t DWORD;typedef int32_t HRESULT,WINBOOL;typedef struct{DWORD value;}GUID;
${['DDSCAPS','DDSCAPS2','DDCAPS_DX7'].map(type).join('\n')}
typedef DDCAPS_DX7 DDCAPS,*LPDDCAPS;
#define DDRAW_FACTORY_DEFAULT 0
#define DDRAW_FACTORY_HARDWARE 1
#define DDRAW_FACTORY_EMULATION 2
#define FAILED(h) ((h)<0)
typedef struct DDSurface{struct DDSurface*next;int overlay_visible;}DDSurface;
typedef struct DDrawObject{unsigned factory_kind;DDSurface*surfaces;}DDrawObject,IDirectDraw7;
static unsigned checks;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"caps profile line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
static DDrawObject*object_from_iface(IDirectDraw7*p){return p;}
static DWORD ddraw_video_memory_free(void){return 48u<<20;}
${body}
int main(void){MUST(ddraw_factory_kind(NULL)==0&&ddraw_factory_kind((void*)1)==1&&ddraw_factory_kind((void*)2)==2);GUID unknown={0};MUST(ddraw_factory_kind(&unknown)==0);
 const DWORD hel=0x10u|0x40u|0x100u|0x200u|0x1000u|0x20000u|0x400000u;
 DDSurface a={.overlay_visible=1},b={.overlay_visible=1};a.next=&b;
 for(unsigned flavor=0;flavor<3;flavor++){DDrawObject owner={flavor,&a};DDCAPS h={.dwSize=sizeof(h)},s={.dwSize=sizeof(s)};MUST(!ddraw_GetCaps(&owner,&h,&s));MUST(h.dwSize==sizeof(h)&&s.dwSize==sizeof(s));MUST(h.ddsOldCaps.dwCaps==h.ddsCaps.dwCaps&&s.ddsOldCaps.dwCaps==s.ddsCaps.dwCaps);if(flavor==2)MUST(!h.ddsCaps.dwCaps&&h.dwCaps==0x02000000u&&!h.dwZBufferBitDepths);else MUST(h.ddsCaps.dwCaps&&h.dwCaps&&h.dwZBufferBitDepths&&h.dwCurrVisibleOverlays==2);if(flavor==1)MUST(!s.ddsCaps.dwCaps&&!s.dwCaps&&!s.dwZBufferBitDepths);else MUST(s.ddsCaps.dwCaps==hel&&s.dwCaps&&!(s.dwCaps&~${native.caps.find(r=>r.flavor===2).sflags}u)&&!s.dwCaps2&&!s.dwMaxVisibleOverlays&&!s.dwCurrVisibleOverlays&&s.dwZBufferBitDepths==0x400u&&!s.dwVidMemTotal&&!s.dwVidMemFree);
  for(unsigned profile=0;profile<4;profile++)for(unsigned size=0;size<sizeof(DDCAPS)+40;size++){union{DWORD align;unsigned char bytes[sizeof(DDCAPS)+128];}out,before;memset(&out,0xa7,sizeof(out));DDCAPS*p=(void*)(out.bytes+32);p->dwSize=size;before=out;HRESULT hr=copy_caps_to_caller(p,2,profile);MUST((size<4)==FAILED(hr));if(size<4)MUST(!memcmp(&out,&before,sizeof(out)));else {MUST(p->dwSize==size);for(unsigned n=0;n<32;n++)MUST(out.bytes[n]==0xa7);unsigned copied=size<sizeof(DDCAPS)?size:sizeof(DDCAPS);for(unsigned n=32+copied;n<sizeof(out.bytes);n++)MUST(out.bytes[n]==0xa7);}}
 }
 printf("PASS actual SDK DDraw caps profiles: %u checks; default/HAL-only/HEL-only inventories, native HEL surface/depth policy, retained size and guarded bounded prefix copies\\n",checks);return 0;}
`;
 fs.writeFileSync(dir+'/test.c',code);const b=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);process.stdout.write(r.stdout);
} finally{fs.rmSync(dir,{recursive:true,force:true});}
