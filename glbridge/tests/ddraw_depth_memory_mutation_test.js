"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../ddrawproxy/ddraw_proxy.c"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-depth-mutants-"));
const cases=[
 ["partial dirty shadow accepted","(was_dirty && (stencil || area.left || area.top ||","(was_dirty && FALSE && (stencil || area.left || area.top ||"],
 ["shadow ignores row pitch","y * surface->pitch + x * surface->bytes_per_pixel","(y * surface->width + x) * surface->bytes_per_pixel"],
 ["depth fill overwrites stencil","packed = (packed & ~mask) | (value & mask);","packed = value;"],
 ["exact integer mirror omitted","surface_mirror_depth_fill(destination, destination_rect, fill_color, was_dirty);","surface_mirror_depth_fill(destination, destination_rect, 0, was_dirty);"],
 ["failed emission writes mirror","if (!emit_command(D9WG_OP_DD_BLT, &command, sizeof(command)))","if (FALSE && !emit_command(D9WG_OP_DD_BLT, &command, sizeof(command)))"],
 ["depth upload discarded","if (!surface->shadow) {","if (!surface->shadow || surface_depth_mask(surface->format)) {"],
];
try{for(const [label,before,after]of cases){assert.equal(source.split(before).length,2,label);let mutant=source.replace(before,after);const file=dir+"/proxy.c";fs.writeFileSync(file,mutant);const r=spawnSync(process.execPath,[path.join(__dirname,"ddraw_depth_memory_test.js")],{env:{...process.env,DDRAW_DEPTH_MEMORY_SOURCE:file},encoding:"utf8"});assert.notEqual(r.status,0,"survived "+label);assert.match(r.stderr,/DDraw depth memory line/,"must compile then fail a runtime assertion: "+label+"\n"+r.stderr);console.log("KILL "+label);}console.log("PASS DDraw depth memory mutations: "+cases.length+"/"+cases.length+", compilation and runtime assertions");}finally{fs.rmSync(dir,{recursive:true,force:true});}
