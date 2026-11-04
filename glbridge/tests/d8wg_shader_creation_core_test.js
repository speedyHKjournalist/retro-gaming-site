"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),nine=fs.readFileSync(process.env.D8WG_SHADER_VALIDATION_BACKEND_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-backend-"));
function extract(source,name){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end).replace(/__attribute__\s*\(\(__stdcall__\)\)/g,"");}
function preprocess(api,body){const file=dir+"/sdk"+api+".c";fs.writeFileSync(file,`#include <d3d${api}.h>\n#include "${root}/d3d9proxy/d3d9_core.h"\n#define TRACE(...)\n#define D8WG_TRACE(...)\n`+body.replace(/\bWINAPI\b/g,""));const p=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",["-E","-P",file],{encoding:"utf8",maxBuffer:16<<20});assert.equal(p.status,0,p.stderr);return p.stdout;}
try{
 const old8=fs.readFileSync(root+"/tests/fixtures/d3d8_shader_validation_before_core.c","utf8"),create8=["vertex","pixel"].map(k=>fs.readFileSync(root+"/tests/fixtures/d3d8_device_create_"+k+"_shader_before_validation_core.c","utf8")).join("\n"),old9=fs.readFileSync(root+"/tests/fixtures/d3d9_shader_create_before_validation8.c","utf8"),names=["shader8_regtype_valid","shader8_opcode_supported","shader8_validate_body","shader_token_count","shader_bytecode_hash","create_shader","device_create_vertex_shader","device_create_pixel_shader"];
 const sdk8=preprocess(8,"#define D8WG_TRACE_ERROR(v) (v)\n"+old8+create8),sdk9=preprocess(9,"#define TRACE_REFUSE(v) (v)\n#define D9_SIO_OPCODE_MASK 0xffffu\n#define D9_SIO_COMMENT 0xfffeu\n#define D9_SIO_END 0xffffu\n#define D9_SIO_DEF 81u\n#define D9_SIO_DEFI 48u\n#define D9_SIO_DEFB 47u\n"+names.map(n=>extract(nine,n)).join("\n")+old9.replace(/\bcreate_shader\b/g,"create_before9"));
 function enumeration(s,n){const m=s.match(new RegExp("typedef enum _"+n+"\\s*\\{[\\s\\S]*?\\}\\s*"+n+"\\s*;"));assert(m,n);return m[0];}
 const reg8=s=>s.replace(/\b(D3DSPR_\w+|_D3DSHADER_PARAM_REGISTER_TYPE|D3DSHADER_PARAM_REGISTER_TYPE)\b/g,"before8_$1"),before=s=>reg8(s).replace(/\b(shader_regtype_valid|shader_opcode_supported|validate_shader_body)\b/g,"before8_$1");
 function prefix(n){const b=extract(sdk8,n),end=b.indexOf("    entry =");assert(end>0);return before(b.slice(0,end)+"return 0x12345678;\n}").replace(new RegExp("\\b"+n+"\\b","g"),n+"_before8");}
 const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
typedef uint32_t DWORD,UINT;typedef uint16_t WORD;typedef int BOOL,HRESULT;
#define WINAPI
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
#define FAILED(v) ((v)<0)
${enumeration(sdk9,"D3DSHADER_INSTRUCTION_OPCODE_TYPE")}
${enumeration(sdk9,"D3DSHADER_PARAM_REGISTER_TYPE")}
${reg8(enumeration(sdk8,"D3DSHADER_PARAM_REGISTER_TYPE"))}
typedef struct D9Shader D9Shader;typedef struct{UINT api,refs;D9Shader*shaders;}D9Device,D8Device,IDirect3DDevice8,IDirect3DDevice9;typedef struct{const void*lpVtbl;}IDirect3DVertexShader9,IDirect3DPixelShader9;typedef UINT D8Shader;
struct D9Shader{union{IDirect3DVertexShader9 vertex;IDirect3DPixelShader9 pixel;}iface;UINT refcount;D9Device*device;UINT handle;BOOL is_pixel;UINT token_count;DWORD*code;UINT hash_low,hash_high;D9Shader*next_device_resource;};
static int g_vertex_shader_vtbl,g_pixel_shader_vtbl;static UINT attempts,live,fail_at,fail_emit,emissions,dumps,child_adds,child_releases,allocator=100;static const DWORD*expected_input;
static D9Device*device_from_iface(D9Device*p){return p;}static UINT device_compatibility_api(const D9Device*d){return d->api;}
static BOOL validate_vertex_declaration(const DWORD*d,UINT max,UINT*out){assert(d&&max==8192);*out=0;return 1;}
static void*HeapAlloc(void*heap,UINT flags,size_t bytes){(void)heap;assert(bytes&&(flags==0||flags==8));attempts++;if(attempts==fail_at)return NULL;void*p=calloc(1,bytes);assert(p);live++;return p;}
static void HeapFree(void*heap,UINT flags,void*p){(void)heap;assert(!flags&&p&&live);live--;free(p);}
static UINT allocate_shader_handle(void){return ++allocator;}
static void device_child_add_ref(D9Device*d){assert(d->refs==1);d->refs++;child_adds++;}static void device_child_release(D9Device*d){assert(d->refs==2);d->refs--;child_releases++;}
static void dump_shader_bytecode(const DWORD*c,UINT n,BOOL pixel,UINT lo,UINT hi){(void)pixel;assert(c!=expected_input&&n>=2&&!memcmp(c,expected_input,n*4u)&&(lo||hi));dumps++;}
static BOOL emit_shader_create(D9Device*d,D9Shader*s){assert(s->device==d&&s->refcount==1&&d->refs==2&&!d->shaders&&s->code!=expected_input&&s->token_count>=2&&!memcmp(s->code,expected_input,s->token_count*4u));emissions++;return !fail_emit;}
${["shader_regtype_valid","shader_opcode_supported","validate_shader_body"].map(n=>before(extract(sdk8,n))).join("\n")}
${["shader8_regtype_valid","shader8_opcode_supported","shader8_validate_body","shader_token_count","shader_bytecode_hash"].map(n=>extract(sdk9,n)).join("\n")}
${prefix("device_create_vertex_shader")}
${prefix("device_create_pixel_shader")}
${["create_shader","create_before9","device_create_vertex_shader","device_create_pixel_shader"].map(n=>extract(sdk9,n)).join("\n")}
static UINT checks,cases,valid8,invalid8,ordinary9;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Shader8 creation line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
static uint64_t reference_hash(const DWORD*code,UINT n){uint64_t h=UINT64_C(0xcbf29ce484222325);for(UINT i=0;i<n;i++)for(UINT b=0;b<4;b++){h^=(code[i]>>(b*8))&255u;h*=UINT64_C(0x1000001b3);}return h;}
static void retire(D9Device*d,D9Shader*s){assert(s&&d->shaders==s&&d->refs==2);d->shaders=NULL;HeapFree(NULL,0,s->code);HeapFree(NULL,0,s);device_child_release(d);}
int main(void){(void)&device_compatibility_api;(void)&shader8_validate_body;const UINT ops[]={0,1,2,4,5,6,19,20,47,48,64,65,66,67,78,80,81,89,0xfffd,0xfffe,0xffff,0xabcd};DWORD declaration[]={0xffffffffu};
for(UINT api=8;api<=9;api++)for(UINT pixel=0;pixel<2;pixel++)for(UINT tag=0;tag<3;tag++)for(UINT major=0;major<5;major++)for(UINT minor=0;minor<8;minor++)for(UINT o=0;o<sizeof(ops)/sizeof(ops[0]);o++){
 DWORD code[32];for(UINT i=0;i<32;i++)code[i]=0xffffu;code[0]=((tag==0?0xfffeu:tag==1?0xffffu:0x1234u)<<16)|(major<<8)|minor;UINT op=ops[o],words=5;before8_shader_opcode_supported((WORD)op,pixel,minor,&words);code[1]=op;for(UINT i=0;i<words;i++)code[i+2]=(i?0x90e40000u:pixel?0x800f0000u:0xc00f0000u)|(!pixel&&i?5u:0u);if(op==81){code[2]=0xa00f0000u;code[3]=0xffffu;code[4]=0x80000000u;code[5]=0x7f800000u;code[6]=0x7fc00000u;}if(op==0xfffe){code[1]=0x0002fffeu;code[2]=0xffffu;code[3]=0x12345678u;code[4]=0xffffu;}DWORD saved[32];memcpy(saved,code,sizeof(code));expected_input=code;
 D9Device d={.api=api,.refs=1},prior={.api=9,.refs=1};D9Shader*out=(void*)(uintptr_t)1,*old_out=NULL;attempts=live=fail_at=fail_emit=emissions=dumps=child_adds=child_releases=0;HRESULT expected;UINT body=0;
 if(api==8){DWORD handle=0x1234;HRESULT old=pixel?device_create_pixel_shader_before8(&d,code,&handle):device_create_vertex_shader_before8(&d,declaration,code,&handle,0);CHECK(!handle);expected=old==0x12345678?0:D3DERR_INVALIDCALL;if(!expected)CHECK(before8_validate_shader_body(code+1,8192,pixel,minor,&body));}
 else{expected=create_before9(&prior,code,pixel,&old_out);if(old_out){body=old_out->token_count;retire(&prior,old_out);}CHECK(prior.refs==1&&!prior.shaders&&!live);attempts=emissions=dumps=child_adds=child_releases=0;ordinary9++;}
 HRESULT actual=create_shader(&d,code,pixel,&out);CHECK(actual==expected&&!memcmp(code,saved,sizeof(code)));
 if(!actual){UINT wanted=api==8?body+2:body;uint64_t hash=reference_hash(code,wanted);CHECK(out&&out->device==&d&&d.shaders==out&&d.refs==2&&out->refcount==1&&out->is_pixel==(BOOL)pixel&&out->token_count==wanted&&out->hash_low==(UINT)hash&&out->hash_high==(UINT)(hash>>32)&&out->iface.vertex.lpVtbl==(pixel?(const void*)&g_pixel_shader_vtbl:(const void*)&g_vertex_shader_vtbl));CHECK(attempts==2&&live==2&&emissions==1&&dumps==1&&child_adds==1&&!child_releases);retire(&d,out);if(api==8)valid8++;}
 else{CHECK(!out&&d.refs==1&&!d.shaders&&!attempts&&!live&&!emissions&&!dumps&&!child_adds&&!child_releases);if(api==8)invalid8++;}
 CHECK(d.refs==1&&!d.shaders&&!live);cases++;
}
for(UINT api=8;api<=9;api++)for(UINT pixel=0;pixel<2;pixel++)for(UINT failure=0;failure<4;failure++){
 const DWORD code[]={pixel?0xffff0100u:0xfffe0100u,1,pixel?0x800f0000u:0xc00f0000u,0x90e40000u,0xffff};expected_input=code;D9Device d={.api=api,.refs=1};D9Shader*out=(void*)(uintptr_t)1;attempts=live=emissions=dumps=child_adds=child_releases=0;fail_at=failure<2?failure+1:0;fail_emit=failure==2;HRESULT expected=failure<2?E_OUTOFMEMORY:failure==2?D3DERR_DRIVERINTERNALERROR:D3DERR_INVALIDCALL;CHECK(create_shader(&d,failure==3?NULL:code,pixel,&out)==expected&&!out&&d.refs==1&&!d.shaders&&!live);CHECK(attempts==(failure==0?1u:failure==3?0u:2u)&&emissions==(failure==2)&&dumps==(failure==2)&&child_adds==(failure==2)&&child_releases==(failure==2));cases++;
}
fail_at=fail_emit=0;static DWORD boundary[8194];boundary[0]=0xfffe0100u;for(UINT end=8192;end<=8193;end++){boundary[end-1]=0;boundary[end]=0xffff;expected_input=boundary;D9Device root8={.api=8,.refs=1};D9Shader*s=(void*)(uintptr_t)1;attempts=live=emissions=dumps=child_adds=child_releases=0;HRESULT result=create_shader(&root8,boundary,0,&s);CHECK(result==(end==8192?D3D_OK:D3DERR_INVALIDCALL));if(!result){CHECK(s&&s->token_count==8193);retire(&root8,s);}else CHECK(!s&&!attempts&&!live);CHECK(root8.refs==1&&!root8.shaders&&!live);cases++;}
fail_at=fail_emit=0;D9Device d={.api=8,.refs=1};CHECK(device_create_vertex_shader(&d,NULL,NULL)==D3DERR_INVALIDCALL&&device_create_pixel_shader(&d,NULL,NULL)==D3DERR_INVALIDCALL);IDirect3DVertexShader9*vs=(void*)(uintptr_t)1;IDirect3DPixelShader9*ps=(void*)(uintptr_t)1;CHECK(device_create_vertex_shader(&d,NULL,&vs)==D3DERR_INVALIDCALL&&!vs&&device_create_pixel_shader(&d,NULL,&ps)==D3DERR_INVALIDCALL&&!ps&&!live);
printf("PASS actual shader8 core creation: %u checks,%u constructors,%u valid8,%u refused8,%u prior9 comparisons; actual old8 version/body policy,1.0/no-dcl,exact bytes/independent existing-protocol hash,type,parent pins,allocation/emit failures andunchanged ordinary9 results\\n",checks,cases,valid8,invalid8,ordinary9);return 0;}
`;
 fs.writeFileSync(dir+"/create.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-Wno-unused-variable",dir+"/create.c","-o",dir+"/create"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/create",[],{encoding:"utf8",timeout:30000});assert.equal(r.status,0,r.stdout+r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
