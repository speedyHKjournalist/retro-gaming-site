"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),eight=fs.readFileSync(process.env.D8WG_FACTORY_CORE_SOURCE||root+"/d3d8proxy/d3d8_proxy.c","utf8"),nine=fs.readFileSync(process.env.D8WG_FACTORY_CORE_BACKEND_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-factory-core-"));
function extract(source,name){const m=new RegExp("^[^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
try{
 const code=`#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
typedef uint32_t UINT,DWORD,ULONG;typedef int32_t LONG,HRESULT;typedef int BOOL;typedef void*HMONITOR;typedef const void*REFIID;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define HEAP_ZERO_MEMORY 8
#define D3D_SDK_VERSION_DX80 120u
#define D3D_SDK_VERSION_DX81 220u
#define D3D_SDK_VERSION_9B 31u
#define D3D_SDK_VERSION_9C 32u
#define D3D_SDK_VERSION_DEBUG_BIT 0x80000000u
#define D8WG_TRACE(...)
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE(...)
#define E_POINTER (-1)
#define E_NOINTERFACE (-2)
#define S_OK 0
typedef struct{const void*lpVtbl;}Interface;typedef Interface IDirect3D8,IDirect3D9;
typedef struct{Interface iface;LONG refcount;}D9Direct3D;
typedef struct{Interface iface;LONG refcount;IDirect3D9*backend;}D8Direct3D;
static int g_d8_vtbl,g_d9_vtbl,IID_IUnknown,IID_IDirect3D8,g_transport_lock;
static UINT checks,live,allocations,fail_allocation,transport_calls,backend_releases,adapter_calls,monitor_calls,hello_eight,hello_nine,adapter_value;
static BOOL transport_ok=TRUE;
static void*GetProcessHeap(void){return NULL;}
static void*HeapAlloc(void*h,UINT flags,size_t n){(void)h;assert(flags==8&&n);if(++allocations==fail_allocation)return NULL;void*p=calloc(1,n);assert(p);live++;return p;}
static void HeapFree(void*h,UINT flags,void*p){(void)h;assert(!flags&&p&&live);live--;free(p);}
static LONG InterlockedIncrement(LONG*p){return ++*p;}static LONG InterlockedDecrement(LONG*p){assert(*p>0);return --*p;}
static void EnterCriticalSection(int*p){assert(!*p);*p=1;}static void LeaveCriticalSection(int*p){assert(*p==1);*p=0;}
static BOOL open_transport_locked(void){assert(g_transport_lock);transport_calls++;return transport_ok;}
static void emit_hello_once(void){hello_eight++;}static void emit_nine_hello(void){hello_nine++;}
static D8Direct3D*d3d_from_iface(Interface*p){return (D8Direct3D*)p;}static D9Direct3D*d9_from_iface(Interface*p){return (D9Direct3D*)p;}
static BOOL iid_is_unknown(REFIID p){return p==&IID_IUnknown;}static BOOL guid_equal(REFIID a,REFIID b){return a==b;}
${extract(nine,"d3d_create_root").replace(/g_d3d_vtbl/g,"g_d9_vtbl").replace(/emit_hello_once/g,"emit_nine_hello")}
${extract(nine,"d3d_release").replace(/\bd3d_release\b/g,"backend_release_actual").replace(/\bd3d_from_iface\b/g,"d9_from_iface")}
static ULONG backend_release(Interface*p){backend_releases++;return backend_release_actual(p);}
static UINT backend_adapter_count(Interface*p){assert(p&&p->lpVtbl==&g_d9_vtbl);adapter_calls++;return adapter_value;}
static HMONITOR backend_adapter_monitor(Interface*p,UINT adapter){assert(p&&p->lpVtbl==&g_d9_vtbl);monitor_calls++;return adapter?NULL:(void*)(uintptr_t)0x1234;}
#define IDirect3D9_Release backend_release
#define IDirect3D9_GetAdapterCount backend_adapter_count
#define IDirect3D9_GetAdapterMonitor backend_adapter_monitor
${["d9wg_d3d9_core_create8","d9wg_d3d9_core_root_release","d9wg_d3d9_core_adapter_count","d9wg_d3d9_core_adapter_monitor"].map(n=>extract(nine,n)).join("\n")}
${extract(eight,"d3d_add_ref")}
#define IDirect3D8_AddRef d3d_add_ref
${["d3d_query_interface","d3d_release","d3d_get_adapter_count","d3d_get_adapter_monitor","Direct3DCreate8"].map(n=>extract(eight,n).replace(/g_d3d_vtbl/g,"g_d8_vtbl")).join("\n")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"D3D8 factory core line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){
 (void)&InterlockedIncrement;(void)&d9wg_d3d9_core_create8;(void)&d9wg_d3d9_core_root_release;(void)&d9wg_d3d9_core_adapter_count;(void)&d9wg_d3d9_core_adapter_monitor;
 UINT profiles=0,queries=0;
 for(UINT round=0;round<64;round++)for(UINT sdk=0;sdk<2;sdk++)for(UINT order=0;order<4;order++){
  UINT old_backend_releases=backend_releases,old_hello=hello_eight,old_adapter=adapter_calls,old_monitor=monitor_calls;adapter_value=7+round;
  Interface*iface=Direct3DCreate8(sdk?220:120);CHECK(iface&&iface->lpVtbl==&g_d8_vtbl);D8Direct3D*wrapper=(void*)iface;IDirect3D9*backend=wrapper->backend;
  CHECK(backend&&backend->lpVtbl==&g_d9_vtbl&&((D9Direct3D*)backend)->refcount==1&&wrapper->refcount==1&&live==2&&!hello_nine&&hello_eight==old_hello+1);
  for(UINT cycle=0;cycle<32;cycle++){
   CHECK(d3d_get_adapter_count(iface)==adapter_value&&d3d_get_adapter_monitor(iface,0)==(void*)(uintptr_t)0x1234&&!d3d_get_adapter_monitor(iface,0xffffffffu));
   void*view=NULL;CHECK(!d3d_query_interface(iface,&IID_IUnknown,&view)&&view==iface&&wrapper->refcount==2);CHECK(d3d_release(view)==1);
   CHECK(!d3d_query_interface(iface,&IID_IDirect3D8,&view)&&view==iface&&d3d_release(view)==1&&((D9Direct3D*)backend)->refcount==1);queries++;
  }
  CHECK(adapter_calls==old_adapter+32&&monitor_calls==old_monitor+64&&!hello_nine&&backend_releases==old_backend_releases);
  CHECK(d3d_query_interface(iface,&IID_IUnknown,NULL)==E_POINTER);void*out=(void*)1;CHECK(d3d_query_interface(iface,(void*)(uintptr_t)1,&out)==E_NOINTERFACE&&!out);
  if(order==0){CHECK(!d3d_release(iface));}
  else if(order==1){CHECK(d3d_add_ref(iface)==2&&d3d_release(iface)==1&&!d3d_release(iface));}
  else {CHECK(d3d_add_ref(iface)==2&&d3d_release(iface)==1&&wrapper->backend==backend&&((D9Direct3D*)backend)->refcount==1);CHECK(d3d_get_adapter_count(iface)==adapter_value);
   if(order==3){CHECK(d3d_add_ref(iface)==2&&d3d_release(iface)==1);}CHECK(!d3d_release(iface));}
  CHECK(!live&&backend_releases==old_backend_releases+1);profiles++;
 }
 UINT old_allocations=allocations,old_transport=transport_calls,old_hello=hello_eight;
 CHECK(!Direct3DCreate8(0)&&!Direct3DCreate8(32)&&!Direct3DCreate8(120|0x80000000u)&&allocations==old_allocations&&transport_calls==old_transport);
 fail_allocation=allocations+1;CHECK(!Direct3DCreate8(120)&&!live&&transport_calls==old_transport);fail_allocation=allocations+2;CHECK(!Direct3DCreate8(220)&&!live&&transport_calls==old_transport+1);fail_allocation=0;
 transport_ok=FALSE;CHECK(!Direct3DCreate8(220)&&!live&&hello_eight==old_hello&&!hello_nine);transport_ok=TRUE;
 printf("PASS actual D3D8 factory/core C: %u checks, %u SDK/owner profiles, %u delegated query/identity rounds; one backend reference per wrapper, zero backend Hello, exact public identity, final-parent lifetime and atomic allocation/transport failures\\n",checks,profiles,queries);return 0;
}
`;
 fs.writeFileSync(dir+"/factory.c",code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/factory.c","-o",dir+"/factory"],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const result=spawnSync(dir+"/factory",[],{encoding:"utf8"});assert.equal(result.status,0,result.stderr+" signal="+result.signal);process.stdout.write(result.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
