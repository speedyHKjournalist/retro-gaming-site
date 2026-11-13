"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const source = fs.readFileSync(process.env.DDRAW_FACTORY_SOURCE || path.join(__dirname, "../ddrawproxy/ddraw_proxy.c"), "utf8");
const legacy = fs.readFileSync(path.join(__dirname, "../ddrawproxy/d3d_legacy_proxy.inc"), "utf8");
function extract(name, input=source) {
    const match = new RegExp("(?:static )?[^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(input);
    assert(match, name); let end = input.indexOf("{", match.index) + 1, depth = 1;
    for (; depth; ++end) { if (input[end] === "{") ++depth; if (input[end] === "}") --depth; }
    return input.slice(match.index, end);
}
const factory = source.slice(source.indexOf("typedef struct DDClassFactory"), source.indexOf("\n/*\n * The retail ddraw.dll"));
const ids = ["IUnknown", "IClassFactory", "IDirectDraw", "IDirectDraw2", "IDirectDraw3", "IDirectDraw4", "IDirectDraw7", "IDirectDrawClipper"];
const code = `#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef int32_t HRESULT,LONG;typedef uint32_t ULONG,DWORD;typedef int BOOL;typedef struct{unsigned Data1;}GUID;typedef const GUID*REFIID;typedef const GUID*REFCLSID;
${ids.map((n,i)=>`static const GUID IID_${n}={${i+1}};`).join("\n")}
static const GUID CLSID_DirectDraw={21},CLSID_DirectDraw7={22},CLSID_DirectDrawClipper={23},bad={99};
typedef struct IClassFactory IClassFactory;typedef struct IUnknown IUnknown;
typedef struct IClassFactoryVtbl{HRESULT(*QueryInterface)(IClassFactory*,REFIID,void**);ULONG(*AddRef)(IClassFactory*);ULONG(*Release)(IClassFactory*);HRESULT(*CreateInstance)(IClassFactory*,IUnknown*,REFIID,void**);HRESULT(*LockServer)(IClassFactory*,BOOL);}IClassFactoryVtbl;
struct IClassFactory{IClassFactoryVtbl*lpVtbl;};struct IUnknown{unsigned unused;};typedef IUnknown IDirect3D;
typedef struct DDrawObject{void*vtbl1;LONG ref;BOOL initialized,extended_factory;}DDrawObject;typedef DDrawObject IDirectDraw7,IDirectDraw,*LPDIRECTDRAW;typedef void*LPVOID;
typedef struct IDirectDrawClipper{LONG ref;}IDirectDrawClipper;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define DD_OK 0
#define S_FALSE 1
#define E_POINTER (-1)
#define E_NOINTERFACE (-2)
#define E_OUTOFMEMORY (-3)
#define CLASS_E_NOAGGREGATION (-4)
#define CLASS_E_CLASSNOTAVAILABLE (-5)
#define DDERR_ALREADYINITIALIZED (-6)
#define FAILED(hr) ((hr)<0)
#define DDWG_TRACE_ERROR(hr) (hr)
#define DDWG_TRACE_RESULT(hr) (hr)
#define DDWG_TRACE_CHECKPOINT(...)
#define DDWG_TRACE(...)
#define HOSTLOG_INFO(...)
#define DDERR_INVALIDPARAMS (-7)
#define DDERR_OUTOFMEMORY E_OUTOFMEMORY
static LONG g_object_count,g_clipper_count;static unsigned checks;static BOOL fail_allocation;
#define MUST(x) do{++checks;if(!(x)){fprintf(stderr,"line %d: %s\\n",__LINE__,#x);exit(1);}}while(0)
static LONG InterlockedIncrement(LONG*p){return ++*p;}static LONG InterlockedDecrement(LONG*p){return --*p;}
static LONG InterlockedCompareExchange(LONG*p,LONG n,LONG old){LONG was=*p;if(was==old)*p=n;return was;}
static BOOL guid_equal(REFIID a,REFIID b){return a&&b&&a->Data1==b->Data1;}static BOOL iid_is_unknown(REFIID iid){return guid_equal(iid,&IID_IUnknown);}
static void*heap_alloc_zero(size_t n){return fail_allocation?NULL:calloc(1,n);}static void heap_free(void*p){free(p);}
static DDrawObject*object_from_iface(IDirectDraw7*p){return p;}
static DDrawObject*object_create(void){DDrawObject*p=heap_alloc_zero(sizeof(*p));if(p){p->ref=1;++g_object_count;}return p;}
static void ddraw_platform_attach(DDrawObject*p){(void)p;}
static void emit_hello_once(void){}
static HRESULT ddraw_QueryInterface(IDirectDraw7*p,REFIID iid,void**out){(void)iid;*out=p;++p->ref;return S_OK;}
static ULONG ddraw_Release(IDirectDraw7*p){LONG n=--p->ref;if(!n){--g_object_count;heap_free(p);}return n;}
static HRESULT DirectDrawCreateClipper(unsigned flags,IDirectDrawClipper**out,IUnknown*outer){(void)flags;(void)outer;*out=heap_alloc_zero(sizeof(**out));if(!*out)return E_OUTOFMEMORY;(*out)->ref=1;++g_clipper_count;return S_OK;}
static HRESULT clipper_QueryInterface(IDirectDrawClipper*p,REFIID iid,void**out){*out=NULL;if(!iid_is_unknown(iid)&&!guid_equal(iid,&IID_IDirectDrawClipper))return E_NOINTERFACE;++p->ref;*out=p;return S_OK;}
static ULONG clipper_Release(IDirectDrawClipper*p){LONG n=--p->ref;if(!n){--g_clipper_count;heap_free(p);}return n;}
${extract("DirectDrawCreateEx")}
${extract("DirectDrawCreate")}
${extract("ddraw_Initialize")}
${extract("d3d1_Initialize",legacy)}
${factory}
static void entry_points(void){
 void*out=(void*)1;MUST(DirectDrawCreateEx(NULL,&out,&IID_IDirectDraw7,NULL)==DD_OK&&out);DDrawObject*d=out;MUST(d->ref==1&&d->initialized&&d->extended_factory&&!ddraw_Release(d));
 LPDIRECTDRAW old=(void*)1;MUST(DirectDrawCreate(NULL,&old,NULL)==DD_OK&&old);d=(void*)old;MUST(d->ref==1&&d->initialized&&!d->extended_factory&&!ddraw_Release(d));
 out=(void*)1;MUST(DirectDrawCreateEx(NULL,&out,&IID_IDirectDraw4,NULL)==DDERR_INVALIDPARAMS&&!out&&!g_object_count);
 out=(void*)1;MUST(DirectDrawCreateEx(NULL,&out,&IID_IDirectDraw7,(void*)1)==CLASS_E_NOAGGREGATION&&out==(void*)1&&!g_object_count);
 old=(void*)1;MUST(DirectDrawCreate(NULL,&old,(void*)1)==CLASS_E_NOAGGREGATION&&old==(void*)1&&!g_object_count);
 MUST(DirectDrawCreateEx(NULL,NULL,&IID_IDirectDraw7,NULL)==DDERR_INVALIDPARAMS&&DirectDrawCreate(NULL,NULL,NULL)==DDERR_INVALIDPARAMS);
 fail_allocation=TRUE;out=(void*)1;old=(void*)1;MUST(DirectDrawCreateEx(NULL,&out,&IID_IDirectDraw7,NULL)==DDERR_OUTOFMEMORY&&!out);MUST(DirectDrawCreate(NULL,&old,NULL)==DDERR_OUTOFMEMORY&&!old);fail_allocation=FALSE;MUST(!g_object_count);
}
static void matrix(void){
 const GUID*classes[]={&CLSID_DirectDraw,&CLSID_DirectDraw7,&CLSID_DirectDrawClipper};
 const GUID*interfaces[]={&IID_IUnknown,&IID_IDirectDraw,&IID_IDirectDraw2,&IID_IDirectDraw3,&IID_IDirectDraw4,&IID_IDirectDraw7,&IID_IDirectDrawClipper,&bad};
 MUST(DllCanUnloadNow()==S_OK);MUST(d3d1_Initialize(NULL,NULL)==DDERR_ALREADYINITIALIZED);MUST(d3d1_Initialize(NULL,&IID_IUnknown)==DDERR_ALREADYINITIALIZED);void*out=(void*)1;
 MUST(DllGetClassObject(&bad,&IID_IClassFactory,&out)==CLASS_E_CLASSNOTAVAILABLE&&!out);
 MUST(DllGetClassObject(&CLSID_DirectDraw,&bad,&out)==E_NOINTERFACE&&!out);
 MUST(DllGetClassObject(&CLSID_DirectDraw,&IID_IClassFactory,NULL)==E_POINTER);
 fail_allocation=TRUE;MUST(DllGetClassObject(&CLSID_DirectDraw,&IID_IClassFactory,&out)==E_OUTOFMEMORY&&!out);fail_allocation=FALSE;
 for(unsigned c=0;c<3;c++){
  IClassFactory*f=NULL;MUST(DllGetClassObject(classes[c],&IID_IClassFactory,(void**)&f)==S_OK&&f);MUST(DllCanUnloadNow()==S_FALSE);
  void*alias=NULL;MUST(f->lpVtbl->QueryInterface(f,&IID_IUnknown,&alias)==S_OK&&alias==f);MUST(f->lpVtbl->Release(alias)==1);
  MUST(f->lpVtbl->QueryInterface(f,&bad,&out)==E_NOINTERFACE&&!out);
  MUST(f->lpVtbl->QueryInterface(f,&IID_IUnknown,NULL)==E_POINTER);
  MUST(f->lpVtbl->CreateInstance(f,(IUnknown*)1,&IID_IUnknown,&out)==CLASS_E_NOAGGREGATION&&!out);
  MUST(f->lpVtbl->CreateInstance(f,NULL,&IID_IUnknown,NULL)==E_POINTER);
  for(unsigned i=0;i<8;i++){
   BOOL accepted=c==2?(i==0||i==6):(i<6);out=(void*)1;
   HRESULT hr=f->lpVtbl->CreateInstance(f,NULL,interfaces[i],&out);MUST(hr==(accepted?S_OK:E_NOINTERFACE));
   if(!accepted){MUST(!out);continue;}
   MUST(out);if(c==2){MUST(((IDirectDrawClipper*)out)->ref==1);MUST(!clipper_Release(out));}
   else{DDrawObject*d=out;MUST(d->ref==1&&!d->initialized);MUST(ddraw_Initialize(d,NULL)==DD_OK&&d->initialized);MUST(ddraw_Initialize(d,NULL)==DDERR_ALREADYINITIALIZED);MUST(!ddraw_Release(d));}
  }
  fail_allocation=TRUE;MUST(f->lpVtbl->CreateInstance(f,NULL,&IID_IUnknown,&out)==E_OUTOFMEMORY&&!out);fail_allocation=FALSE;
  MUST(f->lpVtbl->LockServer(f,TRUE)==S_OK);MUST(!f->lpVtbl->Release(f));MUST(DllCanUnloadNow()==S_FALSE);
  IClassFactory*second=NULL;MUST(DllGetClassObject(classes[c],&IID_IUnknown,(void**)&second)==S_OK);MUST(second->lpVtbl->LockServer(second,FALSE)==S_OK);MUST(second->lpVtbl->LockServer(second,FALSE)==S_OK);MUST(!second->lpVtbl->Release(second));MUST(DllCanUnloadNow()==S_OK);
 }
 /* Factories may go away before the objects they create. */
 for(unsigned c=0;c<3;c++){IClassFactory*f=NULL;MUST(DllGetClassObject(classes[c],&IID_IClassFactory,(void**)&f)==S_OK);MUST(f->lpVtbl->CreateInstance(f,NULL,&IID_IUnknown,&out)==S_OK);MUST(!f->lpVtbl->Release(f)&&DllCanUnloadNow()==S_FALSE);if(c==2)clipper_Release(out);else ddraw_Release(out);MUST(DllCanUnloadNow()==S_OK);}
}
int main(void){for(unsigned i=0;i<64;i++){entry_points();matrix();}MUST(!g_object_count&&!g_clipper_count&&!g_class_factory_count&&!g_server_locks);printf("PASS actual DirectDraw COM factory C: %u checks, three classes, interface selection, one-shot Initialize, failure atomicity and unload lifetimes\\n",checks);return 0;}
`;
const directory=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-factory-"));
try {
    const file=directory+"/factory.c", binary=directory+"/factory";fs.writeFileSync(file,code);
    const built=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",file,"-o",binary],{encoding:"utf8"});
    assert.equal(built.status,0,built.stderr);const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);
} finally {fs.rmSync(directory,{recursive:true,force:true});}
