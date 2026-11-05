"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-exclusive-"));
try {
    const result=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer","-I"+root+"/tests/native-exclusive","-I"+root+"/runtime",root+"/tests/native-exclusive/window.c",process.env.D9WG_EXCLUSIVE_SOURCE||root+"/runtime/d9wg_exclusive_window.c","-o",dir+"/test"],{encoding:"utf8"});
    assert.equal(result.status,0,result.stderr);
    const run=spawnSync(dir+"/test",[],{encoding:"utf8",timeout:30000});
    assert.equal(run.status,0,run.stdout+run.stderr+" signal="+run.signal);process.stdout.write(run.stdout);
} finally {fs.rmSync(dir,{recursive:true,force:true});}
