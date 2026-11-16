"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../ddrawproxy/ddraw_proxy.c"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-mode-mutants-"));
const cases=[
 ["reported size follows caller shape","((DDSURFACEDESC *)to)->dwSize = version <= DD_VERSION_3","((DDSURFACEDESC *)to)->dwSize = output_size == sizeof(DDSURFACEDESC) && version * 0u == 0"],
 ["narrow caller gets full descriptor","    else CopyMemory(to, from, sizeof(*from));","    CopyMemory(to, from, sizeof(*from));"],
];
try{for(const[label,before,after]of cases){assert.equal(source.split(before).length,2,label);const file=dir+"/proxy.c";fs.writeFileSync(file,source.replace(before,after));const run=spawnSync(process.execPath,[path.join(__dirname,"ddraw_display_mode_shape_test.js")],{encoding:"utf8",env:{...process.env,DDRAW_MODE_SHAPE_SOURCE:file}});assert.notEqual(run.status,0,"survived "+label);assert.match(run.stderr,/mode shape line \d+:/,"strict compilation and runtime assertion required: "+label+"\n"+run.stderr);console.log("KILL "+label);}console.log("PASS display-mode shape mutations: 2/2 compiled implementations rejected by native descriptor/guard assertions");}finally{fs.rmSync(dir,{recursive:true,force:true});}
