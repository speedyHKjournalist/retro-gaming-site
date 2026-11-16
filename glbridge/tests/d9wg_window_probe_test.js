"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-window-probe-"));
try{
 const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-I",root+"/tests/native-window","-I",root,"-I",root+"/runtime",root+"/tests/native-window/window_probe_test.c",root+"/tests/native-window/window_mock.c",process.env.D9WG_WINDOW_PROBE_SOURCE||root+"/runtime/d9wg_window_probe.c",process.env.D9WG_WINDOW_GEOMETRY_SOURCE||root+"/runtime/d9wg_window_geometry.c",root+"/runtime/d9wg_window_state.c","-o",dir+"/test"],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+"/test",[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
