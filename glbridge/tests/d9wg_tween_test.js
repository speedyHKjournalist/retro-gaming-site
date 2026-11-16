"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-tween-"));
try{
 const file=path.join(directory,"tween.c"),binary=path.join(directory,"tween");
 fs.writeFileSync(path.join(directory,"windows.h"),'#include <stdlib.h>\nstatic void *GetProcessHeap(void){return (void*)1;}\nstatic void *HeapAlloc(void *heap,unsigned flags,size_t size){(void)heap;(void)flags;return malloc(size);}\nstatic int HeapFree(void *heap,unsigned flags,void *pointer){(void)heap;(void)flags;free(pointer);return 1;}\n');
 const windowsHeader=process.env.D9WG_TWEEN_WINDOWS_HEADER||path.join(root,"runtime/d9wg_vertex_windows.h");
 fs.writeFileSync(file,'#include <stdio.h>\n#include "tests/native/d9wg_tween_cases.h"\n#include "'+windowsHeader+'"\nstatic int window_cases(void){const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};uint32_t rs[256]={0};rs[151]=255;rs[170]=0x3f400000;D9WGVertexState state={0};if(d9wg_vertex_windows_blend(&state,rs,identity,1,0,identity,identity)||!state.blend||!state.blend->tween||state.blend->tween_factor!=.75f)return __LINE__;d9wg_vertex_windows_blend_free(&state);return state.blend?__LINE__:0;}\nint main(void){int result=window_cases();if(!result)result=tween_cases(100000);if(result){fprintf(stderr,"tween mismatch header line %d\\n",result);return 1;}puts("PASS C tween: 100000 analytic blends, independent position/normal channels, lighting/normalization/fog/texgen/user planes, packed secondary attributes, multi/constant streams, atomic failure and ClipStatus");return 0;}\n');
 const sources=["clip","blend","vertex","fvf","math","lighting","tnl_math"].map(name=>process.env["D9WG_TWEEN_"+name.toUpperCase()+"_SOURCE"]||path.join(root,"runtime/d9wg_"+name+".c"));
 const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-ffp-contract=off","-Wall","-Wextra","-Werror","-I",directory,"-I",root,"-I",path.join(root,"runtime"),file,...sources,"-o",binary],{encoding:"utf8"});assert.equal(build.status,0,build.stderr||String(build.error));
 const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr||String(run.error));process.stdout.write(run.stdout);
}finally{fs.rmSync(directory,{recursive:true,force:true});}
