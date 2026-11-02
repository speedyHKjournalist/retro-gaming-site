"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-window-watch-"));
try{
 const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-pthread","-I",root+"/tests/native-watch","-I",root,"-I",root+"/runtime",'-DD9WG_WINDOW_WATCH_IMPLEMENTATION="'+(process.env.D9WG_WINDOW_WATCH_SOURCE||root+"/runtime/d9wg_window_watch.c")+'"',root+"/tests/native-watch/window_watch_test.c",root+"/runtime/d9wg_runtime.c","-o",dir+"/test"],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+"/test",[],{encoding:"utf8",timeout:60000});assert.equal(run.status,0,run.stderr||run.error);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
