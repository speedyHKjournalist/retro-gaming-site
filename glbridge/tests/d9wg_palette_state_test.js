"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),header=process.env.D9WG_PALETTE_HEADER||path.join(root,"runtime/d9wg_palette_state.h");
function extract(source,name){const m=new RegExp("static (?:HRESULT WINAPI|BOOL) "+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let at=source.indexOf("{",m.index)+1,depth=1;for(;depth;++at){assert(at<source.length);if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return source.slice(m.index,at);}
const directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-palettes-"));
try{for(const api of [8,9]){
 const source=readComparisonSource(process.env["D"+api+"WG_PALETTE_SOURCE"]||path.join(root,"d3d"+api+"proxy/d3d"+api+"_proxy.c"),"utf8");
 const funcs=api===8?["device_set_palette","device_get_palette","device_set_current_palette","device_get_current_palette"]:["device_set_palette_entries","device_get_palette_entries","state_block_record_palette","device_set_current_texture_palette","device_get_current_texture_palette"];
 const names=api===8?funcs:[funcs[0],funcs[1],funcs[3],funcs[4]];
 const code=`#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "${path.join(root,'d3d9proxy/d3d9_protocol.h')}"
typedef int BOOL,HRESULT;typedef uint32_t UINT,DWORD;typedef uint8_t BYTE;
typedef struct PALETTEENTRY {BYTE peRed,peGreen,peBlue,peFlags;} PALETTEENTRY;
static unsigned allocations,frees,fail_at,attempts;
static void *allocate(size_t n){attempts++;if(fail_at&&attempts==fail_at)return NULL;allocations++;return calloc(1,n);}
static void release(void *p){frees++;free(p);}
#define D9WG_PALETTE_ALLOC(n) allocate(n)
#define D9WG_PALETTE_FREE(p) release(p)
#include "${header}"
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define E_OUTOFMEMORY (-3)
#define D8WG_TRACE_ERROR(x) (x)
#define TRACE_REFUSE(x) (x)
#define PURE_DEVICE_REFUSE(...) ((void)0)
#define D8WG_MAX_PALETTES D9WG_PALETTE_COUNT
#define D9_MAX_PALETTES D9WG_PALETTE_COUNT
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
#define EnterCriticalSection(p) ((void)(p))
#define LeaveCriticalSection(p) ((void)(p))
static int g_transport_lock;
typedef struct Snapshot {BOOL palette_mask;UINT current_palette;} Snapshot;
typedef struct Block {Snapshot state;BOOL has_palette;UINT current_palette;} Block;
typedef struct Device {D9WGPaletteStore palettes;Block *recording_state_block,*recording_result;UINT current_palette;uint32_t handle;} Device;
typedef Device D${api}Device,IDirect3DDevice${api};
static Device *device_from_iface(Device *p){return p;}
static unsigned emissions;static uint8_t buffer[4096];static D9WGSetCurrentTexturePalette selection;
static uint8_t *batch_base(void){return buffer;}
static BOOL reserve_command_locked(uint32_t op,uint32_t size,uint32_t blob,void *unused,uint8_t **payload,uint8_t **data){(void)unused;if(op!=D9WG_OP_SET_PALETTE||size!=sizeof(D9WGSetPalette)||blob!=1024)abort();emissions++;*payload=buffer;*data=buffer+sizeof(D9WGSetPalette);return TRUE;}
static BOOL emit_command(uint32_t op,const void *p,uint32_t size){if(op!=D9WG_OP_SET_CURRENT_TEXTURE_PALETTE||size!=sizeof(selection))abort();memcpy(&selection,p,size);emissions++;return TRUE;}
${funcs.map(n=>extract(source,n)).join('\n')}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"palette API ${api} line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
int main(void){Device device={0};Block block={0};PALETTEENTRY input[256],output[256];for(unsigned i=0;i<256;++i)input[i]=(PALETTEENTRY){i,255-i,i^85,i^170};
 UINT current=999;CHECK(${names[3]}(&device,&current)==0&&current==0);
 const UINT keys[]={0,15,16,255,256,40000,65535};
 for(unsigned k=0;k<7;++k){memset(output,0,sizeof(output));CHECK(${names[1]}(&device,keys[k],output)==0);for(unsigned b=0;b<sizeof(output);++b)CHECK(((BYTE*)output)[b]==255);CHECK(!allocations);}
 CHECK(${names[2]}(&device,65535)==0);CHECK(device.current_palette==65535&&selection.palette_index==65535);
 unsigned sent=emissions;device.${api===8?'recording_state_block':'recording_result'}=&block;
 CHECK(${names[2]}(&device,65535)==0);CHECK(${names[2]}(&device,256)==0);CHECK(device.current_palette==65535&&emissions==sent);CHECK(block.${api===8?'state.palette_mask':'has_palette'}&&block.${api===8?'state.current_palette':'current_palette'}==256);
 CHECK(${names[2]}(&device,65536)==-1);CHECK(block.${api===8?'state.current_palette':'current_palette'}==256);
 CHECK(${names[0]}(&device,256,input)==0);CHECK(emissions==sent+1);CHECK(${names[1]}(&device,256,output)==0&&!memcmp(input,output,sizeof(input)));const uint32_t *wire=(const uint32_t*)(buffer+sizeof(D9WGSetPalette));for(unsigned i=0;i<256;++i)CHECK(wire[i]==((uint32_t)(i^170)<<24|i<<16|(255-i)<<8|(i^85)));
 device.${api===8?'recording_state_block':'recording_result'}=NULL;
 CHECK(${names[0]}(&device,65536,input)==-1&&${names[0]}(&device,0,NULL)==-1);memset(output,0xcc,sizeof(output));CHECK(${names[1]}(&device,65536,output)==-1);for(unsigned i=0;i<sizeof(output);++i)CHECK(((BYTE*)output)[i]==0xcc);CHECK(${names[3]}(&device,NULL)==-1);
 /* Failed page/palette allocations leave even the sparse topology untouched. */
 for(unsigned which=1;which<=2;++which){D9WGPaletteStore empty={0};fail_at=attempts+which;CHECK(!d9wg_palette_set(&empty,65535,input));CHECK(!empty.pages[255]&&!d9wg_palette_find(&empty,65535));d9wg_palette_free(&empty);fail_at=0;}
 CHECK(device.palettes.pages[1]);fail_at=attempts+1;CHECK(!d9wg_palette_set(&device.palettes,257,input));CHECK(!d9wg_palette_find(&device.palettes,257));CHECK(!memcmp(d9wg_palette_find(&device.palettes,256),wire,1024));fail_at=0;
 for(uint32_t index=0;index<65536;++index){input[0].peRed=(BYTE)index;input[0].peGreen=(BYTE)(index>>8);CHECK(d9wg_palette_set(&device.palettes,index,input));}
 for(uint32_t index=0;index<65536;++index){d9wg_palette_get(&device.palettes,index,output);CHECK(output[0].peRed==(BYTE)index&&output[0].peGreen==(BYTE)(index>>8)&&output[255].peFlags==(255^170));}
 unsigned count=allocations;input[0].peRed=17;CHECK(d9wg_palette_set(&device.palettes,65535,input)&&allocations==count);d9wg_palette_free(&device.palettes);CHECK(allocations==frees);d9wg_palette_free(&device.palettes);CHECK(allocations==frees);return 0;}
`;
 const file=path.join(directory,"palette"+api+".c"),binary=path.join(directory,"palette"+api);fs.writeFileSync(file,code);const built=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",file,"-o",binary],{encoding:"utf8"});assert.equal(built.status,0,built.stderr||String(built.error));const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr||String(run.error));
 }console.log("PASS shared palettes and actual D3D8/9 setters: all 65536 IDs, default white, RGBA packing, deferred selection, live resource contents, allocation failure atomicity and complete release");
}finally{fs.rmSync(directory,{recursive:true,force:true});}
