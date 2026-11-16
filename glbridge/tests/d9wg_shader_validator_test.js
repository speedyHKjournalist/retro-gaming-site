"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-validator-"));
try {
 fs.writeFileSync(dir+"/test.c",String.raw`#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stdint.h>
typedef void *HMODULE,*PVOID;typedef unsigned UINT;typedef wchar_t WCHAR;
#define WINAPI
#define MAX_PATH 260
#define CopyMemory memcpy
static HMODULE g_module_instance=(void *)1;
static unsigned loads,frees,creates,reads,mode,checks;
static HMODULE loaded=(void *)2;
static void *object1=(void *)101,*object2=(void *)102;
static void *native_create(void){++creates;return mode==8?NULL:object1;}
static void *other_create(void){++creates;return object2;}
void *Direct3DShaderValidatorCreate9(void);
static UINT GetSystemDirectoryW(WCHAR *p,UINT count){assert(count==MAX_PATH);++reads;if(mode==1)return 0;if(mode==2)return MAX_PATH;if(mode==3)return MAX_PATH-2;wcscpy(p,L"C:\\Windows\\system32");return (UINT)wcslen(p);}
static HMODULE LoadLibraryW(const WCHAR *p){assert(!wcscmp(p,L"C:\\Windows\\system32\\d3d9.dll"));++loads;return mode==4?NULL:loaded;}
static void FreeLibrary(HMODULE m){assert(m==loaded);++frees;}
typedef void *(*Create)(void);typedef Create FARPROC;
static Create GetProcAddress(HMODULE m,const char *s){assert(!strcmp(s,"Direct3DShaderValidatorCreate9"));if(mode==5)return NULL;if(mode==6)return Direct3DShaderValidatorCreate9;return m==(void *)3?other_create:native_create;}
static PVOID InterlockedCompareExchangePointer(PVOID volatile *p,PVOID value,PVOID expected){if(mode==7&&value)*p=(void *)3;void *old=*p;if(old==expected)*p=value;return old;}
#include "${root}/d3d9proxy/d3d9_shader_validator.inc"
#define CHECK(v) do{assert(v);++checks;}while(0)
static void reset(unsigned m){g_native_shader_validator_module=NULL;loads=frees=creates=reads=0;mode=m;loaded=(void *)2;}
int main(void){
 reset(0);CHECK(Direct3DShaderValidatorCreate9()==object1);CHECK(Direct3DShaderValidatorCreate9()==object1);CHECK(loads==1&&!frees&&creates==2&&reads==1&&g_native_shader_validator_module==loaded);
 for(unsigned m=1;m<=6;++m){reset(m);CHECK(!Direct3DShaderValidatorCreate9());CHECK(!creates&&!g_native_shader_validator_module);CHECK(loads==(m>=4));CHECK(frees==(m>=5));}
 reset(0);loaded=g_module_instance;CHECK(!Direct3DShaderValidatorCreate9()&&loads==1&&frees==1&&!creates&&!g_native_shader_validator_module);
 reset(7);CHECK(Direct3DShaderValidatorCreate9()==object2);CHECK(loads==1&&frees==1&&creates==1&&g_native_shader_validator_module==(void *)3);CHECK(Direct3DShaderValidatorCreate9()==object2&&loads==1&&frees==1&&creates==2);
 reset(8);CHECK(!Direct3DShaderValidatorCreate9()&&creates==1&&g_native_shader_validator_module==loaded&&!frees);
 printf("PASS native shader validator delegation: %u checks; absolute system path,bounded path,self/alias recursion guards,missing export/load failures,one retained module,racing loads and real constructor result\n",checks);return 0;
}`);
 const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Wall","-Wextra","-Werror",dir+"/test.c","-o",dir+"/test"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);
 const r=spawnSync(dir+"/test",[],{encoding:"utf8"});assert.equal(r.status,0,r.stdout+r.stderr);process.stdout.write(r.stdout);
 const exports=fs.readFileSync(root+"/d3d9proxy/d3d9.def","utf8");assert.match(exports,/Direct3DShaderValidatorCreate9=Direct3DShaderValidatorCreate9@0/);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
