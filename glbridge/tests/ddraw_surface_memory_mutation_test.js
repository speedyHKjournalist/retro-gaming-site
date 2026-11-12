"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../ddrawproxy/ddraw_proxy.c"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-memory-mut-"));
const mutations=[
 ["borrowed bits replaced","surface_memory_create(external ? request->lpSurface : NULL,","surface_memory_create(NULL,"],
 ["borrowed bytes owned","shadow_bytes, !external);","shadow_bytes, TRUE);"],
 ["caller RGB pitch ignored","pitch = external ? (UINT)request->lPitch\n            : surface_pitch_for(width, bytes_per_pixel);","pitch = surface_pitch_for(width, bytes_per_pixel);"],
 ["caller compressed pitch ignored","pitch = external ? (UINT)request->lPitch\n            : ((width + 3u) / 4u) * block_bytes;","pitch = ((width + 3u) / 4u) * block_bytes;"],
 ["external memory loses synchronization","surface->external_memory = external;","surface->external_memory = FALSE;"],
 ["description leaks pointer","surface->desc.lpSurface = NULL;","surface->desc.lpSurface = request->lpSurface;"],
 ["unflagged backbuffer value exposed","surface->desc.dwBackBufferCount = 0;","surface->desc.dwBackBufferCount = request->dwBackBufferCount;"],
 ["RGB metadata retains linear size","surface->desc.dwFlags &= ~DDSD_LINEARSIZE;","surface->desc.dwFlags |= DDSD_LINEARSIZE;"],
 ["storage allocation failure leaks memory","surface_memory_release(surface->memory);\n            heap_free(surface);","heap_free(surface);"],
];
try{for(const[label,from,to]of mutations){assert.equal(source.split(from).length,2,label);const file=dir+"/proxy.c";fs.writeFileSync(file,source.replace(from,to));const r=spawnSync(process.execPath,[__dirname+"/ddraw_surface_memory_test.js"],{encoding:"utf8",env:{...process.env,DDRAW_MEMORY_SOURCE:file}});assert.notEqual(r.status,0,label);assert.match(r.stderr,/surface memory line/,label+": must compile and fail at runtime");assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(r.stderr),r.stderr);console.log("KILL "+label);}console.log("PASS surface allocation mutations: "+mutations.length+"/"+mutations.length+" compiled implementations rejected at runtime");}finally{fs.rmSync(dir,{recursive:true,force:true});}
