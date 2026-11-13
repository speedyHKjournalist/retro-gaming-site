"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../ddrawproxy/ddraw_proxy.c"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-memory-mutants-"));
const cases=[
 ["packed memory route disabled","if (surface_can_memory_blt(destination, source, flags))","if (FALSE && surface_can_memory_blt(destination, source, flags))"],
 ["last packed byte dropped","CopyMemory(row, source_row, dw*bytes);","CopyMemory(row, source_row, dw*bytes-1u);"],
 ["overlap snapshot disabled","if (source_start < destination_start + destination->memory->bytes &&","if (FALSE && source_start < destination_start + destination->memory->bytes &&"],
 ["duplicate and external overlap missed","if (source_start < destination_start + destination->memory->bytes &&","if (source == destination && source_start < destination_start + destination->memory->bytes &&"],
 ["horizontal samples mirrored","source_row + sx*bytes, bytes);","source_row + (sw-1u-sx)*bytes, bytes);"],
 ["vertical fractional step omitted","y_error += y_remainder;","y_error += 0u*y_remainder;"],
 ["dirty source not recovered","if (!surface_readback(source)) return FALSE;","if (FALSE && !surface_readback(source)) return FALSE;"],
 ["partial destination not recovered","if (destination->memory->gpu_dirty && (dst.left || dst.top ||","if (FALSE && destination->memory->gpu_dirty && (dst.left || dst.top ||"],
 ["logical backing size ignored","(uint64_t)area->right*surface->bytes_per_pixel <= surface->memory->bytes;","(uint64_t)area->right*surface->bytes_per_pixel <= surface->memory->bytes+4u;"],
 ["short pitch accepted","(uint64_t)surface->width * surface->bytes_per_pixel > surface->pitch)","(FALSE && (uint64_t)surface->width * surface->bytes_per_pixel > surface->pitch))"],
 ["upload debt misses aliases","if (alias->memory == surface->memory) alias->upload_pending = pending;","if (alias == surface) alias->upload_pending = pending;"],
 ["failed row upload loses debt","if (!result) surface_memory_upload_aliases(surface, TRUE);","if (FALSE && !result) surface_memory_upload_aliases(surface, TRUE);"],
 ["full upload retains stale alias debt","surface_memory_upload_aliases(surface, FALSE);\n    DDWG_TRACE_CHECKPOINT(\"SURFACE UPLOAD_END", "surface->upload_pending = FALSE;\n    DDWG_TRACE_CHECKPOINT(\"SURFACE UPLOAD_END"],
 ["write leaves GPU authoritative","destination->memory->gpu_dirty = FALSE;","destination->memory->gpu_dirty = TRUE;"],
 ["video-memory path captured by CPU","(destination->desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) &&","(TRUE || (destination->desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY)) &&"],
 ["keyed path captured by CPU","return source && !flags && !destination->clipper &&","return source && (TRUE || !flags) && !destination->clipper &&"],
];
try{for(const[label,before,after]of cases){let body=source,prefix="",suffix="";if(["overlap snapshot disabled","duplicate and external overlap missed","dirty source not recovered","partial destination not recovered","write leaves GPU authoritative","keyed path captured by CPU"].includes(label)){const predicate=label==="keyed path captured by CPU",start=source.indexOf(predicate?"static BOOL surface_can_memory_blt(":"static BOOL surface_memory_blt("),end=source.indexOf(predicate?"static BOOL surface_memory_rect(":"static BOOL surface_can_bc_blt(",start);assert(start>=0&&end>start,label);prefix=source.slice(0,start);body=source.slice(start,end);suffix=source.slice(end);}assert.equal(body.split(before).length,2,label);const file=dir+"/proxy.c";fs.writeFileSync(file,prefix+body.replace(before,after)+suffix);const r=spawnSync(process.execPath,[path.join(__dirname,"ddraw_memory_blt_test.js")],{env:{...process.env,DDRAW_MEMORY_BLT_SOURCE:file},encoding:"utf8"});assert.notEqual(r.status,0,"survived "+label);assert.match(r.stderr,/memory blit line [0-9]+:/,"strict compilation and runtime assertion required: "+label+"\n"+r.stderr.slice(-2000));assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(r.stderr),r.stderr.slice(-2000));console.log("KILL "+label);}console.log("PASS packed memory blit faults: "+cases.length+"/"+cases.length);}finally{fs.rmSync(dir,{recursive:true,force:true});}
