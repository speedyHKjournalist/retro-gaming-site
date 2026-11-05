"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, "..");
const source = fs.readFileSync(process.env.D8WG_DEVICE_FRONTEND_SOURCE || root + "/d3d8proxy/d3d8_core_device.c", "utf8");
const types = source.slice(source.indexOf("typedef struct D8CoreDevice"), source.indexOf("static LONG g_core_shader8_token"));
const functions = source.slice(source.indexOf("static D8CoreDevice *core_device8("), source.indexOf("static HRESULT WINAPI core_device8_QueryInterface"));
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "device8-lifetime-"));
try {
    fs.writeFileSync(dir + "/test.c", `
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t UINT,DWORD,ULONG; typedef int32_t LONG,HRESULT; typedef int BOOL; typedef unsigned char BYTE;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D9WG_TEXTURE_STAGE_STATES8 32
#define HEAP_ZERO_MEMORY 8
#define D3DERR_INVALIDCALL (-1)
typedef struct IUnknown IUnknown;
typedef IUnknown IDirect3D8, IDirect3DDevice8;
typedef struct IDirect3DDevice8Vtbl {HRESULT(*QueryInterface)(IUnknown*,const void*,void**); ULONG(*AddRef)(IUnknown*); ULONG(*Release)(IUnknown*);} IDirect3DDevice8Vtbl;
struct IUnknown {const IDirect3DDevice8Vtbl *lpVtbl;};
struct IDirect3DDevice9 {const IDirect3DDevice8Vtbl *lpVtbl;};
struct IDirect3DVertexDeclaration9;
typedef struct D9WGDeviceMethods8 D9WGDeviceMethods8;
#define IUnknown_AddRef(p) ((p)->lpVtbl->AddRef(p))
#define IUnknown_Release(p) ((p)->lpVtbl->Release(p))
#define IDirect3D8_Release(p) IUnknown_Release(p)
#define IDirect3DDevice8_AddRef(p) IUnknown_AddRef(p)
#define IDirect3DDevice8_Release(p) IUnknown_Release(p)
static LONG InterlockedIncrement(LONG *p){return ++*p;}
static LONG InterlockedDecrement(LONG *p){assert(*p>0);return --*p;}
static unsigned allocations, private_holds, retired_count, cleanup_count, checks;
static void *GetProcessHeap(void){return NULL;}
static void HeapFree(void *heap, DWORD flags, void *p){(void)heap;(void)flags; assert(p&&allocations);--allocations;free(p);}
static void *allocate(size_t size){void *p=calloc(1,size);assert(p);++allocations;return p;}
static HRESULT d9wg_d3d9_core_shader_storage_release8(struct IDirect3DDevice9*,IUnknown*);
static HRESULT d9wg_d3d9_core_release_owned8(struct IDirect3DDevice9*);
${types}
${functions}
static IDirect3DDevice8Vtbl g_core_device8_vtbl={NULL,core_device8_AddRef,core_device8_Release};
typedef struct Parent {IUnknown iface;UINT refs;} Parent;
typedef struct Backend {struct IDirect3DDevice9 iface;UINT refs;BOOL guard,dead;D8CoreDevice *view;} Backend;
static UINT reentry_mode; static D8CoreDevice *revived;
static ULONG parent_add(IUnknown *p){return ++((Parent*)p)->refs;}
static ULONG parent_release(IUnknown *p){assert(((Parent*)p)->refs);return --((Parent*)p)->refs;}
static const IDirect3DDevice8Vtbl parent_vtbl={NULL,parent_add,parent_release};
static ULONG backend_add(IUnknown *p){Backend*b=(Backend*)p;assert(!b->dead);return ++b->refs;}
static void cleanup(Backend*b){++cleanup_count;core_device8_cleanup(b->view);}
static ULONG backend_release(IUnknown*p){
    Backend*b=(Backend*)p;assert(!b->dead&&b->refs);--b->refs;
    if(!b->refs&&!b->guard){
        b->guard=TRUE;++b->refs;cleanup(b);--b->refs;b->guard=FALSE;
        if(!b->refs){assert(b->view->metadata_refs>=2&&b->view->children==0);core_device8_retire(b->view);b->view=NULL;b->dead=TRUE;++retired_count;}
    }
    return b->refs;
}
static const IDirect3DDevice8Vtbl backend_vtbl={NULL,backend_add,backend_release};
static HRESULT d9wg_d3d9_core_release_owned8(struct IDirect3DDevice9*p){
    Backend*b=(Backend*)p;assert(!b->dead);if(b->guard)return D3DERR_INVALIDCALL;
    b->guard=TRUE;++b->refs;cleanup(b);b->guard=FALSE;backend_release((IUnknown*)p);return 0;
}
static HRESULT d9wg_d3d9_core_shader_storage_release8(struct IDirect3DDevice9*p,IUnknown*shader){
    Backend*b=(Backend*)p;assert(!b->dead&&private_holds&&shader);--private_holds;
    if(reentry_mode){UINT mode=reentry_mode;reentry_mode=0;revived=b->view;
        assert(core_device8_AddRef(&revived->iface)>=1);
        if(mode==1){core_device8_Release(&revived->iface);revived=NULL;}
    }
    return 0;
}
static D8CoreDevice *make(Parent*parent,Backend*backend){
    D8CoreDevice*d=allocate(sizeof(*d));d->iface.lpVtbl=&g_core_device8_vtbl;
    d->refs=d->metadata_refs=1;d->parent=&parent->iface;d->backend=&backend->iface;
    parent_add(&parent->iface);backend->refs=1;backend->view=d;backend->iface.lpVtbl=&backend_vtbl;return d;
}
static void add_bound_shader(D8CoreDevice*d){
    D8CoreShader*s=allocate(sizeof(*s));s->device=d;s->refs=1;s->deleted=TRUE;
    s->program=(IUnknown*)(uintptr_t)1;s->next=d->shaders;d->shaders=s;d->live.vs=s;++private_holds;
    d->blocks=allocate(sizeof(*d->blocks));d->blocks->raw.vs=s;++s->refs;
    d->recording=allocate(sizeof(*d->recording));d->recording->raw.vs=s;++s->refs;
}
int main(void){
    for(UINT iteration=0;iteration<1024;++iteration){
        Parent parent={{&parent_vtbl},1};Backend backend={0};D8CoreDevice*d=make(&parent,&backend);
        UINT children=iteration%7;
        for(UINT i=0;i<children;++i)assert(d8wg_core_device_child_add8(&d->iface)==i+2);
        add_bound_shader(d);reentry_mode=iteration%3;
        assert(core_device8_Release(&d->iface)==children);
        if(revived){assert(!backend.dead);core_device8_Release(&revived->iface);revived=NULL;}
        if(children){
            assert(!backend.dead&&d->refs==(LONG)children&&d->children==(LONG)children);
            assert(!d->blocks&&!d->recording&&!d->live.vs&&!d->shaders);
            assert(core_device8_AddRef(&d->iface)==children+1);
            assert(core_device8_Release(&d->iface)==children);
            for(UINT i=children;i;--i)assert(d8wg_core_device_child_release8(&d->iface)==i-1);
        }
        assert(backend.dead&&backend.refs==0&&parent.refs==1&&!allocations&&!private_holds);checks+=10+children;
    }
    Parent fixture={{&parent_vtbl},1};
    assert(d8wg_core_device_child_add8(&fixture.iface)==2);
    assert(d8wg_core_device_child_release8(&fixture.iface)==1);
    printf("PASS Device8 actual frontend lifetime: %u checks, 1024 child/reentry cases, %u retirements, %u cleanups; backend COM shim only\\n",checks,retired_count,cleanup_count);
}
`);
    const build = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", dir + "/test.c", "-o", dir + "/test"], {encoding:"utf8"});
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(dir + "/test", [], {encoding:"utf8",timeout:30000});
    assert.equal(run.status, 0, run.stderr + " signal=" + run.signal);
    process.stdout.write(run.stdout);
} finally { fs.rmSync(dir,{recursive:true,force:true}); }
