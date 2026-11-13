'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(process.env.DDRAW_DESC_POLICY_SOURCE||root+'/ddrawproxy/ddraw_proxy.c','utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-desc-policy-'));
function extract(name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let end=source.indexOf('{',m.index)+1,d=1;for(;d;end++){if(source[end]==='{')d++;if(source[end]==='}')d--;}return source.slice(m.index,end);}
const memoryNative=JSON.parse(fs.readFileSync(root+'/../../v86/tests/glbridge/fixtures/ddraw_native_memory_descriptors.json'));
assert.equal(require('node:crypto').createHash('sha256').update(fs.readFileSync(root+'/../'+memoryNative.source)).digest('hex'),memoryNative.sourceSha256);
const capsInputs=[[0,0,0,0],[0,0xdeadbeef,0,0],[0,0,0xdeadbeef,0],[0,0,0,0xdeadbeef],[0,0xffffffff,0xffffffff,0xffffffff],[0x800,0,0,0],[0x800,0xdeadbeef,0,0]];
const definitions=fs.readFileSync(__dirname+'/ddraw_color_key_test.js','utf8').split('typedef uint32_t DWORD,UINT;')[1].split('typedef struct DDSurface DDSurface;')[0].replace('dwCaps,dwCaps2;', 'dwCaps,dwCaps2,dwCaps3,dwCaps4;');
const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t DWORD,UINT;${definitions}
typedef size_t SIZE_T;typedef int32_t LONG;typedef unsigned char BYTE;
#ifndef DDERR_INVALIDSURFACETYPE
#define DDERR_INVALIDSURFACETYPE (-28)
#endif
#ifndef DDERR_SURFACEBUSY
#define DDERR_SURFACEBUSY (-29)
#endif
#ifndef DDERR_OUTOFMEMORY
#define DDERR_OUTOFMEMORY (-30)
#endif
typedef struct{BYTE*bits;BOOL gpu_dirty;}DDSurfaceMemory;
typedef struct{unsigned ref;DWORD handle;}DDTextureStorage;
typedef struct DDSurface{DDSURFACEDESC2 desc;unsigned lock_depth,width,height,format,bytes_per_pixel,pitch;void*dc;BOOL implicit,indexed,external_memory,upload_pending;struct DDSurface*mip_next,*cube_faces[6];DDSurfaceMemory*memory;DDTextureStorage*storage;BYTE*shadow;DWORD handle;}DDSurface,IDirectDrawSurface7;
typedef struct{unsigned format;}DDrawPixelFormatDesc;
static unsigned checks,allocated,released;static BOOL rejecting;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"desc policy line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
static DDSurface*surface_from_iface(DDSurface*p){return p;}
static void describe_pixel_format(DDPIXELFORMAT*p,DDrawPixelFormatDesc*d){d->format=p->dwRGBBitCount==32?22:0;}
static unsigned ddraw_pixel_format_to_d3dformat(DDrawPixelFormatDesc*d){return d->format;}
static UINT format_bytes_per_pixel(unsigned f){return f==22?4:0;}
static UINT format_compressed_block_bytes(unsigned f){(void)f;return 0;}
static UINT surface_pitch_for(UINT w,UINT p){return w*p;}
static DDSurfaceMemory*surface_memory_create(BYTE*p,SIZE_T bytes,BOOL own){MUST(!rejecting&&p&&bytes&&!own);DDSurfaceMemory*m=calloc(1,sizeof(*m));MUST(m);m->bits=p;allocated++;return m;}
static void surface_memory_release(DDSurfaceMemory*p){if(p){released++;free(p);}}
static DDTextureStorage*surface_storage_allocate(void){MUST(!rejecting);DDTextureStorage*p=calloc(1,sizeof(*p));MUST(p);p->ref=1;p->handle=123;return p;}
static void surface_storage_release(DDTextureStorage*p){free(p);}
static BOOL ddraw_format_is_palettised(unsigned f){(void)f;return FALSE;}
static BOOL emit_surface_create(DDSurface*p){MUST(!rejecting&&p);return TRUE;}
static BOOL emit_surface_upload(DDSurface*p,void*r){MUST(!rejecting&&p&&!r);return TRUE;}
static void rebind_host_surface_state(DDSurface*p){MUST(!rejecting&&p);}
${extract('surface_SetSurfaceDesc')}
static const struct{DWORD caps[4],hr;}native_caps[]={NATIVE_CAPS};
static void native_caps_tail(void){
 BYTE bits[256]={0};DDSurfaceMemory*memory=calloc(1,sizeof(*memory));DDTextureStorage*storage=calloc(1,sizeof(*storage));MUST(memory&&storage);memory->bits=bits;storage->ref=1;
 DDSurface s={.width=8,.height=8,.format=22,.memory=memory,.storage=storage,.desc={.dwSize=sizeof(DDSURFACEDESC2),.ddsCaps={.dwCaps=DDSCAPS_SYSTEMMEMORY|DDSCAPS_OFFSCREENPLAIN},.ddpfPixelFormat={.dwSize=sizeof(DDPIXELFORMAT),.dwRGBBitCount=32}}};
 rejecting=FALSE;
 for(unsigned i=0;i<sizeof(native_caps)/sizeof(*native_caps);i++){
  DDSURFACEDESC2 r={.dwSize=sizeof(r),.dwFlags=DDSD_LPSURFACE|DDSD_CAPS,.lpSurface=bits,.ddsCaps={.dwCaps=native_caps[i].caps[0],.dwCaps2=native_caps[i].caps[1],.dwCaps3=native_caps[i].caps[2],.dwCaps4=native_caps[i].caps[3]}},before=r;DDSurface saved=s;unsigned a=allocated,d=released;
  HRESULT expected=native_caps[i].hr?DDERR_INVALIDCAPS:DD_OK;MUST(surface_SetSurfaceDesc(&s,&r,0)==expected&&!memcmp(&r,&before,sizeof(r)));
  if(expected)MUST(!memcmp(&s,&saved,sizeof(s))&&allocated==a&&released==d);
  else MUST(s.shadow==bits&&s.desc.lpSurface==bits&&s.desc.ddsCaps.dwCaps==(DDSCAPS_SYSTEMMEMORY|DDSCAPS_OFFSCREENPLAIN)&&!s.desc.ddsCaps.dwCaps2&&!s.desc.ddsCaps.dwCaps3&&!s.desc.ddsCaps.dwCaps4&&allocated==a+1&&released==d+1);
 }
 surface_memory_release(s.memory);surface_storage_release(s.storage);
}
int main(void){BYTE old_bits[256]={0},new_bits[1024]={0};DWORD managed[]={DDSCAPS2_TEXTUREMANAGE,DDSCAPS2_D3DTEXTUREMANAGE};DWORD flags[]={DDSD_LPSURFACE,DDSD_WIDTH,DDSD_WIDTH|DDSD_HEIGHT,DDSD_LPSURFACE|DDSD_WIDTH|DDSD_PITCH,0,0xffffffffu};
 for(unsigned i=0;i<2;i++)for(unsigned j=0;j<6;j++){DDSurface s={.width=8,.height=8,.format=22,.desc={.dwSize=sizeof(DDSURFACEDESC2),.ddsCaps={.dwCaps=DDSCAPS_SYSTEMMEMORY|DDSCAPS_TEXTURE,.dwCaps2=managed[i]}}},before=s;DDSURFACEDESC2 request={.dwSize=sizeof(request),.dwFlags=flags[j],.dwWidth=8,.dwHeight=8,.lPitch=32,.lpSurface=new_bits},saved=request;rejecting=TRUE;MUST(surface_SetSurfaceDesc(&s,&request,0)==DDERR_INVALIDSURFACETYPE&&!memcmp(&s,&before,sizeof(s))&&!memcmp(&request,&saved,sizeof(request))&&!allocated&&!released);}
 rejecting=FALSE;DDSurfaceMemory*old=calloc(1,sizeof(*old));MUST(old);old->bits=old_bits;DDTextureStorage*storage=calloc(1,sizeof(*storage));MUST(storage);storage->ref=1;DDSurface ordinary={.width=8,.height=8,.format=22,.memory=old,.storage=storage,.desc={.dwSize=sizeof(DDSURFACEDESC2),.ddsCaps={.dwCaps=DDSCAPS_SYSTEMMEMORY|DDSCAPS_OFFSCREENPLAIN},.ddpfPixelFormat={.dwSize=sizeof(DDPIXELFORMAT),.dwRGBBitCount=32}}};DDSURFACEDESC2 request={.dwSize=sizeof(request),.dwFlags=DDSD_LPSURFACE|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PITCH,.dwWidth=16,.dwHeight=16,.lPitch=64,.lpSurface=new_bits};MUST(!surface_SetSurfaceDesc(&ordinary,&request,0)&&ordinary.memory->bits==new_bits&&ordinary.width==16&&ordinary.height==16&&ordinary.pitch==64&&ordinary.external_memory&&allocated==1&&released==1);surface_memory_release(ordinary.memory);surface_storage_release(ordinary.storage);
 native_caps_tail();
 printf("PASS actual SetSurfaceDesc policy C: %u checks; two managed types reject before metadata/memory changes, invalid field combinations preserve outputs, ordinary client-memory replacement still succeeds\\n",checks);return 0;}
`.replace('NATIVE_CAPS',capsInputs.map((caps,i)=>{const row=memoryNative.results.find(r=>r.name==='caps'+i);assert(row&&[0,0x88760064].includes(row.hr));return '{{'+caps.join(',')+'},'+row.hr+'u}';}).join(','));
try{fs.writeFileSync(dir+'/test.c',code);const b=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
