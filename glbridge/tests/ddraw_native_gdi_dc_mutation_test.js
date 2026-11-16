"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../ddrawproxy/ddraw_proxy.c"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-gdi-mutants-"));
const cases=[
 ["missing SDK surface-more link","native->surface.lpSurfMore = &native->more;","native->surface.lpSurfMore = NULL;"],
 ["shadow alias shifted","global->fpVidMem = (FLATPTR)(uintptr_t)surface->shadow;","global->fpVidMem = (FLATPTR)(uintptr_t)(surface->shadow + 1);"],
 ["partial surface handle leaked","if (native->surface.hDDSurface) native->delete_surface(&native->surface);","if (FALSE && native->surface.hDDSurface) native->delete_surface(&native->surface);"],
 ["DC never released","native->release_dc(&native->surface);","(void)native->surface;"],
 ["GDI work not flushed","    if (native->dc) {\n        GdiFlush();","    if (native->dc) {\n        if (!native->draw.hDD) GdiFlush();"],
 ["parent destroyed before surface","    if (native->surface.hDDSurface) native->delete_surface(&native->surface);\n    if (native->draw.hDD) native->delete_draw(&native->draw);","    if (native->draw.hDD) native->delete_draw(&native->draw);\n    if (native->surface.hDDSurface) native->delete_surface(&native->surface);"],
];
try{for(const[label,before,after]of cases){assert.equal(source.split(before).length,2,label);const file=dir+"/proxy.c";fs.writeFileSync(file,source.replace(before,after));const run=spawnSync(process.execPath,[path.join(__dirname,"ddraw_native_gdi_dc_test.js")],{encoding:"utf8",env:{...process.env,DDRAW_NATIVE_GDI_SOURCE:file}});assert.notEqual(run.status,0,"survived "+label);assert.match(run.stderr,/native GDI line \d+:/,"strict compilation and runtime ownership assertion required: "+label+"\n"+run.stderr);assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(run.stderr),run.stderr);console.log("KILL "+label);}console.log("PASS native GDI DC mutations: 6/6 compiled implementations rejected by SDK graph/lifetime assertions");}finally{fs.rmSync(dir,{recursive:true,force:true});}
