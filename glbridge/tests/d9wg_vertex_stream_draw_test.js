"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-streams-"));
try{
 const input=path.join(directory,"stream.c"),binary=path.join(directory,"stream");
 fs.writeFileSync(input,'#include <stdio.h>\n#include "tests/native/d9wg_vertex_stream_draw_cases.h"\nint main(void){int e=vertex_stream_draw_cases();if(e){fprintf(stderr,"multi-stream draw mismatch line %d\\n",e);return 1;}puts("PASS multi-stream record-aware clipping: exact single-stream geometry/color/texture agreement, per-source byte addresses, user planes, viewport changes, signed indices and no partial emission");return 0;}');
 const sources=["clip","clip_draw","blend","vertex","fvf","math","lighting","tnl_math"].map(n=>process.env['D9WG_'+n.toUpperCase()+'_SOURCE']||path.join(root,"runtime/d9wg_"+n+".c"));
 const built=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-ffp-contract=off","-Wall","-Wextra","-Werror","-I",root,"-I",path.join(root,"runtime"),input,...sources,"-o",binary],{encoding:"utf8"});assert.equal(built.status,0,built.stderr.slice(0,3000)||String(built.error));
 const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr.slice(0,1200)||String(run.error));process.stdout.write(run.stdout);
}finally{fs.rmSync(directory,{recursive:true,force:true});}
