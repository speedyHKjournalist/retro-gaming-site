"use strict";
const fs=require("node:fs"),os=require("node:os"),path=require("node:path"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.DDRAW_PRIVATE_SOURCE||root+"/ddrawproxy/ddraw_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-private-"));
function extract(name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end).replace(/\bWINAPI\b/g,"");}
const head=String.raw`
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t DWORD,ULONG,UINT;typedef int32_t LONG,HRESULT;typedef int BOOL;typedef unsigned char BYTE;typedef size_t SIZE_T;typedef void*LPVOID;typedef DWORD*LPDWORD;
typedef struct {DWORD Data1;uint16_t Data2,Data3;BYTE Data4[8];}GUID;typedef GUID IID;typedef const GUID*REFGUID;
typedef struct Counter Counter;struct Counter{LONG refs;};typedef Counter IUnknown;
static unsigned checks,allocations,fail_alloc,callbacks;static void(*on_release)(void);
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"private data line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static void*heap_alloc_zero(size_t bytes){if(fail_alloc||bytes>1048576)return NULL;void*p=calloc(1,bytes);MUST(p);allocations++;return p;}
static void heap_free(void*p){if(p){MUST(allocations);allocations--;free(p);}}
static LONG InterlockedIncrement(LONG*p){return ++*p;}
static ULONG IUnknown_AddRef(IUnknown*p){MUST(p&&p->refs>0);return ++p->refs;}
static ULONG IUnknown_Release(IUnknown*p){MUST(p&&p->refs>0);ULONG n=--p->refs;if(on_release){void(*callback)(void)=on_release;on_release=NULL;callbacks++;callback();}return n;}
typedef struct{DWORD content_version;}DDSurfaceMemory;
`;
const shape=source.match(/typedef struct DDSurfacePrivateData \{[\s\S]*?\} DDSurfacePrivateData;/)[0];
const object=String.raw`
typedef struct{DDSurfaceMemory*memory;DDSurfacePrivateData*private_data;}DDSurface;
typedef DDSurface IDirectDrawSurface7,IDirectDrawSurface4;
static DDSurface*surface_from_iface(void*p){return p;}
`;
const functions=['guid_equal','surface_memory_changed','surface_private_data_link','surface_private_data_release','surface_private_data_clear','surface_SetPrivateData','surface_GetPrivateData','surface_FreePrivateData','surface_GetUniquenessValue','surface_ChangeUniquenessValue','surface4_SetPrivateData','surface4_GetPrivateData','surface4_FreePrivateData','surface4_GetUniquenessValue','surface4_ChangeUniquenessValue'].map(extract).join('\n');
const tests=String.raw`
static const GUID one={1,2,3,{4,5,6,7,8,9,10,11}},two={1,2,3,{4,5,6,7,8,9,10,12}};
static unsigned api;
static HRESULT set(DDSurface*s,REFGUID t,void*d,DWORD bytes,DWORD flags){return api==4?surface4_SetPrivateData(s,t,d,bytes,flags):surface_SetPrivateData(s,t,d,bytes,flags);}
static HRESULT get(DDSurface*s,REFGUID t,void*d,DWORD*bytes){return api==4?surface4_GetPrivateData(s,t,d,bytes):surface_GetPrivateData(s,t,d,bytes);}
static HRESULT clear(DDSurface*s,REFGUID t){return api==4?surface4_FreePrivateData(s,t):surface_FreePrivateData(s,t);}
static void byte_data(void){DDSurfaceMemory m={1};DDSurface s={&m,NULL},other={&m,NULL};BYTE data[64],copy[64];for(unsigned n=1;n<=64;n++){for(unsigned i=0;i<64;i++)data[i]=copy[i]=(BYTE)(i+n);MUST(!set(&s,&one,data,n,0));memset(data,0xab,sizeof(data));struct{BYTE before[8],out[64],after[8];}out;memset(&out,0x6d,sizeof(out));DWORD bytes=n-1;MUST(get(&s,&one,out.out,&bytes)==DDERR_MOREDATA&&bytes==n&&out.out[0]==0x6d);bytes=64;MUST(!get(&s,&one,out.out,&bytes)&&bytes==n&&!memcmp(out.out,copy,n));for(unsigned i=n;i<64;i++)MUST(out.out[i]==0x6d);for(unsigned i=0;i<8;i++)MUST(out.before[i]==0x6d&&out.after[i]==0x6d);bytes=n-1;MUST(get(&s,&one,NULL,&bytes)==DDERR_MOREDATA&&bytes==n);bytes=64;MUST(get(&s,&one,NULL,&bytes)==DDERR_INVALIDPARAMS&&bytes==64);MUST(get(&s,&one,NULL,NULL)==DDERR_INVALIDPARAMS);bytes=0xdeadbeef;MUST(get(&s,&two,out.out,&bytes)==DDERR_NOTFOUND&&bytes==0xdeadbeef);MUST(get(&other,&one,NULL,NULL)==DDERR_NOTFOUND);}
    MUST(set(&s,&one,NULL,1,0)==DDERR_INVALIDPARAMS&&set(&s,&one,data,0,0)==DDERR_INVALIDPARAMS&&set(&s,&one,data,1,0x80000000)==DDERR_INVALIDPARAMS);fail_alloc=1;MUST(set(&s,&one,data,1,0)==DDERR_OUTOFMEMORY);fail_alloc=0;DWORD bytes=64;MUST(!get(&s,&one,data,&bytes)&&bytes==64&&!memcmp(data,copy,64));MUST(!clear(&s,&one)&&clear(&s,&one)==DDERR_NOTFOUND&&!allocations);
}
static DDSurface*reentrant_surface;static IUnknown*reentrant_expected;static int expect_missing;
static void reenter(void){IUnknown*p=NULL;DWORD size=sizeof(p);HRESULT hr=get(reentrant_surface,&one,&p,&size);if(expect_missing)MUST(hr==DDERR_NOTFOUND);else MUST(!hr&&p==reentrant_expected&&size==sizeof(p));}
static void objects(void){DDSurfaceMemory m={1};DDSurface s={&m,NULL};Counter a={1},b={1};IUnknown*p=NULL;DWORD bytes=sizeof(p);MUST(!set(&s,&one,&a,sizeof(p),DDSPD_IUNKNOWNPOINTER)&&a.refs==2);MUST(!get(&s,&one,&p,&bytes)&&p==&a&&a.refs==2);MUST(!set(&s,&one,&a,sizeof(p),DDSPD_IUNKNOWNPOINTER)&&a.refs==2);MUST(set(&s,&one,&b,0,DDSPD_IUNKNOWNPOINTER)==DDERR_INVALIDPARAMS&&a.refs==2&&b.refs==1);fail_alloc=1;MUST(set(&s,&one,&b,sizeof(p),DDSPD_IUNKNOWNPOINTER)==DDERR_OUTOFMEMORY&&a.refs==2&&b.refs==1);fail_alloc=0;
    reentrant_surface=&s;reentrant_expected=&b;expect_missing=0;on_release=reenter;MUST(!set(&s,&one,&b,sizeof(p),DDSPD_IUNKNOWNPOINTER)&&a.refs==1&&b.refs==2&&!on_release);bytes=sizeof(p);MUST(!get(&s,&one,&p,&bytes)&&p==&b&&b.refs==2);expect_missing=1;on_release=reenter;MUST(!clear(&s,&one)&&b.refs==1&&!on_release);
    MUST(!set(&s,&one,&a,sizeof(p),DDSPD_IUNKNOWNPOINTER));MUST(set(&s,&one,&b,sizeof(p)*2,DDSPD_IUNKNOWNPOINTER)==DDERR_INVALIDPARAMS&&a.refs==1&&b.refs==1&&get(&s,&one,NULL,NULL)==DDERR_NOTFOUND);
    MUST(!set(&s,&one,&a,sizeof(p),DDSPD_IUNKNOWNPOINTER)&&!set(&s,&two,&b,sizeof(p),DDSPD_IUNKNOWNPOINTER)&&a.refs==2&&b.refs==2);on_release=reenter;surface_private_data_clear(&s);MUST(a.refs==1&&b.refs==1&&!s.private_data&&!allocations&&!on_release);
}
static void volatile_data(void){DDSurfaceMemory m={1},replacement={1};DDSurface s={&m,NULL},alias={&m,NULL};DWORD data=42,value=0,bytes=4,unique=0;MUST(!set(&s,&one,&data,4,DDSPD_VOLATILE)&&!set(&alias,&one,&data,4,DDSPD_VOLATILE));MUST(!get(&s,&one,&value,&bytes)&&value==42);if(api==4){MUST(!surface4_GetUniquenessValue(&s,&unique)&&unique==1);MUST(!surface4_ChangeUniquenessValue(&alias));}else{MUST(!surface_GetUniquenessValue(&s,&unique)&&unique==1);MUST(!surface_ChangeUniquenessValue(&alias));}MUST(m.content_version==2);value=0xdeadbeef;bytes=4;MUST(get(&s,&one,&value,&bytes)==DDERR_EXPIRED&&!bytes&&value==0xdeadbeef);bytes=4;MUST(get(&alias,&one,&value,&bytes)==DDERR_EXPIRED&&!bytes);MUST(!clear(&s,&one)&&!clear(&alias,&one));
    Counter a={1};MUST(!set(&s,&one,&a,sizeof(IUnknown*),DDSPD_IUNKNOWNPOINTER|DDSPD_VOLATILE)&&a.refs==2);surface_memory_changed(&m);IUnknown*p=NULL;bytes=sizeof(p);MUST(get(&s,&one,&p,&bytes)==DDERR_EXPIRED&&!bytes&&a.refs==2);MUST(!clear(&s,&one)&&a.refs==1);
    MUST(!set(&s,&one,&data,4,DDSPD_VOLATILE));s.memory=&replacement;bytes=4;MUST(get(&s,&one,&value,&bytes)==DDERR_EXPIRED&&!bytes);MUST(!clear(&s,&one));replacement.content_version=0xffffffff;surface_memory_changed(&replacement);MUST(replacement.content_version==1&&!allocations);MUST(surface_GetUniquenessValue(&s,NULL)==DDERR_INVALIDPARAMS);
}
int main(void){_Static_assert(sizeof(GUID)==16,"native GUID shape");for(unsigned round=0;round<16;round++)for(api=4;api<=7;api+=3){byte_data();objects();volatile_data();}MUST(!allocations&&callbacks==96);printf("PASS actual surface private-data C: %u checks, DD4/7 adapters, copied bytes/canaries and GUID isolation, borrowed COM getters, native invalid-size erasure, OOM preservation, reentrant replacement/free/destruction, alias/content epochs and volatile expiry\n",checks);return 0;}
`;
try{const sdkFile=dir+'/sdk.c';fs.writeFileSync(sdkFile,'#define COBJMACROS\n#include <ddraw.h>\n#include <stddef.h>\n#undef IUnknown_AddRef\n#undef IUnknown_Release\n#undef InterlockedIncrement\n#undef CopyMemory\n#define CopyMemory(d,s,n) memcpy(d,s,n)\n#define DDWG_TRACE_ERROR(h) (h)\n#define SURFACE7(p) ((IDirectDrawSurface7*)(p))\nstatic int stage6_begin;\n'+shape+object+functions+tests+'\nstatic int stage6_end;');const pp=spawnSync(process.env.D9WG_CC||'i686-w64-mingw32-gcc',['-E','-P',sdkFile],{encoding:'utf8',maxBuffer:16<<20});assert.equal(pp.status,0,pp.stderr);const body=pp.stdout.split('static int stage6_begin;')[1].split('static int stage6_end;')[0];fs.writeFileSync(dir+'/test.c',head+body);const build=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
