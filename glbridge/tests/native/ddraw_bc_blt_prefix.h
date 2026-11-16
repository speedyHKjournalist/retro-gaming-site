#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t DWORD,UINT;typedef int32_t LONG;typedef uint16_t WORD;typedef uint8_t BYTE;typedef int BOOL;typedef size_t SIZE_T;
typedef struct{LONG left,top,right,bottom;}RECT;
typedef struct DDSurface DDSurface;
typedef struct{UINT device_handle;DDSurface*surfaces;}DDrawObject;
typedef struct{BYTE bits[1024],gpu[1024];SIZE_T bytes;BOOL gpu_dirty;DWORD content_version;}DDSurfaceMemory;
struct DDSurface{DDrawObject*owner;DDSurfaceMemory*memory;BYTE*shadow;UINT width,height,pitch,bytes_per_pixel,format,handle,mip_level,cube_face;BOOL upload_pending;void*clipper;struct{struct{DWORD dwCaps;}ddsCaps;}desc;DDSurface*next;};
#define TRUE 1
#define FALSE 0
#define DDSCAPS_SYSTEMMEMORY 0x800u
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
#define EnterCriticalSection(p) ((void)(p))
#define LeaveCriticalSection(p) ((void)(p))
#define InterlockedIncrement(p) (++*(p))
#define DDWG_TRACE_CHECKPOINT(...)
#define HOSTLOG_REFUSED(...)
static unsigned checks,reads,uploads,emits,allocations,frees,fail_allocation;
static BOOL fail_upload,fail_storage;static int g_object_lock;
static DDSurfaceMemory*fail_read;static DDSurface*upload_target;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"BC blit line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static void*heap_alloc_zero(SIZE_T n){allocations++;if(allocations==fail_allocation)return NULL;MUST(n<=4096);return calloc(1,n);}
static void heap_free(void*p){if(p)frees++;free(p);}
static BOOL surface_readback(DDSurface*s){if(!s->memory->gpu_dirty)return TRUE;reads++;if(s->memory==fail_read)return FALSE;memcpy(s->shadow,s->memory->gpu,s->memory->bytes);s->memory->gpu_dirty=FALSE;return TRUE;}
static BOOL ensure_surface_storage(DDSurface*s){MUST(s);return !fail_storage;}
static BOOL emit_texture_rows(const D9WGTextureRows*u){DDSurface*s=upload_target;uploads++;MUST(u->resource_handle==s->handle&&u->source_pitch==s->pitch&&u->block_height==4);MUST(u->x==0&&u->y==0&&u->width==s->width&&u->height==s->height);MUST(u->row_count==(s->height+3)/4&&u->row_bytes==(s->width+3)/4*(d9wg_bc_kind(s->format)==1?8u:16u));UINT rows=fail_upload?u->row_count/2:u->row_count;for(UINT y=0;y<rows;y++)memcpy(s->memory->gpu+y*s->pitch,u->source+y*u->source_pitch,u->row_bytes);return !fail_upload;}
static BOOL surface_can_memory_blt(DDSurface*d,DDSurface*s,uint32_t f){(void)d;(void)s;(void)f;return FALSE;}
static BOOL surface_memory_blt(DDSurface*d,const RECT*dr,DDSurface*s,const RECT*sr){(void)d;(void)dr;(void)s;(void)sr;MUST(0);return FALSE;}
static BOOL sync_external_surface_for_read(DDSurface*s){(void)s;return TRUE;}
static BOOL ensure_device(DDrawObject*d){return !!d;}
static BOOL surface_fill_yuv(DDSurface*s,const RECT*r,DWORD c){(void)s;(void)r;(void)c;MUST(0);return FALSE;}
static BOOL surface_can_mirror_color_fill(DDSurface*s){(void)s;return FALSE;}
static BOOL surface_prepare_color_fill(DDSurface*s,const RECT*r){(void)s;(void)r;MUST(0);return FALSE;}
static void surface_mirror_color_fill(DDSurface*s,const RECT*r,DWORD c,BOOL d){(void)s;(void)r;(void)c;(void)d;MUST(0);}
static void surface_mirror_depth_fill(DDSurface*s,const RECT*r,DWORD c,BOOL d){(void)s;(void)r;(void)c;(void)d;MUST(0);}
static BOOL emit_command(UINT op,const void*p,UINT n){(void)op;(void)p;(void)n;emits++;MUST(0);return FALSE;}
