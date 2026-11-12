"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "ddraw-enum-"));
const source = fs.readFileSync(process.env.DDRAW_ENUM_SOURCE || root + "/ddrawproxy/ddraw_proxy.c", "utf8");
const plain = source.replace(/\\\n/g, "\n");
function extract(name) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(plain);
    assert(m, name); let end = plain.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) { if (plain[end] === "{") depth++; if (plain[end] === "}") depth--; }
    return plain.slice(m.index, end).replace(/\bWINAPI\b/g, "");
}
const macros = plain.slice(plain.indexOf("typedef struct { ENUMSURFCB callback;"), plain.indexOf("static HRESULT WINAPI tag##_FlipToGDISurface"));
const adapters = [1, 2, 3, 4].map(v => macros.replaceAll("tag##", "api" + v).replace(/\bENUMSURFCB\b/g, v < 4 ? "SurfaceCB1" : "SurfaceCB4").replace(/\bSURFIFACE\b/g, v < 4 ? "IDirectDrawSurface" : "IDirectDrawSurface4").replace(/\bIFACE\b/g, "IDirectDraw").replace(/\bVERSION\b/g, String(v)).replace(/\bDESC\b/g, v < 4 ? "DDSURFACEDESC" : "DDSURFACEDESC2")).join("\n");
const modeAdapters = [1, 2, 3, 4].map(v => extract("tag##_EnumDisplayModes").replaceAll("tag##", "api" + v).replace(/\bENUMMODESCB\b/g, v < 4 ? "ModeCB1" : "ModeCB2").replace(/\bIFACE\b/g, "IDirectDraw").replace(/\bDESC\b/g, v < 4 ? "DDSURFACEDESC" : "DDSURFACEDESC2")).join("\n");
const engine = plain.slice(plain.indexOf("typedef HRESULT (*DDrawEnumSurfaceCallback)"), plain.indexOf("static HRESULT WINAPI ddraw_FlipToGDISurface"));
const functions = ["describe_pixel_format", "mode_pixel_format", "surface_pitch_for", "desc1_to_desc2", "desc2_to_desc1", "legacy_widen", "surface_factory_view", "ddraw_enum_pixel_format_matches", "ddraw_enum_color_key_matches", "ddraw_enum_description_matches", "ddraw_EnumDisplayModes"].map(extract).join("\n");
const modes = plain.match(/static const struct \{ DWORD width; DWORD height; \} g_modes\[\] = \{[\s\S]*?g_mode_depths\[\] = \{[^}]+};/)[0];
const stubs = String.raw`
typedef struct DDSurface DDSurface;
typedef struct { DDSurface *surfaces; DWORD mode_bpp; } DDrawObject;
struct DDSurface { LONG ref, public_refs[8]; DDSURFACEDESC2 desc; DDSurface *next; unsigned serial; };
static unsigned checks, allocations, fail_allocation, callbacks, cancel_at, want_null, view, seen[64], mutate;
static int g_object_lock;
static DDrawObject owner;
#define MUST(x) do { checks++; if (!(x)) { fprintf(stderr,"enum line %u: %s\n",__LINE__,#x); exit(1); } } while(0)
static void *heap_alloc_zero(size_t n) { if (fail_allocation) return NULL; void *p=calloc(1,n); MUST(p); allocations++; return p; }
static void heap_free(void *p) { if (p) { MUST(allocations); allocations--; free(p); } }
static void EnterCriticalSection(int *p) { MUST(!(*p)++); }
static void LeaveCriticalSection(int *p) { MUST((*p)--==1); }
static DDrawObject *object_from_iface(void *p) { return p; }
static DDrawObject *object_from_any(void *p, unsigned *v) { (void)v; return p; }
static void *surface_interface(DDSurface *s, unsigned v) { MUST(v==1||v==4||v==7); return s; }
static ULONG surface_private_addref(DDSurface *s) { MUST(s->ref>0); return ++s->ref; }
static ULONG surface_private_release(DDSurface *s) {
    MUST(s->ref>0); if (--s->ref) return s->ref;
    DDSurface **link=&owner.surfaces; while (*link && *link!=s) link=&(*link)->next;
    MUST(*link==s); *link=s->next; heap_free(s); return 0;
}
static ULONG surface_public_addref(DDSurface *s, unsigned v) { surface_private_addref(s); return ++s->public_refs[v]; }
static ULONG surface_public_release(DDSurface *s, unsigned v) { MUST(s->public_refs[v]>0); ULONG n=--s->public_refs[v]; surface_private_release(s); return n; }
static UINT format_bytes_per_pixel(unsigned f) { const D9WGFormatLayout *l=d9wg_find_format(f); return l&&l->block_height==1 ? l->block_bytes/l->block_width : 0; }
static UINT format_compressed_block_bytes(unsigned f) { const D9WGFormatLayout *l=d9wg_find_format(f); return l&&l->block_height==4 ? l->block_bytes : 0; }
`;
const tests = String.raw`
static DDSurface *make_surface(unsigned serial, unsigned width) {
    DDSurface *s=heap_alloc_zero(sizeof(*s)); s->ref=1; s->public_refs[view]=1; s->serial=serial;
    s->desc=(DDSURFACEDESC2){.dwSize=sizeof(s->desc),.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PITCH|DDSD_PIXELFORMAT,.dwWidth=width,.dwHeight=32,.lPitch=width*4,.ddsCaps={.dwCaps=DDSCAPS_SYSTEMMEMORY|DDSCAPS_OFFSCREENPLAIN}};
    mode_pixel_format(32,&s->desc.ddpfPixelFormat); s->next=owner.surfaces; owner.surfaces=s; return s;
}
static HRESULT common_callback(DDSurface *s, const DDSURFACEDESC2 *d, void *context) {
    MUST(context==(void*)(uintptr_t)0x1234 && !g_object_lock && callbacks<64 && !!s==!want_null);
    MUST(d->dwWidth&&d->dwHeight&&(d->dwFlags&(DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT))==(DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT));
    if (s) {
        MUST(s->public_refs[view]>=1 && s->ref>s->public_refs[view]); seen[callbacks]=s->serial;
        for(unsigned v=0;v<8;v++) if(v!=view) MUST(!s->public_refs[v]);
        surface_public_release(s,view);
    } else { MUST(d->lPitch==(LONG)surface_pitch_for(d->dwWidth,4) && d->ddpfPixelFormat.dwRGBBitCount==32); seen[callbacks]=d->dwWidth; }
    callbacks++;
    if (mutate && callbacks==1) {
        DDSurface *victim=owner.surfaces->next; MUST(victim && victim->ref>=2);
        surface_public_release(victim,view); /* Snapshot keeps the later object alive. */
        make_surface(99,64); /* New object must not be included in this enumeration. */
        mutate=0;
    }
    return cancel_at && callbacks==cancel_at ? DDENUMRET_CANCEL : DDENUMRET_OK;
}
static HRESULT callback1(IDirectDrawSurface *p,DDSURFACEDESC *d,void *c) { DDSURFACEDESC2 d2; MUST(d->dwSize==sizeof(*d)); desc1_to_desc2(d,&d2); return common_callback(p,&d2,c); }
static HRESULT callback4(IDirectDrawSurface4 *p,DDSURFACEDESC2 *d,void *c) { MUST(d->dwSize==sizeof(*d)); return common_callback(p,d,c); }
static HRESULT callback7(IDirectDrawSurface7 *p,DDSURFACEDESC2 *d,void *c) { MUST(d->dwSize==sizeof(*d)); return common_callback(p,d,c); }
static HRESULT enumerate(unsigned api,DWORD flags,DDSURFACEDESC2 *filter) {
    DDSURFACEDESC old; if(filter)desc2_to_desc1(filter,&old);
    if(api==1)return api1_EnumSurfaces(&owner,flags,filter?&old:NULL,(void*)(uintptr_t)0x1234,callback1);
    if(api==2)return api2_EnumSurfaces(&owner,flags,filter?&old:NULL,(void*)(uintptr_t)0x1234,callback1);
    if(api==3)return api3_EnumSurfaces(&owner,flags,filter?&old:NULL,(void*)(uintptr_t)0x1234,callback1);
    if(api==4)return api4_EnumSurfaces(&owner,flags,filter,(void*)(uintptr_t)0x1234,callback4);
    return ddraw_EnumSurfaces(&owner,flags,filter,(void*)(uintptr_t)0x1234,callback7);
}
static void run(unsigned api,DWORD flags,DDSURFACEDESC2 *filter,HRESULT expected,unsigned count) {
    callbacks=0; memset(seen,0,sizeof(seen)); MUST(enumerate(api,flags,filter)==expected && callbacks==count && !g_object_lock);
}
static void existing(unsigned api) {
    view=api<4?1:api; owner.mode_bpp=32; DDSurface *a=make_surface(1,32),*b=make_surface(2,16);
    DDSURFACEDESC2 f=b->desc;
    for(unsigned flags=0;flags<64;flags++) {
        want_null=!!(flags&DDENUMSURFACES_CANBECREATED);
        BOOL valid=flags==10||flags==16||flags==17||flags==18||flags==20||flags==26;
        run(api,flags,&f,valid?DD_OK:DDERR_INVALIDPARAMS,valid?(flags==17?2:1):0);
    }
    want_null=0; run(api,17,NULL,DD_OK,2); run(api,18,NULL,DDERR_INVALIDPARAMS,0);
    f.dwFlags=DDSD_WIDTH|DDSD_HEIGHT; run(api,18,&f,DD_OK,1); MUST(seen[0]==2);
    run(api,20,&f,DD_OK,1); MUST(seen[0]==1);
    f.dwFlags=0; run(api,16,&f,DD_OK,2); run(api,20,&f,DD_OK,0);
    f.dwFlags=DDSD_CAPS; f.ddsCaps.dwCaps=DDSCAPS_SYSTEMMEMORY; run(api,18,&f,DD_OK,0);
    f.ddsCaps=a->desc.ddsCaps; run(api,18,&f,DD_OK,2);
    f.dwFlags=DDSD_WIDTH; run(api,18,&f,DDERR_INVALIDPARAMS,0);
    f.dwFlags=0; cancel_at=1; run(api,17,NULL,DD_OK,1); cancel_at=0;
    MUST(a->ref==1&&b->ref==1&&a->public_refs[view]==1&&b->public_refs[view]==1);
    fail_allocation=1; run(api,17,NULL,DDERR_OUTOFMEMORY,0); fail_allocation=0;
    MUST(a->ref==1&&b->ref==1);
    mutate=1; run(api,17,NULL,DD_OK,2); MUST(seen[0]==2&&seen[1]==1);
    MUST(owner.surfaces->serial==99&&owner.surfaces->next==b&&b->next==NULL);
    while(owner.surfaces) surface_public_release(owner.surfaces,view);
    MUST(!allocations); run(api,17,NULL,DD_OK,0);
}
static void creatable(unsigned api) {
    want_null=1; DDSURFACEDESC2 f={.dwSize=sizeof(f),.dwFlags=DDSD_CAPS,.ddsCaps={.dwCaps=DDSCAPS_TEXTURE}};
    run(api,10,&f,DD_OK,10); MUST(seen[0]==320&&seen[9]==1280&&!allocations&&!owner.surfaces);
    cancel_at=2; run(api,10,&f,DD_OK,2); cancel_at=0;
    f.dwFlags|=DDSD_WIDTH|DDSD_HEIGHT; f.dwWidth=f.dwHeight=32;
    run(api,10,&f,DD_OK,1); MUST(seen[0]==32); run(api,26,&f,DD_OK,1); MUST(seen[0]==32);
    f.dwFlags|=DDSD_PIXELFORMAT; f.ddpfPixelFormat=(DDPIXELFORMAT){.dwSize=sizeof(DDPIXELFORMAT),.dwFlags=DDPF_FOURCC,.dwFourCC=0xdeadbeef};
    run(api,10,&f,DD_OK,0); MUST(!allocations&&!owner.surfaces);
    want_null=0;
}
static unsigned mode_count, wanted_depth;
static HRESULT mode2(DDSURFACEDESC2 *d,void *context) { MUST(context==(void*)123&&d->ddpfPixelFormat.dwRGBBitCount==wanted_depth&&d->lPitch==(LONG)surface_pitch_for(d->dwWidth,wanted_depth/8)); mode_count++; return DDENUMRET_OK; }
static HRESULT mode1(DDSURFACEDESC *d,void *c) { DDSURFACEDESC2 d2; desc1_to_desc2(d,&d2); return mode2(&d2,c); }
static void display_modes(void) {
    for(unsigned api=1;api<=7;api++){if(api==5||api==6)continue;
        for(unsigned bpp=8;bpp<=32;bpp*=2){DDSURFACEDESC2 f={.dwSize=sizeof(f),.dwFlags=DDSD_PIXELFORMAT};mode_pixel_format(bpp,&f.ddpfPixelFormat);wanted_depth=bpp;mode_count=0;DDSURFACEDESC old;desc2_to_desc1(&f,&old);HRESULT hr;
            if(api==1)hr=api1_EnumDisplayModes(NULL,0,&old,(void*)123,mode1);
            else if(api==2)hr=api2_EnumDisplayModes(NULL,0,&old,(void*)123,mode1);
            else if(api==3)hr=api3_EnumDisplayModes(NULL,0,&old,(void*)123,mode1);
            else if(api==4)hr=api4_EnumDisplayModes(NULL,0,&f,(void*)123,mode2);
            else hr=ddraw_EnumDisplayModes(NULL,0,&f,(void*)123,mode2);
            MUST(hr==DD_OK&&mode_count==10);
        }
    }
}
int main(void) { const unsigned apis[]={1,2,3,4,7}; for(unsigned round=0;round<8;round++) for(unsigned n=0;n<5;n++){existing(apis[n]);creatable(apis[n]);}display_modes();printf("PASS actual SDK surface enumeration C: %u checks, 5 version adapters, native flag matrix, exact caps and dimensional filters, metadata-only creation, display format filtering, cancellation, OOM and reentrant release/create lifetime\n",checks);return 0; }
`;
try {
    const sdkFile = dir + "/sdk.c";
    fs.writeFileSync(sdkFile, '#define COBJMACROS\n#include <ddraw.h>\n#include <stddef.h>\n#include "' + root + '/ddrawproxy/ddraw_protocol.h"\n#include "' + root + '/runtime/d9wg_format_layouts.h"\n#undef ZeroMemory\n#define ZeroMemory(p,n) memset(p,0,n)\n#undef CopyMemory\n#define CopyMemory(d,s,n) memcpy(d,s,n)\n#undef WINAPI\n#define WINAPI\n#define DDWG_TRACE_ERROR(h) (h)\n#define DDWG_TRACE_RESULT(h) (h)\n#define DDWG_TRACE_CHECKPOINT(...)\n#define DD_VERSION_1 1\n#define DD_VERSION_2 2\n#define DD_VERSION_3 3\n#define DD_VERSION_4 4\n#define DD_VERSION_7 7\nstatic int stage6_begin;\n' + stubs + modes + functions + engine + adapters + modeAdapters + tests + '\nstatic int stage6_end;');
    const pp=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",["-E","-P",sdkFile],{encoding:"utf8",maxBuffer:16<<20});assert.equal(pp.status,0,pp.stderr);
    function type(name){const end=new RegExp("}\\s*"+name+"[ ,;]").exec(pp.stdout);assert(end,name);return pp.stdout.slice(pp.stdout.lastIndexOf("typedef struct",end.index),pp.stdout.indexOf(";",end.index)+1);}
    const types=["DDSCAPS","DDSCAPS2","DDCOLORKEY","DDPIXELFORMAT","DDSURFACEDESC","DDSURFACEDESC2"].map(type).join("\n");
    const body=pp.stdout.split("static int stage6_begin;")[1].split("static int stage6_end;")[0];
    const program='#include <stdint.h>\n#include <stddef.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include "'+root+'/ddrawproxy/ddraw_protocol.h"\n#include "'+root+'/runtime/d9wg_format_layouts.h"\ntypedef uint32_t DWORD,UINT,ULONG;typedef uint16_t WORD;typedef int32_t LONG,HRESULT,BOOL;typedef void*LPVOID;typedef size_t SIZE_T;typedef void IDirectDraw,IDirectDraw7,IDirectDrawSurface,IDirectDrawSurface4,IDirectDrawSurface7;\n'+types+'\ntypedef DDSURFACEDESC2*LPDDSURFACEDESC2;typedef HRESULT(*SurfaceCB1)(IDirectDrawSurface*,DDSURFACEDESC*,void*);typedef HRESULT(*SurfaceCB4)(IDirectDrawSurface4*,DDSURFACEDESC2*,void*);typedef HRESULT(*LPDDENUMSURFACESCALLBACK7)(IDirectDrawSurface7*,DDSURFACEDESC2*,void*);typedef HRESULT(*ModeCB1)(DDSURFACEDESC*,void*);typedef HRESULT(*ModeCB2)(DDSURFACEDESC2*,void*);typedef ModeCB2 LPDDENUMMODESCALLBACK2;\n'+body;
    fs.writeFileSync(dir+"/test.c",program);
    const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer","-ferror-limit=3",dir+"/test.c","-o",dir+"/test"],{encoding:"utf8"});assert.equal(build.status,0,build.stderr.split("\n").map(l=>l.slice(0,240)).join("\n"));
    const run=spawnSync(dir+"/test",[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);
} finally { fs.rmSync(dir,{recursive:true,force:true}); }
