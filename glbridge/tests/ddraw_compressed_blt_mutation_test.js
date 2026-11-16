"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),front=fs.readFileSync(root+"/ddrawproxy/ddraw_proxy.c","utf8"),codec=fs.readFileSync(root+"/ddrawproxy/ddraw_bc_codec.h","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-bc-mutants-"));
const cases=[
 ["BC2/3 incorrectly use three colors","codec","if (a > b || kind != 1u)","if (a > b || (kind != 1u && 0))"],
 ["BC1 transparency lost","codec","if (a > b || kind != 1u)","if (kind || 1)"],
 ["explicit alpha nibble reversed","codec","((i%2u)*4u)) & 15u)","((1u-i%2u)*4u)) & 15u)"],
 ["alpha interpolants reversed like XP software Blt","codec","(divisor + 1u - i)*a + (i - 1u)*b","(i - 1u)*a + (divisor + 1u - i)*b"],
 ["alpha selector spill omitted","codec","if (shift > 5u) bits |=","if (0 && shift > 5u) bits |="],
 ["encoder never finds color endpoints","codec","if (error > furthest)","if (error < furthest)"],
 ["BC1 writes sixteen bytes","codec","i < (kind == 1u ? 8u : 16u)","i < 16u"],
 ["compressed route disabled","front","return surface_can_bc_blt(destination, source, flags) &&","return FALSE && surface_can_bc_blt(destination, source, flags) &&"],
 ["source rectangle offset lost","front","surface_blt_sample_map(maps, dw, src.left, sw);","surface_blt_sample_map(maps, dw, 0, sw);"],
 ["old partial block erased","front","d9wg_bc_decode(destination->format, output, rgba);","d9wg_bc_decode(destination->format, output, rgba); ZeroMemory(rgba,sizeof(rgba));"],
 ["overlapping encoded source not snapshotted","body","if (source_start < destination_start + destination->memory->bytes &&","if (FALSE && source_start < destination_start + destination->memory->bytes &&"],
 ["raw blocks unnecessarily reencoded","body","BOOL raw = source->format == destination->format","BOOL raw = FALSE && source->format == destination->format"],
 ["small mip padding not clamped","front","if (x >= valid_width || y >= valid_height)","if (FALSE && (x >= valid_width || y >= valid_height))"],
 ["existing premultiplied pixels multiplied twice","front","if (destination_premultiplied && !source_premultiplied)","if (destination_premultiplied)"],
 ["stored encoder premultiplies a second time","codec-front","for (unsigned c = 0; c < 4u; ++c) colors[i][c] = in[i][c];","for (unsigned c = 0; c < 4u; ++c) colors[i][c] = in[i][c]; if(kind==2u||kind==4u)for(unsigned c=0;c<3u;c++)colors[i][c]=(unsigned)colors[i][c]*colors[i][3]/255u;"],
 ["compressed backing tail ignored","front","((uint64_t)area->right + 3u)/4u*block <= surface->memory->bytes;","((uint64_t)area->right + 3u)/4u*block <= surface->memory->bytes+16u;"],
 ["dirty source read failure ignored","body","if (!surface_readback(source)) return FALSE;","if (FALSE && !surface_readback(source)) return FALSE;"],
 ["partial dirty destination ignored","body","if (destination->memory->gpu_dirty && (dst.left || dst.top ||","if (FALSE && destination->memory->gpu_dirty && (dst.left || dst.top ||"],
 ["failed upload reported as success","body","if (!emit_surface_upload(destination, NULL)) return FALSE;","if (!emit_surface_upload(destination, NULL) && FALSE) return FALSE;"],
 ["cached block texels transposed","front","cached[(y%4u)*4u + x%4u]","cached[(x%4u)*4u + y%4u]"],
 ["unsupported P8 source admitted","front","source->format <= DDWG_FMT_A4R4G4B4","source->format <= DDWG_FMT_P8"],
];
try{for(const[label,kind,before,after]of cases){let original=kind.startsWith("codec")?codec:front;let prefix="",suffix="";if(kind==="body"){const at=original.indexOf("static BOOL surface_bc_blt("),end=original.indexOf("static BOOL emit_blt(",at);assert(at>=0&&end>at);prefix=original.slice(0,at);suffix=original.slice(end);original=original.slice(at,end);}assert.equal(original.split(before).length,2,label);const file=dir+(kind.startsWith("codec")?"/codec.h":"/proxy.c");fs.writeFileSync(file,prefix+original.replace(before,after)+suffix);const codecOnly=kind==="codec",test=codecOnly?"ddraw_bc_codec_test.js":"ddraw_compressed_blt_test.js",key=kind.startsWith("codec")?"DDRAW_BC_CODEC_HEADER":"DDRAW_BC_BLT_SOURCE";const run=spawnSync(process.execPath,[__dirname+"/"+test],{env:{...process.env,[key]:file},encoding:"utf8"});assert.notEqual(run.status,0,"survived "+label);assert.match(run.stderr,/BC (?:codec|blit) line [0-9]+:/,"strict compilation and runtime assertion required: "+label+"\n"+run.stderr.slice(-2200));assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(run.stderr),run.stderr.slice(-2200));console.log("KILL "+label);}console.log("PASS compressed blit/codec faults: "+cases.length+"/"+cases.length);}finally{fs.rmSync(dir,{recursive:true,force:true});}
