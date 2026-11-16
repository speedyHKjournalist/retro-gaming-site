"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../ddrawproxy/ddraw_proxy.c"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-fill-memory-mut-"));
const cases=[
 ["compressed blit reaches GPU","emit_blt","if (format_is_compressed(destination->format))","if (FALSE && format_is_compressed(destination->format))"],
 ["video memory falsely mirrored","surface_can_mirror_color_fill","(surface->desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY)","TRUE"],
 ["partial dirty shadow not recovered","surface_prepare_color_fill","return surface_readback(surface);","return TRUE;"],
 ["complete known fill needlessly read back","surface_prepare_color_fill","surface->memory->gpu_dirty && (area.left || area.top ||","surface->memory->gpu_dirty || (area.left || area.top ||"],
 ["unknown outside bytes declared valid","surface_mirror_color_fill","if (was_dirty && (area.left || area.top ||","if (FALSE && was_dirty && (area.left || area.top ||"],
 ["fill ignores row pitch","surface_mirror_color_fill","y * surface->pitch +","y * surface->width * surface->bytes_per_pixel +"],
 ["fill keeps only one byte","surface_mirror_color_fill","&value, surface->bytes_per_pixel);","&value, 1u);"],
 ["known CPU shadow left dirty","surface_mirror_color_fill","surface->memory->gpu_dirty = FALSE;","surface->memory->gpu_dirty = TRUE;"],
 ["native packed mirror omitted","emit_blt","if (mirror_color)\n        surface_mirror_color_fill","if (FALSE && mirror_color)\n        surface_mirror_color_fill"],
 ["keyed fill overwrites all CPU pixels","emit_blt","mirror_color = !source && flags == D9WG_DDBLT_COLOR_FILL &&","mirror_color = !source && (flags & D9WG_DDBLT_COLOR_FILL) &&"],
 ["failed emission changes CPU pixels","emit_blt","if (!emit_command(D9WG_OP_DD_BLT, &command, sizeof(command)))","if (FALSE && !emit_command(D9WG_OP_DD_BLT, &command, sizeof(command)))"],
];
try{for(const[label,name,from,to]of cases){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}const body=source.slice(m.index,end);assert.equal(body.split(from).length,2,label);const file=dir+"/proxy.c";fs.writeFileSync(file,source.slice(0,m.index)+body.replace(from,to)+source.slice(end));const r=spawnSync(process.execPath,[__dirname+"/ddraw_color_fill_memory_test.js"],{encoding:"utf8",env:{...process.env,DDRAW_COLOR_FILL_SOURCE:file}});assert.notEqual(r.status,0,label);assert.match(r.stderr,/color fill memory line/,label+": strict compilation and runtime rejection required");assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(r.stderr),r.stderr);console.log("KILL "+label);}console.log("PASS packed color-fill mutations: "+cases.length+"/"+cases.length+" compiled implementations rejected at runtime");}finally{fs.rmSync(dir,{recursive:true,force:true});}
