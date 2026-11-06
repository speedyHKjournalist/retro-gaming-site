"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),nine=fs.readFileSync(process.env.D8WG_SHADER_PAIR_CORE_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"shader8-pair-"));
function extract(source,name){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end).replace(/__attribute__\s*\(\(__stdcall__\)\)/g,"");}
// S2-41 contract delta to the frozen legacy oracle: CND is legal in ps_1_4.
function legacyCnd14(source){const before="if (!is_pixel_shader || minor > 3u) return FALSE;";assert.equal(source.split(before).length,2);return source.replace(before,"if (!is_pixel_shader || minor > 4u) return FALSE;");}
function preprocess(api,body){const file=dir+"/sdk"+api+".c";fs.writeFileSync(file,`#include <d3d${api}.h>\n#include "${root}/d3d9proxy/d3d9_core.h"\n#include "${root}/runtime/d9wg_declaration8.h"\n#define TRACE(...)\n#define D8WG_TRACE(...)\n`+body.replace(/\bWINAPI\b/g,""));const p=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",["-E","-P",file],{encoding:"utf8",maxBuffer:16<<20});assert.equal(p.status,0,p.stderr);return p.stdout;}
try{
 const old8=legacyCnd14(fs.readFileSync(root+"/tests/fixtures/d3d8_shader_validation_before_core.c","utf8")),create8=["vertex","pixel"].map(k=>fs.readFileSync(root+"/tests/fixtures/d3d8_device_create_"+k+"_shader_before_validation_core.c","utf8")).join("\n"),old9=fs.readFileSync(root+"/tests/fixtures/d3d9_shader_create_before_validation8.c","utf8"),names=["shader8_regtype_valid","shader8_opcode_supported","shader8_validate_body","shader8_inputs_declared","shader_token_count","shader_bytecode_hash","create_shader_object","create_shader","device_create_vertex_shader","device_create_pixel_shader"];
 const sdk8=preprocess(8,"#define D8WG_TRACE_ERROR(v) (v)\n"+old8+create8),sdk9=preprocess(9,"#define TRACE_REFUSE(v) (v)\n#define D9_SIO_OPCODE_MASK 0xffffu\n#define D9_SIO_COMMENT 0xfffeu\n#define D9_SIO_END 0xffffu\n#define D9_SIO_DEF 81u\n#define D9_SIO_DEFI 48u\n#define D9_SIO_DEFB 47u\n"+names.map(n=>extract(nine,n)).join("\n")+old9.replace(/\bcreate_shader\b/g,"create_before9"));
 function enumeration(s,n){const m=s.match(new RegExp("typedef enum _"+n+"\\s*\\{[\\s\\S]*?\\}\\s*"+n+"\\s*;"));assert(m,n);return m[0];}
 const reg8=s=>s.replace(/\b(D3DSPR_\w+|_D3DSHADER_PARAM_REGISTER_TYPE|D3DSHADER_PARAM_REGISTER_TYPE)\b/g,"before8_$1"),before=s=>reg8(s).replace(/\b(shader_regtype_valid|shader_opcode_supported|validate_shader_body)\b/g,"before8_$1");
 function prefix(n){const b=extract(sdk8,n),end=b.indexOf("    entry =");assert(end>0);return before(b.slice(0,end)+"return 0x12345678;\n}").replace(new RegExp("\\b"+n+"\\b","g"),n+"_before8");}
 const code=`#include <stdint.h>
#include "${root}/runtime/d9wg_shader9_constants.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
typedef uint32_t DWORD,UINT;typedef uint16_t WORD;typedef uint8_t BYTE;typedef int BOOL,HRESULT;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D9_MAX_SHADER_TOKENS 65536u
#define D9WG_D3D8_SHADER_TOKENS 8192u
#define D8WG_MAX_SHADER_TOKENS 8192u
#define D3D_OK 0
#define D3DERR_INVALIDCALL ((HRESULT)0x8876086cu)
#define E_OUTOFMEMORY ((HRESULT)0x8007000eu)
#define D3DERR_DRIVERINTERNALERROR ((HRESULT)0x88760827u)
#define HEAP_ZERO_MEMORY 8
#define GetProcessHeap() NULL
#define CopyMemory(p,q,n) memcpy(p,q,n)
#define ZeroMemory(p,n) memset(p,0,n)
#include "${root}/runtime/d9wg_declaration8.h"
#include "${process.env.D8WG_LOCAL_DEFS_HEADER||root+'/runtime/d9wg_shader8_local_defs.h'}"
#define D3DDECL_END() {255,0,17,0,0,0}
typedef struct{WORD Stream,Offset;BYTE Type,Method,Usage,UsageIndex;}D3DVERTEXELEMENT9;
#define FAILED(v) ((v)<0)
${enumeration(sdk9,"D3DSHADER_INSTRUCTION_OPCODE_TYPE")}
${enumeration(sdk9,"D3DSHADER_PARAM_REGISTER_TYPE")}
${reg8(enumeration(sdk8,"D3DSHADER_PARAM_REGISTER_TYPE"))}
typedef struct D9Shader D9Shader;typedef struct{UINT api,refs;D9Shader*shaders;struct{UINT BehaviorFlags;}creation;}D9Device,D8Device,IDirect3DDevice8,IDirect3DDevice9;typedef struct{const void*lpVtbl;}IDirect3DVertexShader9,IDirect3DPixelShader9;typedef UINT D8Shader;
struct D9Shader{union{IDirect3DVertexShader9 vertex;IDirect3DPixelShader9 pixel;}iface;UINT refcount;D9Device*device;UINT handle;BOOL is_pixel;UINT token_count;DWORD*code;UINT hash_low,hash_high;D9Shader*next_device_resource;};
static int g_vertex_shader_vtbl,g_pixel_shader_vtbl;static UINT attempts,live,fail_at,fail_emit,emissions,dumps,child_adds,child_releases,allocator=100;static const DWORD*expected_input;static UINT expected_count,decl_live,shader_live,decl_emits,shader_emits,fail_decl_emit;
static D9Device*device_from_iface(D9Device*p){return p;}static UINT device_compatibility_api(const D9Device*d){return d->api;}
static BOOL validate_vertex_declaration(const DWORD*d,UINT max,UINT*out){assert(d&&max==8192);*out=0;return 1;}
static void*HeapAlloc(void*heap,UINT flags,size_t bytes){(void)heap;assert(bytes&&(flags==0||flags==8));attempts++;if(attempts==fail_at)return NULL;BYTE*allocation=malloc(bytes+32);assert(allocation);memset(allocation,0xa5,bytes+32);memcpy(allocation,&bytes,sizeof(bytes));memset(allocation+16,flags?0:0xcd,bytes);live++;return allocation+16;}
static void HeapFree(void*heap,UINT flags,void*p){(void)heap;assert(!flags&&p&&live);BYTE*b=(BYTE*)p-16;size_t n;memcpy(&n,b,sizeof(n));for(UINT i=8;i<16;i++)assert(b[i]==0xa5);for(UINT i=0;i<16;i++)assert(b[16+n+i]==0xa5);live--;free(b);}
static UINT allocate_shader_handle(void){return ++allocator;}
static void device_child_add_ref(D9Device*d){assert(d->refs>=1);d->refs++;child_adds++;}static void device_child_release(D9Device*d){assert(d->refs>=2);d->refs--;child_releases++;}
static void dump_shader_bytecode(const DWORD*c,UINT n,BOOL pixel,UINT lo,UINT hi){(void)pixel;assert(c!=expected_input&&n==expected_count&&!memcmp(c,expected_input,n*4u)&&(lo||hi));dumps++;}
static BOOL emit_shader_create(D9Device*d,D9Shader*s){assert(s->device==d&&s->refcount==1&&d->refs>=2&&!d->shaders&&s->code!=expected_input&&s->token_count==expected_count&&!memcmp(s->code,expected_input,s->token_count*4u));emissions++;shader_emits++;if(fail_emit)return FALSE;shader_live++;return TRUE;}
typedef struct {D9Device*device;UINT refs;}IDirect3DVertexDeclaration9;
static HRESULT make_decl(IDirect3DDevice9*d,const D3DVERTEXELEMENT9*e,IDirect3DVertexDeclaration9**out){assert(d->api==8&&e[0].Stream==0&&e[0].Offset==0&&e[0].Type==3);*out=NULL;IDirect3DVertexDeclaration9*c=HeapAlloc(NULL,8,sizeof(*c));if(!c)return E_OUTOFMEMORY;c->device=d;c->refs=1;device_child_add_ref(d);decl_emits++;if(fail_decl_emit){device_child_release(d);HeapFree(NULL,0,c);return D3DERR_DRIVERINTERNALERROR;}decl_live++;*out=c;return 0;}
static UINT release_decl(IDirect3DVertexDeclaration9*c){assert(c&&c->refs==1&&decl_live);D9Device*d=c->device;decl_live--;HeapFree(NULL,0,c);device_child_release(d);return 0;}
#define IDirect3DDevice9_CreateVertexDeclaration make_decl
#define IDirect3DVertexDeclaration9_Release release_decl
#define IDirect3DDevice9_CreatePixelShader device_create_pixel_shader
${["shader_regtype_valid","shader_opcode_supported","validate_shader_body"].map(n=>before(extract(sdk8,n))).join("\n")}
${["shader8_regtype_valid","shader8_opcode_supported","shader8_validate_body","shader8_inputs_declared","shader_token_count","shader_bytecode_hash"].map(n=>extract(sdk9,n)).join("\n")}
${prefix("device_create_vertex_shader")}
${prefix("device_create_pixel_shader")}
${["create_shader_object","create_shader","create_before9","device_create_vertex_shader","device_create_pixel_shader"].map(n=>extract(sdk9,n)).join("\n")}

${extract(nine,'d9wg_d3d9_core_vertex_declaration8')}
${extract(nine,'d9wg_d3d9_core_vertex_shader8')}
${extract(nine,'d9wg_d3d9_core_pixel_shader8')}
static UINT checks,cases;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Shader8 pair case %u line %u: %s\\n",cases,__LINE__,#v);return 1;}checks++;}while(0)
static void reset_counters(void){attempts=live=emissions=dumps=child_adds=child_releases=decl_live=shader_live=decl_emits=shader_emits=0;}
static void retire_pair(D9Device*d,D9Shader*s,IDirect3DVertexDeclaration9*c){if(s){assert(d->shaders==s&&shader_live);shader_live--;d->shaders=NULL;HeapFree(NULL,0,s->code);HeapFree(NULL,0,s);device_child_release(d);}if(c)release_decl(c);}
static UINT expected_defs(DWORD*out,const DWORD*code,UINT count,BOOL constants){UINT n=0;out[n++]=code[0];if(constants){const DWORD golden[]={81,0xa00f0002u,0x3f800000u,0x3f800000u,0,0x3f800000u,81,0xa00f0003u,0,0x3f800000u,0,0x3f800000u,81,0xa00f0008u,0,0,0x3f800000u,0x3f800000u};memcpy(out+n,golden,sizeof(golden));n+=18;}memcpy(out+n,code+1,(count-1)*4);return n+count-1;}
int main(void){(void)&device_create_vertex_shader_before8;(void)&device_create_pixel_shader_before8;(void)&create_before9;(void)&device_create_vertex_shader;const DWORD base[]={0x20000000u,0x40030000u,0xffffffffu};const DWORD full[]={0x20000000u,0x40030000u,0x84000002u,0x3f800000u,0,0,0x3f800000u,0,0x3f800000u,0,0x3f800000u,0x82000008u,0,0,0x3f800000u,0x3f800000u,0x82000002u,0x3f800000u,0x3f800000u,0,0x3f800000u,0xffffffffu};const DWORD normal[]={0x20000000u,0x40030000u,0x40010003u,0xffffffffu},badconst[]={0x20000000u,0x40030000u,0x82000060u,0,0,0,0,0xffffffffu},empty[]={0xffffffffu};DWORD function[8],golden[32],decl_before[32];
for(UINT api=8;api<=9;api++)for(UINT kind=0;kind<6;kind++)for(UINT declaration_kind=0;declaration_kind<5;declaration_kind++)for(UINT fault=0;fault<7;fault++){
 const DWORD*declarations[]={base,full,normal,badconst,empty};const DWORD*decl=declarations[declaration_kind];UINT decl_bytes=declaration_kind==0?sizeof(base):declaration_kind==1?sizeof(full):declaration_kind==2?sizeof(normal):declaration_kind==3?sizeof(badconst):sizeof(empty);memcpy(decl_before,decl,decl_bytes);const DWORD initial[]={0xfffe0100u,1,0xc00f0000u,0x90e40000u,1,0xd00f0000u,0xa0e40002u,0xffffu};memcpy(function,initial,sizeof(function));if(kind==2)function[0]=0xfffe0101u;if(kind==3)function[0]=0xfffe0200u;if(kind==4)function[0]=0xffff0100u;if(kind==5)function[1]=0xabcd;
 expected_count=expected_defs(golden,function,8,declaration_kind==1);expected_input=golden;D9Device d={.api=api,.refs=1};IDirect3DVertexDeclaration9*layout=(void*)(uintptr_t)77;IDirect3DVertexShader9*vs=(void*)(uintptr_t)77;reset_counters();fail_at=fault>=1&&fault<=4?fault:0;fail_decl_emit=fault==5;fail_emit=fault==6;BOOL valid=api==8&&(kind<=2)&&(declaration_kind<=2)&&!(kind==0&&declaration_kind==2);HRESULT expected=!valid?D3DERR_INVALIDCALL:fail_at==1?E_OUTOFMEMORY:fail_decl_emit?D3DERR_DRIVERINTERNALERROR:kind&&fail_at>=2&&fail_at<=4?E_OUTOFMEMORY:kind&&fail_emit?D3DERR_DRIVERINTERNALERROR:0;
 HRESULT hr=d9wg_d3d9_core_vertex_shader8(&d,decl,kind?function:NULL,&layout,&vs);CHECK(hr==expected&&!memcmp(decl_before,decl,decl_bytes));if(hr){CHECK(!layout&&!vs&&d.refs==1&&!d.shaders&&!live&&!decl_live&&!shader_live&&child_adds==child_releases);}else{CHECK(!kind||d.shaders);CHECK(layout&&layout->device==&d&&layout->refs==1&&vs==(kind?&d.shaders->iface.vertex:NULL));CHECK(d.refs==(kind?3u:2u)&&decl_live==1&&shader_live==(UINT)(kind!=0));D9Shader*s=kind?d.shaders:NULL;if(s){CHECK(!s->is_pixel&&s->token_count==expected_count&&!memcmp(s->code,golden,expected_count*4)&&s->iface.vertex.lpVtbl==&g_vertex_shader_vtbl);}retire_pair(&d,s,layout);CHECK(d.refs==1&&!live&&!decl_live&&!shader_live&&child_adds==child_releases);}cases++;
}
/* A declaration supplies only v0: every other source input must fail
 * before creating either object, including with allocation failures armed. */
for(UINT input=1;input<16;input++){
 DWORD code[]={0xfffe0101u,1u,0xc00f0000u,0x90e40000u|input,0xffffu};
 D9Device d={.api=8,.refs=1};IDirect3DVertexDeclaration9*c=(void*)(uintptr_t)77;IDirect3DVertexShader9*s=(void*)(uintptr_t)77;
 reset_counters();fail_at=1;fail_emit=fail_decl_emit=0;
 CHECK(d9wg_d3d9_core_vertex_shader8(&d,base,code,&c,&s)==D3DERR_INVALIDCALL);
 CHECK(!c&&!s&&!attempts&&!live&&d.refs==1);cases++;
}
/* Input-looking words inside comments and DEF literals are not reads. */
{const DWORD words[]={0x0002fffeu,0x90e40001u,0x90e4001fu,81u,0xa00f0000u,0x90e40002u,0xffffffffu,0xffffu,0u};
 CHECK(shader8_inputs_declared(base,3,words,9,1));cases++;}
/* NativecorePS8: fiveSM1.x versions,wrongprofile/kind/version/body andeach allocation/emit failure. */
for(UINT api=8;api<=9;api++)for(UINT minor=0;minor<6;minor++)for(UINT fault=0;fault<4;fault++){DWORD ps[]={0xffff0100u|minor,1,0x800f0000u,0x90e40000u,0xffffu};expected_input=ps;expected_count=5;D9Device d={.api=api,.refs=1};IDirect3DPixelShader9*out=(void*)(uintptr_t)77;reset_counters();fail_at=fault<2?fault+1:0;fail_emit=fault==2;fail_decl_emit=0;HRESULT expected=api!=8||minor>4?D3DERR_INVALIDCALL:fault<2?E_OUTOFMEMORY:fault==2?D3DERR_DRIVERINTERNALERROR:0;CHECK(d9wg_d3d9_core_pixel_shader8(&d,ps,&out)==expected);if(expected)CHECK(!out&&d.refs==1&&!live&&!d.shaders&&!shader_live);else{CHECK(out==&d.shaders->iface.pixel&&d.shaders->is_pixel);retire_pair(&d,d.shaders,NULL);CHECK(d.refs==1&&!live&&!shader_live);}cases++;}
/* The original8 program limit is checked beforethe18trustedDEF words areinserted. */
static DWORD large[8195],large_golden[8213];large[0]=0xfffe0101u;for(UINT end=8192;end<=8193;end++){large[end-1]=0;large[end]=0xffff;expected_count=expected_defs(large_golden,large,end+1,TRUE);expected_input=large_golden;D9Device d={.api=8,.refs=1};IDirect3DVertexDeclaration9*c=NULL;IDirect3DVertexShader9*s=NULL;reset_counters();fail_at=fail_emit=fail_decl_emit=0;CHECK(d9wg_d3d9_core_vertex_shader8(&d,full,large,&c,&s)==(end==8192?0:D3DERR_INVALIDCALL));if(end==8192){CHECK(c&&s&&d.shaders->token_count==8211&&d.shaders->code[8210]==0xffff);retire_pair(&d,d.shaders,c);}else CHECK(!c&&!s&&!attempts);CHECK(!live&&!decl_live&&!shader_live&&d.refs==1);cases++;}
/* Last legal declaration constant must survivecore8 expansion too. */
{const DWORD last_decl[]={0x20000000u,0x40030000u,0x8200005fu,0,0,0,0x3f800000u,0xffffffffu};const DWORD code[]={0xfffe0101u,1,0xc00f0000u,0x90e40000u,1,0xd00f0000u,0xa0e4005fu,0xffffu};const DWORD wanted[]={0xfffe0101u,81,0xa00f005fu,0,0,0,0x3f800000u,1,0xc00f0000u,0x90e40000u,1,0xd00f0000u,0xa0e4005fu,0xffffu};expected_input=wanted;expected_count=14;D9Device last={.api=8,.refs=1};IDirect3DVertexDeclaration9*layout=NULL;IDirect3DVertexShader9*shader=NULL;reset_counters();fail_at=fail_emit=fail_decl_emit=0;CHECK(d9wg_d3d9_core_vertex_shader8(&last,last_decl,code,&layout,&shader)==0&&layout&&shader&&last.shaders);CHECK(last.shaders->token_count==14&&!memcmp(last.shaders->code,wanted,sizeof(wanted)));retire_pair(&last,last.shaders,layout);CHECK(!live&&last.refs==1);cases++;}
D9Device d={.api=8,.refs=1};IDirect3DVertexDeclaration9*c=(void*)(uintptr_t)77;IDirect3DVertexShader9*s=(void*)(uintptr_t)77;reset_counters();CHECK(d9wg_d3d9_core_vertex_shader8(NULL,base,NULL,&c,&s)==D3DERR_INVALIDCALL&&!c&&!s);c=(void*)(uintptr_t)77;s=(void*)(uintptr_t)77;CHECK(d9wg_d3d9_core_vertex_shader8(&d,NULL,NULL,&c,&s)==D3DERR_INVALIDCALL&&!c&&!s);s=(void*)(uintptr_t)77;CHECK(d9wg_d3d9_core_vertex_shader8(&d,base,NULL,NULL,&s)==D3DERR_INVALIDCALL&&!s);c=(void*)(uintptr_t)77;CHECK(d9wg_d3d9_core_vertex_shader8(&d,base,NULL,&c,NULL)==D3DERR_INVALIDCALL&&!c);IDirect3DPixelShader9*p=(void*)(uintptr_t)77;CHECK(d9wg_d3d9_core_pixel_shader8(NULL,NULL,&p)==D3DERR_INVALIDCALL&&!p);p=(void*)(uintptr_t)77;CHECK(d9wg_d3d9_core_pixel_shader8(&d,NULL,&p)==D3DERR_INVALIDCALL&&!p);CHECK(d9wg_d3d9_core_pixel_shader8(&d,NULL,NULL)==D3DERR_INVALIDCALL);CHECK(!live&&!attempts);cases+=7;
printf("PASS actual core8 shader pairs: %u checks / %u cases; real9 allocation,expanded localDEFbytes/overlap/holes/END,FFPNULL/normal,VS1.0/1.1,PS1.0-1.4,original8192bound beforeexpanded8211storage,profile/outputs/OOM/decl-shader emission rollback,parent pins/guards\\n",checks,cases);return 0;}
`;
 fs.writeFileSync(dir+"/create.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-Wno-unused-variable",dir+"/create.c","-o",dir+"/create"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/create",[],{encoding:"utf8",timeout:30000});assert.equal(r.status,0,r.stdout+r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
