'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-shader9-constants-'));
try{
 fs.copyFileSync(__dirname+'/native/d9wg_shader9_constants_cases.h',dir+'/test.c');
 let r=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+root+'/runtime',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);
 r=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(r.status,0,r.stdout+r.stderr);process.stdout.write(r.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
