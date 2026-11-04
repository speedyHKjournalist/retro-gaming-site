"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"declaration8-core-")),source=fs.readFileSync(process.env.D8WG_DECL_CORE_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8");
function extract(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return s.slice(m.index,b);}
let old=fs.readFileSync(root+'/tests/fixtures/d3d8_declaration_before_core.c','utf8');const defines=[...old.matchAll(/^#define (\w+)/gm)].map(m=>m[1]);for(const n of defines)old=old.replace(new RegExp('\\b'+n+'\\b','g'),'OR_'+n);old=old.replace(/\bd3d8_/g,'oracle_d3d8_').replace(/\bd3d8_(\w+)/g,'oracle_d3d8_$1').replace(/\bvalidate_vertex_declaration\b/g,'oracle_validate').replace(/\bdeclaration_to_elements\b/g,'oracle_elements');
const sdk=spawnSync(process.env.D9WG_CC||'i686-w64-mingw32-gcc',['-E','-P','-x','c','-'],{input:'#include <d3d9.h>\n',encoding:'utf8',maxBuffer:16<<20});assert.equal(sdk.status,0,sdk.stderr);const layout=/typedef struct _D3DVERTEXELEMENT9\s*\{[\s\S]*?\}\s*D3DVERTEXELEMENT9\s*(?:,\s*\*LPD3DVERTEXELEMENT9)?\s*;/.exec(sdk.stdout);assert(layout,'SDK9 vertex element');
try{const c=`#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include "${process.env.D8WG_DECLARATION8_HEADER||root+'/runtime/d9wg_declaration8.h'}"
typedef uint32_t DWORD,UINT;typedef uint16_t WORD;typedef uint8_t BYTE;typedef int32_t HRESULT;typedef int BOOL;
${layout[0]}
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define E_OUTOFMEMORY (-3)
#define TRUE 1
#define FALSE 0
#define WINAPI
#define D8WG_MAX_STREAMS 16
#define D3DVSD_TOKENTYPESHIFT 29
#define D3DVSD_TOKEN_END 7
#define D3DVSD_TOKEN_CONSTMEM 4
#define D3DVSD_CONSTCOUNTMASK (15u<<25)
#define D3DVSD_CONSTCOUNTSHIFT 25
#define D9WG_D3D8_SHADER_TOKENS 8192
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
#define D3DDECL_END() {0xff,0,17,0,0,0}
typedef struct{UINT api;}IDirect3DDevice9;
typedef struct{UINT marker;}IDirect3DVertexDeclaration9;
static IDirect3DVertexDeclaration9 object={91};static UINT calls;static HRESULT backend_result;static D3DVERTEXELEMENT9 captured[33];
static IDirect3DDevice9*device_from_iface(IDirect3DDevice9*d){return d;}
static UINT device_compatibility_api(IDirect3DDevice9*d){return d->api;}
static HRESULT make_decl(IDirect3DDevice9*d,const D3DVERTEXELEMENT9*e,IDirect3DVertexDeclaration9**out){if(d->api!=8)abort();calls++;UINT i=0;do{if(i>=33)abort();captured[i]=e[i];}while(e[i++].Stream!=255);if(backend_result)return backend_result;*out=&object;return 0;}
#define IDirect3DDevice9_CreateVertexDeclaration make_decl
${old}
${extract(source,'d9wg_d3d9_core_vertex_declaration8')}
static UINT cases,checks;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Declaration8 core case %u line %u: %s\\n",cases,__LINE__,#v);return 1;}checks++;}while(0)
int main(void){_Static_assert(sizeof(D3DVERTEXELEMENT9)==8&&sizeof(D9WGVertexElement)==8,"SDK9 wire layout");_Static_assert(offsetof(D3DVERTEXELEMENT9,UsageIndex)==7,"SDK9 element fields");IDirect3DDevice9 device={8};const UINT streams[]={0,3,15},skips[]={0,1,15};
for(UINT st=0;st<3;st++)for(UINT reg=0;reg<32;reg++)for(UINT type=0;type<16;type++)for(UINT sk=0;sk<3;sk++)for(UINT cc=0;cc<8;cc++)for(UINT shape=0;shape<5;shape++){
 DWORD tokens[96],before[96];UINT n=0;tokens[n++]=0x20000000u|streams[st];tokens[n++]=0x40000000u|(3u<<16);tokens[n++]=0x50000000u|(skips[sk]<<16);tokens[n++]=0x40000000u|(type<<16)|reg;tokens[n++]=0x40000007u;
 if(cc){UINT start=cc==2?95:cc==3?96:2,count=cc==4?0:cc==5?2:1;tokens[n++]=0x80000000u|(count<<25)|start;for(UINT i=0;i<count*4;i++)tokens[n++]=cc==6?0xffffffffu:i*17+0x3f000000u;if(cc==7){tokens[n++]=0x82000002u;for(UINT i=0;i<4;i++)tokens[n++]=0xccccccccu+i;}}
 if(shape==1)tokens[n++]=0xc0000000u;if(shape==2)tokens[n++]=0xa0000000u;if(shape==3)tokens[0]|=0x10000000u;tokens[n++]=0xffffffffu;memcpy(before,tokens,n*4);UINT max=shape==4?n-1:n,old_count=777,new_count=777;BOOL valid=oracle_validate(tokens,max,&old_count),actual=d9wg_declaration8_validate(tokens,max,&new_count);CHECK(actual==valid&&new_count==old_count);CHECK(!memcmp(tokens,before,n*4));
 for(UINT cap=0;cap<3;cap++){UINT capacity=cap==0?0:cap==1?1:32;D9WGVertexElement a[32],b[32];DWORD ca[384],cb[384];BYTE ma[96],mb[96];UINT na=777,nb=777,va=777,vb=777;memset(a,0xcd,sizeof(a));memset(b,0xcd,sizeof(b));memset(ca,0xcd,sizeof(ca));memset(cb,0xcd,sizeof(cb));memset(ma,0xcd,sizeof(ma));memset(mb,0xcd,sizeof(mb));BOOL expected=valid?oracle_elements(tokens,old_count,a,capacity,&na,ca,96,ma,&va):FALSE;BOOL got=valid?d9wg_declaration8_elements(tokens,new_count,b,capacity,&nb,cb,96,mb,&vb):FALSE;CHECK(got==expected&&na==nb&&va==vb);CHECK(!memcmp(a,b,sizeof(a))&&!memcmp(ca,cb,sizeof(ca))&&!memcmp(ma,mb,sizeof(ma)));
 if(cap==2)for(UINT only=0;only<2;only++)for(UINT error=0;error<3;error++){BOOL accepted=expected;if(accepted&&only)for(UINT i=0;i<na;i++)if(a[i].usage==3&&!a[i].usage_index&&a[i].type!=2)accepted=FALSE;calls=0;backend_result=error==1?-3:error==2?-2:0;memset(captured,0xcc,sizeof(captured));IDirect3DVertexDeclaration9*out=(void*)(uintptr_t)77;HRESULT hr=d9wg_d3d9_core_vertex_declaration8(&device,tokens,only,&out);/* Constructor uses the full declared bound, independently of synthetic max. */
 BOOL constructor_valid=oracle_validate(tokens,8192,&old_count),constructor_ok=constructor_valid&&oracle_elements(tokens,old_count,a,32,&na,ca,96,ma,&va);if(constructor_ok&&only)for(UINT i=0;i<na;i++)if(a[i].usage==3&&!a[i].usage_index&&a[i].type!=2)constructor_ok=FALSE;CHECK(hr==(constructor_ok?backend_result:-1)&&calls==(UINT)constructor_ok&&out==(constructor_ok&&!error?&object:NULL));if(constructor_ok){CHECK(!memcmp(captured,a,na*8));CHECK(captured[na].Stream==255&&captured[na].Type==17);}CHECK(!memcmp(tokens,before,n*4));(void)accepted;cases++;}
 }
}
for(UINT end=8190;end<=8192;end++){DWORD*long_decl=calloc(8193,sizeof(DWORD));CHECK(long_decl!=NULL);long_decl[0]=0x20000000u;long_decl[1]=0x40020000u;long_decl[end]=0xffffffffu;IDirect3DVertexDeclaration9*value=(void*)(uintptr_t)77;calls=0;backend_result=0;HRESULT h=d9wg_d3d9_core_vertex_declaration8(&device,long_decl,TRUE,&value);CHECK(h==(end<8192?0:-1)&&calls==(UINT)(end<8192)&&value==(end<8192?&object:NULL));free(long_decl);cases++;}
IDirect3DVertexDeclaration9*out=(void*)(uintptr_t)77;DWORD decl[]={0x20000000u,0x40020000u,0xffffffffu};CHECK(d9wg_d3d9_core_vertex_declaration8(NULL,decl,0,&out)==-1&&!out);out=(void*)(uintptr_t)77;CHECK(d9wg_d3d9_core_vertex_declaration8(&device,NULL,0,&out)==-1&&!out);CHECK(d9wg_d3d9_core_vertex_declaration8(&device,decl,0,NULL)==-1);device.api=9;out=(void*)(uintptr_t)77;CHECK(d9wg_d3d9_core_vertex_declaration8(&device,decl,0,&out)==-1&&!out);cases+=4;
printf("PASS actual declaration8 core: %u checks / %u cases; frozen8 parser/register policy,SDK9 element layout,streams/offsets/types/opaque registers,constant blocks/literals/overlap/ranges,capacity,normal-only policy,output/profile/constructor failures\\n",checks,cases);return 0;}
`;
fs.writeFileSync(dir+'/decl.c',c);const build=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror',dir+'/decl.c','-o',dir+'/decl'],{encoding:'utf8'});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+'/decl',[],{encoding:'utf8',timeout:90000});assert(!run.error,String(run.error));assert.equal(run.status,0,run.stdout+run.stderr+' signal='+run.signal);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
