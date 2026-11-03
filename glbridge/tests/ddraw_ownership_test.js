"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), current = fs.readFileSync(root + "/ddrawproxy/ddraw_proxy.c", "utf8"), source = fs.readFileSync(process.env.DDRAW_OWNERSHIP_SOURCE || root + "/ddrawproxy/ddraw_proxy.c", "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "ddraw-ownership-"));
function extract(name) {const re = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{"); let input = source, m = re.exec(input); if (!m) {input = current; m = re.exec(input);} assert(m, name); let end = input.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (input[end] === "{") depth++; if (input[end] === "}") depth--;} return input.slice(m.index, end);}
const definitions = fs.readFileSync(__dirname + "/ddraw_color_key_test.js", "utf8").split("typedef uint32_t DWORD,UINT;")[1].split("typedef struct DDSurface DDSurface;")[0].replace("typedef struct{DWORD dwSize,dwFlags,dwWidth", "typedef struct{void*lpSurface;DWORD dwSize,dwFlags,dwWidth");
const code = `#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "${root}/runtime/d9wg_ddraw_color_key.h"
#include "d3d9proxy/d3d9_protocol.h"
typedef uint32_t DWORD,UINT;${definitions}
typedef int32_t LONG;typedef uint32_t ULONG;typedef uint8_t BYTE;typedef void*HDC;typedef void*HBITMAP;
#define DD_VERSION_4 4u
#define DD_VERSION_7 7u
#define DDPCAPS_8BIT 4u
#define DDERR_OUTOFMEMORY (-8)
#define DDERR_INVALIDOBJECT (-9)
#define DDERR_CANTDUPLICATE (-10)
#define DDERR_SURFACEBUSY (-11)
#define DDERR_CANNOTATTACHSURFACE (-14)
#define DDERR_CANNOTDETACHSURFACE (-12)
#define DDERR_SURFACENOTATTACHED (-13)
#define DDSCAPS_FRONTBUFFER 0x20u
#define DDSCAPS_OVERLAY 0x80u
#define DDSCAPS_ZBUFFER 0x20000u
#define DDSCAPS_3DDEVICE 0x2000u
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory(d,s,n) memcpy(d,s,n)
typedef struct {BYTE peRed,peGreen,peBlue,peFlags;} PALETTEENTRY,*LPPALETTEENTRY;
typedef struct DDrawObject DDrawObject;typedef struct DDSurface DDSurface;typedef struct DDPalette DDPalette;typedef struct DDClipper DDClipper;
struct DDrawObject {void*vtbl7;LONG ref,lifetime_ref;DWORD device_handle,guest_mode_changed,mode_bpp,next_palette_slot;DDSurface*surfaces,*primary;DDPalette*palettes;DDClipper*clippers;DDrawObject*next;};
struct DDPalette {void*vtbl;LONG ref;DDrawObject*owner;BOOL owner_public_ref;DWORD caps,slot;PALETTEENTRY entries[256];DDPalette*next;};
struct DDClipper {void*vtbl;LONG ref;DDrawObject*owner;void*clip_list;DDClipper*next;};
typedef struct {LONG ref;DWORD handle;} DDTextureStorage;typedef struct {LONG ref;BYTE bits[4];} DDSurfaceMemory;
struct DDSurface {void*vtbl7;LONG ref;DDrawObject*owner;BOOL owner_public_ref,primary,front_of_chain,owns_flip_chain,implicit,overlay_visible,external_memory;DWORD handle,lock_depth;UINT width,height,format,mip_level,cube_face;DDSURFACEDESC2 desc;DDSurface*next,*flip_next,*mip_next,*cube_faces[6],*attached_depth,*overlay_destination;DDPalette*palette;DDClipper*clipper;DDTextureStorage*storage;DDSurfaceMemory*memory;BYTE*shadow;void*dc,*dib,*old_bitmap;DWORD color_key_present,texture_handle;DDCOLORKEY color_key[4];};
typedef DDrawObject IDirectDraw7;typedef DDSurface IDirectDrawSurface7,*LPDIRECTDRAWSURFACE7;typedef DDPalette IDirectDrawPalette,*LPDIRECTDRAWPALETTE;typedef DDClipper IDirectDrawClipper,*LPDIRECTDRAWCLIPPER;typedef struct {unsigned unused;} DDrawPixelFormatDesc;
static unsigned checks,objects_freed,surfaces_freed,textures_freed,memories_freed,creates,fail_create,device_destroys,hidden;static int g_object_lock,g_transport_lock,g_palette_vtbl,g_clipper_vtbl;static LONG g_object_count;static DDrawObject*g_objects;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"DDraw ownership line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
struct Allocation {void*p;size_t bytes;int kind,dead;};static struct Allocation allocations[20000];static unsigned allocated;
static void*allocation(size_t bytes,int kind){MUST(allocated<20000);void*p=calloc(1,bytes);MUST(p);allocations[allocated++]=(struct Allocation){p,bytes,kind,0};return p;}
static void alive(const void*p){for(unsigned i=0;i<allocated;i++)if((uintptr_t)p>=(uintptr_t)allocations[i].p&&(uintptr_t)p<(uintptr_t)allocations[i].p+allocations[i].bytes)MUST(!allocations[i].dead);}
static LONG InterlockedIncrement(LONG*p){alive(p);return ++*p;}static LONG InterlockedDecrement(LONG*p){alive(p);return --*p;}
static void*heap_alloc_zero(size_t n){return allocation(n,0);}static void heap_free(void*p){if(!p)return;for(unsigned i=0;i<allocated;i++)if(allocations[i].p==p){MUST(!allocations[i].dead);if(allocations[i].kind==1){DDrawObject*d=p;MUST(!d->surfaces&&!d->palettes&&!d->clippers&&!d->lifetime_ref&&!d->ref);objects_freed++;}if(allocations[i].kind==2)surfaces_freed++;allocations[i].dead=1;return;}MUST(0);}
static void EnterCriticalSection(void*p){(void)p;}static void LeaveCriticalSection(void*p){(void)p;}
static void unwatch_object_window(DDrawObject*d){alive(d);}static void ChangeDisplaySettingsA(void*p,unsigned n){(void)p;(void)n;}
static void emit_device_destroy(DDrawObject*d){alive(d);device_destroys++;}static void submit_batch_locked(BOOL wait){(void)wait;}
static void emit_window_state_not_showing(DDrawObject*d){alive(d);hidden++;}static void SelectObject(void*p,void*q){(void)p;(void)q;}static void DeleteObject(void*p){(void)p;}static void DeleteDC(void*p){(void)p;}
static void emit_overlay_state(DDSurface*s){alive(s);}static void emit_palette_table(DDPalette*p){alive(p);}static void emit_surface_palette_binding(DDSurface*s){alive(s);}
static DDrawObject*object_from_iface(IDirectDraw7*p){return p;}static DDrawObject*object_from_any(IDirectDraw7*p,void*v){(void)v;return p;}static DDSurface*surface_from_iface(DDSurface*p){return p;}static DDSurface*surface_from_any(DDSurface*p,void*v){(void)v;return p;}static DDPalette*palette_from_iface(DDPalette*p){return p;}static DDClipper*clipper_from_iface(DDClipper*p){return p;}
static ULONG WINAPI ddraw_AddRef(IDirectDraw7*);static ULONG WINAPI ddraw_Release(IDirectDraw7*);static void ddraw_lifetime_release(DDrawObject*);static void surface_destroy(DDSurface*);static ULONG WINAPI surface_Release(IDirectDrawSurface7*);
${["ddraw_child_attach", "ddraw_child_detach", "ddraw_AddRef", "ddraw_lifetime_release", "ddraw_Release", "palette_AddRef", "palette_Release", "clipper_AddRef", "clipper_Release"].map(extract).join("\n")}
#define IDirectDrawPalette_AddRef palette_AddRef
#define IDirectDrawPalette_Release palette_Release
#define IDirectDrawClipper_AddRef clipper_AddRef
#define IDirectDrawClipper_Release clipper_Release
static void surface_storage_release(DDTextureStorage*p){if(p&&!--p->ref){textures_freed++;heap_free(p);}}static void surface_memory_release(DDSurfaceMemory*p){if(p&&!--p->ref){memories_freed++;heap_free(p);}}static void surface_memory_addref(DDSurfaceMemory*p){p->ref++;}
#define IDirectDrawSurface7_AddRef surface_AddRef
#define IDirectDrawSurface7_Release surface_Release
static void d3d7_refresh_render_target(DDrawObject*d,DDSurface*s){alive(d);alive(s);}
${["surface_AddRef", "surface_Release", "surface_destroy", "surface_AddAttachedSurface", "surface_DeleteAttachedSurface", "surface_SetPalette", "surface_SetClipper", "ddraw_CreatePaletteForVersion", "ddraw_CreatePalette", "ddraw_CreateClipper"].map(extract).join("\n")}
static DDSurface*new_surface(DDrawObject*d,DDTextureStorage*shared){DDSurface*s=allocation(sizeof(*s),2);s->ref=1;s->owner=d;s->next=d->surfaces;d->surfaces=s;if(shared){s->storage=shared;shared->ref++;s->implicit=1;}else{s->storage=allocation(sizeof(*s->storage),0);s->storage->ref=1;}s->memory=allocation(sizeof(*s->memory),0);s->memory->ref=1;s->shadow=s->memory->bits;ddraw_child_attach(d,FALSE);return s;}
static void object_display_size(DDrawObject*d,UINT*w,UINT*h){(void)d;*w=*h=32;}static void mode_pixel_format(UINT bits,DDPIXELFORMAT*p){*p=(DDPIXELFORMAT){.dwSize=sizeof(*p),.dwRGBBitCount=bits};}static void describe_pixel_format(const DDPIXELFORMAT*p,DDrawPixelFormatDesc*d){(void)p;d->unused=0;}static unsigned ddraw_pixel_format_to_d3dformat(const DDrawPixelFormatDesc*d){(void)d;return 22;}static void watch_object_window(DDrawObject*d){alive(d);}static BOOL seed_windowed_primary_from_screen(DDSurface*s){alive(s);return 1;}
static DDSurface*surface_create(DDrawObject*d,const DDSURFACEDESC2*r,unsigned f,UINT w,UINT h,DWORD caps){if(fail_create&&++creates==fail_create)return NULL;DDSurface*s=new_surface(d,NULL);s->desc=*r;s->desc.ddsCaps.dwCaps=caps;s->format=f;s->width=w;s->height=h;s->primary=(caps&DDSCAPS_PRIMARYSURFACE)!=0;return s;}
static DDSurface*surface_create_complex_texture(DDrawObject*d,const DDSURFACEDESC2*r,unsigned f,UINT w,UINT h,DWORD caps,UINT levels,BOOL cube){(void)levels;(void)cube;return surface_create(d,r,f,w,h,caps);}
static DDSurface*surface_allocate(DDrawObject*d,const DDSURFACEDESC2*r,unsigned f,UINT w,UINT h,DWORD caps,DDTextureStorage*shared,UINT mip,UINT face){DDSurface*s=new_surface(d,shared);s->desc=*r;s->format=f;s->width=w;s->height=h;s->desc.ddsCaps.dwCaps=caps;s->mip_level=mip;s->cube_face=face;return s;}
${["surface_request_color_keys_valid", "surface_full_mip_count", "ddraw_CreateSurfaceForVersion", "ddraw_CreateSurface", "ddraw_DuplicateSurfaceForVersion", "ddraw_DuplicateSurface"].map(extract).join("\n")}
static DDrawObject*owner(void){DDrawObject*d=allocation(sizeof(*d),1);d->ref=d->lifetime_ref=1;d->mode_bpp=32;d->next=g_objects;g_objects=d;g_object_count++;return d;}
static DDSURFACEDESC2 description(void){DDSURFACEDESC2 r={0};r.dwSize=sizeof(r);r.dwFlags=DDSD_WIDTH|DDSD_HEIGHT|DDSD_CAPS;r.dwWidth=r.dwHeight=32;r.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN;return r;}
static void version_cases(unsigned version){DDrawObject*d=owner();DDSurface*s=NULL,*dup=NULL;DDPalette*p=NULL;DDClipper*c=NULL;PALETTEENTRY entries[256]={{0}};DDSURFACEDESC2 r=description();unsigned pin=version>=4;
 MUST(!ddraw_CreateSurfaceForVersion(d,&r,&s,NULL,version));MUST(d->ref==1+(LONG)pin&&d->lifetime_ref==2+(LONG)pin&&s->owner_public_ref==(BOOL)pin);MUST(!ddraw_CreatePaletteForVersion(d,DDPCAPS_8BIT,entries,&p,NULL,version));MUST(d->ref==1+2*(LONG)pin);MUST(!ddraw_CreateClipper(d,0,&c,NULL)&&d->ref==1+2*(LONG)pin);MUST(!ddraw_DuplicateSurfaceForVersion(d,s,&dup,version)&&d->ref==1+3*(LONG)pin&&s->storage==dup->storage&&s->memory==dup->memory);
 MUST(!surface_SetPalette(s,p)&&p->ref==2);MUST(!surface_SetClipper(s,c)&&c->ref==2);MUST(palette_Release(p)==1&&clipper_Release(c)==1);for(unsigned i=0;i<64;i++){MUST(!surface_SetPalette(s,p)&&p->ref==1);MUST(!surface_SetClipper(s,c)&&c->ref==1);}unsigned freed=objects_freed;MUST(ddraw_Release(d)==3*pin&&objects_freed==freed);MUST(surface_AddRef(s)==2&&surface_Release(s)==1);MUST(!surface_Release(dup)&&objects_freed==freed);MUST(!surface_Release(s)&&objects_freed==freed+1);}
static void complex_cases(unsigned count){DDrawObject*d=owner();DDSurface*root=new_surface(d,NULL),*prev=root;root->owner_public_ref=1;ddraw_AddRef(d);root->owns_flip_chain=1;root->primary=1;d->primary=root;for(unsigned i=0;i<count;i++){DDSurface*child=new_surface(d,NULL);child->implicit=1;prev->flip_next=child;prev=child;if(i&1){MUST(surface_Release(child)==0);MUST(surface_AddRef(child)==1);}else surface_AddRef(child);}prev->flip_next=root;
 DDSurface*mip=new_surface(d,root->storage);root->mip_next=mip;surface_AddRef(mip);MUST(surface_DeleteAttachedSurface(root,0,mip)==DDERR_CANNOTDETACHSURFACE&&root->mip_next==mip);DDSurface*face=new_surface(d,root->storage);root->cube_faces[1]=face;face->mip_next=new_surface(d,root->storage);surface_AddRef(face);unsigned s=surfaces_freed,t=textures_freed,m=memories_freed,o=objects_freed;MUST(ddraw_Release(d)==1);MUST(!surface_Release(root));MUST(surfaces_freed==s+count+4&&textures_freed==t+count+1&&memories_freed==m+count+4&&objects_freed==o+1);}
static void soft_attachment(void){DDrawObject*d=owner();DDSurface*r=new_surface(d,NULL),*child=new_surface(d,NULL);child->desc.ddsCaps.dwCaps=DDSCAPS_BACKBUFFER;MUST(!surface_AddAttachedSurface(r,child)&&child->ref==2&&r->owns_flip_chain);unsigned o=objects_freed;MUST(!ddraw_Release(d));MUST(!surface_Release(r)&&child->ref==1&&!child->flip_next&&objects_freed==o);MUST(!surface_Release(child)&&objects_freed==o+1);}
static void failures(void){DDrawObject*d=owner();DDSURFACEDESC2 r=description();r.dwFlags|=DDSD_BACKBUFFERCOUNT;r.dwBackBufferCount=3;for(unsigned failure=1;failure<=4;failure++){DDSurface*out=(void*)1;creates=0;fail_create=failure;unsigned freed=surfaces_freed;MUST(ddraw_CreateSurfaceForVersion(d,&r,&out,NULL,7)==DDERR_OUTOFVIDEOMEMORY&&out==NULL);MUST(!d->surfaces&&d->ref==1&&d->lifetime_ref==1&&surfaces_freed==freed+failure-1);}fail_create=0;MUST(!ddraw_Release(d));}
int main(void){(void)&ddraw_child_detach;(void)&ddraw_CreatePalette;(void)&ddraw_CreateSurface;(void)&ddraw_DuplicateSurface;for(unsigned round=0;round<32;round++){version_cases(1);version_cases(2);version_cases(3);version_cases(4);version_cases(7);complex_cases(round%4+1);soft_attachment();}failures();MUST(!g_objects&&!g_object_count&&device_destroys==objects_freed);for(unsigned i=0;i<allocated;i++){MUST(allocations[i].dead);free(allocations[i].p);}printf("PASS actual DDraw ownership C: %u checks, 160 factory profiles, 10240 same-pointer bindings per kind, 32 complex chains, 32 soft attachments, implicit zero-count retention, complete storage/memory cleanup, DD1/2 private lifetime, DD4/7 public pins, four allocation failures and final-owner ordering\\n",checks);return 0;}
`;
try {const file = dir + "/ownership.c", binary = dir + "/ownership"; fs.writeFileSync(file, code); const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", "-I", root, file, "-o", binary], {encoding: "utf8"}); assert.equal(built.status, 0, built.stderr); const run = spawnSync(binary, [], {encoding: "utf8"}); assert.equal(run.status, 0, run.stderr); process.stdout.write(run.stdout);} finally {fs.rmSync(dir, {recursive: true, force: true});}
