"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-chain-lifetime-"));
function extract(s, name) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name);
    let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    return s.slice(m.index, end);
}
try {
    const source = fs.readFileSync(process.env.D9WG_CHAIN_LIFETIME_SOURCE || root + "/d3d9proxy/d3d9_proxy.c", "utf8");
    fs.writeFileSync(dir + "/chain.c", `#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "d3d9proxy/d3d9_protocol.h"
typedef uint32_t UINT,DWORD,ULONG;typedef int32_t LONG,HRESULT;typedef int BOOL;typedef const int*REFIID;
#define WINAPI
#define D3D_OK 0
#define S_OK 0
#define E_POINTER (-1)
#define E_NOINTERFACE (-2)
#define D3DERR_INVALIDCALL (-3)
#define TRACE(...)
#define TRACE_MARK_ENTER(...)
#define TRACE_MARK_EXIT(...)
typedef struct{void*lpVtbl;}Interface;typedef Interface IDirect3DSwapChain9,IDirect3DDevice9;
typedef struct Device Device;typedef struct Chain Chain;typedef struct Surface{Interface iface;Device*device;LONG refcount,binding_refs;Chain*swap_chain;}Surface;typedef Surface D9Surface;
struct Chain{Interface iface;LONG refcount;Device*device;UINT handle;Chain*next_device_chain;Surface*back_buffer;};
struct Device{Interface iface;LONG refcount,child_parent_refs;UINT handle;Chain implicit_swap_chain,*additional_swap_chains;};
typedef Chain D9SwapChain;typedef Device D9Device;
static const int unknown_id=0,IID_IDirect3DSwapChain9=1,bad_iid=99;
static BOOL iid_is_unknown(REFIID p){return p&&!*p;}static BOOL guid_equal(REFIID a,REFIID b){return a&&b&&*a==*b;}
static LONG InterlockedIncrement(LONG*p){return ++*p;}static LONG InterlockedDecrement(LONG*p){assert(*p>0);return --*p;}
static LONG InterlockedCompareExchange(LONG*p,LONG v,LONG c){LONG old=*p;if(old==c)*p=v;return old;}
static Surface*surface_from_iface(void*p){return (Surface*)p;}
static Device*device_from_iface(Interface*p){return (Device*)p;}static Chain*swap_chain_from_iface(Interface*p){return (Chain*)p;}
static unsigned live,device_destroys,chain_destroys,surface_destroys;static UINT retired[4];
static void*GetProcessHeap(void){return NULL;}static void*allocate(size_t n){void*p=calloc(1,n);assert(p);live++;return p;}
static void HeapFree(void*h,unsigned f,void*p){(void)h;(void)f;assert(p&&live);live--;free(p);}
static ULONG device_add_ref(Interface*p){return (ULONG)++device_from_iface(p)->refcount;}
static ULONG device_release(Interface*p){Device*d=device_from_iface(p);assert(d->refcount>0);ULONG refs=(ULONG)--d->refcount;if(!refs){assert(!d->child_parent_refs&&!d->additional_swap_chains&&!d->implicit_swap_chain.refcount);device_destroys++;HeapFree(GetProcessHeap(),0,d);}return refs;}
#define IDirect3DDevice9_AddRef device_add_ref
#define IDirect3DDevice9_Release device_release
${["device_child_add_ref", "device_child_release"].map(n => extract(source, n)).join("\n")}
static void surface_destroy_storage(Surface*s){assert(!s->refcount&&!s->binding_refs);surface_destroys++;HeapFree(GetProcessHeap(),0,s);}
static BOOL emit_command(UINT op,const void*p,UINT n){const D9WGDestroySwapChain*q=p;assert(op==D9WG_OP_DESTROY_SWAP_CHAIN&&n==sizeof(*q)&&q->device_handle==77&&q->swap_chain_handle>=100&&q->swap_chain_handle<=102);assert(!retired[q->swap_chain_handle-100]++);chain_destroys++;return 1;}
${["swap_chain_destroy_storage", "swap_chain_add_ref", "swap_chain_release"].map(n => extract(source, n)).join("\n")}
#define IDirect3DSwapChain9_AddRef swap_chain_add_ref
${["device_get_swap_chain", "swap_chain_query_interface", "swap_chain_get_device"].map(n => extract(source, n)).join("\n")}
static Device*new_device(void){Device*d=allocate(sizeof(*d));d->refcount=1;d->handle=77;d->implicit_swap_chain.device=d;return d;}
static Chain*new_chain(Device*d,UINT i){Chain*c=allocate(sizeof(*c));*c=(Chain){.refcount=1,.device=d,.handle=100+i,.next_device_chain=d->additional_swap_chains};d->additional_swap_chains=c;device_child_add_ref(d);c->back_buffer=allocate(sizeof(*c->back_buffer));*c->back_buffer=(Surface){.device=d,.swap_chain=c};return c;}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Swap-chain lifetime line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){unsigned checks=0;Device*d=new_device();Interface*a=NULL,*b=NULL;void*q=(void*)(uintptr_t)1;
CHECK(InterlockedCompareExchange(&d->refcount,0,0)==1);
CHECK(device_get_swap_chain(&d->iface,0,NULL)==D3DERR_INVALIDCALL);
CHECK(device_get_swap_chain(&d->iface,1,&a)==D3DERR_INVALIDCALL&&!a);
for(unsigned cycle=0;cycle<128;cycle++){
 CHECK(!device_get_swap_chain(&d->iface,0,&a)&&a==&d->implicit_swap_chain.iface&&d->refcount==2&&d->child_parent_refs==1);
 CHECK(!device_get_swap_chain(&d->iface,0,&b)&&a==b&&d->implicit_swap_chain.refcount==2&&d->refcount==2&&d->child_parent_refs==1);
 CHECK(swap_chain_add_ref(a)==3&&d->refcount==2&&d->child_parent_refs==1);
 CHECK(!swap_chain_query_interface(a,&unknown_id,&q)&&q==a&&d->implicit_swap_chain.refcount==4&&d->refcount==2);
 CHECK(swap_chain_query_interface(a,&bad_iid,&q)==E_NOINTERFACE&&!q&&d->refcount==2);
 CHECK(swap_chain_query_interface(a,&unknown_id,NULL)==E_POINTER);
 CHECK(swap_chain_get_device(a,NULL)==D3DERR_INVALIDCALL);
 CHECK(!swap_chain_get_device(a,&b)&&b==&d->iface&&d->refcount==3&&device_release(b)==2);
 for(unsigned count=3;count>0;count--)CHECK(swap_chain_release(a)==count&&d->refcount==2&&d->child_parent_refs==1);
 CHECK(!swap_chain_release(a)&&d->refcount==1&&!d->child_parent_refs);
 CHECK(!swap_chain_release(a)&&!swap_chain_release(a)&&d->refcount==1);
 CHECK(swap_chain_add_ref(a)==1&&d->refcount==2&&d->child_parent_refs==1);
 CHECK(!swap_chain_release(a)&&d->refcount==1&&!d->child_parent_refs);
}
Chain*chains[3];for(UINT i=0;i<3;i++)chains[i]=new_chain(d,i);
CHECK(d->refcount==4&&d->child_parent_refs==3);
for(UINT i=0;i<3;i++)for(UINT n=2;n<=5;n++)CHECK(swap_chain_add_ref(&chains[i]->iface)==n&&d->refcount==4&&d->child_parent_refs==3);
for(UINT i=0;i<3;i++)for(UINT n=4;n>0;n--)CHECK(swap_chain_release(&chains[i]->iface)==n&&d->refcount==4&&d->child_parent_refs==3);
for(UINT i=0;i<3;i++){UINT j=i==0?1:i==1?0:2;CHECK(!swap_chain_release(&chains[j]->iface)&&d->refcount==(LONG)(3-i)&&d->child_parent_refs==(LONG)(2-i));}
CHECK(!d->additional_swap_chains&&chain_destroys==3&&surface_destroys==3&&!device_destroys);
CHECK(!device_release(&d->iface)&&!live&&device_destroys==1);
/* Last remaining child release may destroy the device and embedded chain. */
d=new_device();CHECK(!device_get_swap_chain(&d->iface,0,&a));CHECK(device_release(&d->iface)==1&&live==1);CHECK(!swap_chain_release(a)&&!live&&device_destroys==2);
d=new_device();Chain*c=new_chain(d,0);retired[0]=0;CHECK(device_release(&d->iface)==1);CHECK(!swap_chain_release(&c->iface)&&!live&&device_destroys==3&&chain_destroys==4&&surface_destroys==4);
printf("PASS actual D3D9 swap-chain lifetime: %u checks, 128 implicit revivals, stable device pins, three additional chains, exact unlink/destruction and last-child device teardown\\n",checks);return 0;}
`);
    const build = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", "-I", root, dir + "/chain.c", "-o", dir + "/chain"], {encoding: "utf8"}); assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(dir + "/chain", [], {encoding: "utf8"}); assert.equal(run.status, 0, run.stdout + run.stderr + " signal=" + run.signal); process.stdout.write(run.stdout);
} finally {fs.rmSync(dir, {recursive: true, force: true});}
