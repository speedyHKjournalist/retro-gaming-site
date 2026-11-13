"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(root+"/ddrawproxy/ddraw_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-descriptor-mut-"));
const cases=[
 ["legacy D24 keeps wire packing","desc1_to_desc2","to->ddpfPixelFormat.dwZBitMask = 0x00ffffffu;","to->ddpfPixelFormat.dwZBitMask = 0xffffff00u;"],
 ["unused legacy format bytes leak into synthesized depth","desc1_to_desc2","ZeroMemory(&to->ddpfPixelFormat, sizeof(to->ddpfPixelFormat));",""],
 ["live primary format omitted","legacy_describe","ddraw_platform_primary_format(surface->owner, &current.ddpfPixelFormat);","(void)&ddraw_platform_primary_format;"],
 ["modern shape returns stale primary format","legacy_describe","*(DDSURFACEDESC2 *)description = current;","*(DDSURFACEDESC2 *)description = surface->desc;"],
 ["legacy depth keeps pixel-format flag","legacy_describe","narrow->dwFlags & ~DDSD_PIXELFORMAT","narrow->dwFlags | DDSD_PIXELFORMAT"],
 ["legacy depth union erased","legacy_describe","narrow->dwZBufferBitDepth = current.ddpfPixelFormat.dwZBufferBitDepth;","narrow->dwZBufferBitDepth = 0;"],
 ["legacy depth mapping omitted","legacy_describe","if (current.ddpfPixelFormat.dwFlags & DDPF_ZBUFFER)","if (0 && (current.ddpfPixelFormat.dwFlags & DDPF_ZBUFFER))"],
 ["current format overwrites allocation metadata","legacy_describe","    if (narrow->dwSize ==","    surface->desc.ddpfPixelFormat = current.ddpfPixelFormat;\n    if (narrow->dwSize =="],
 ["legacy depth leaks into modern mip union","desc1_to_desc2","to->dwMipMapCount = 0;","to->dwMipMapCount = from->dwMipMapCount;"],
 ["legacy depth input loses both pixel-format aliases","desc1_to_desc2","to->ddpfPixelFormat.dwZBufferBitDepth = from->dwZBufferBitDepth;\n        to->ddpfPixelFormat.dwRGBBitCount = from->dwZBufferBitDepth;","to->ddpfPixelFormat.dwZBufferBitDepth = 0;"],
];
function body(name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,d=1;for(;d;end++){if(source[end]==="{")d++;if(source[end]==="}")d--;}return{start:m.index,end,text:source.slice(m.index,end)};}
try{for(const[label,name,from,to]of cases){const b=body(name);assert.equal(b.text.split(from).length,2,label);const file=dir+"/mutation.c";fs.writeFileSync(file,source.slice(0,b.start)+b.text.replace(from,to)+source.slice(b.end));const r=spawnSync(process.execPath,[__dirname+"/ddraw_descriptor_bridge_test.js"],{encoding:"utf8",env:{...process.env,DDRAW_DESCRIPTOR_SOURCE:file}});assert.notEqual(r.status,0,label);assert.match(r.stderr,/descriptor line \d+:/,label+": strict compile and runtime rejection required");assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(r.stderr),r.stderr);console.log("KILL "+label);}console.log("PASS descriptor mutations: "+cases.length+"/"+cases.length+" strict compiled implementations rejected at runtime");}finally{fs.rmSync(dir,{recursive:true,force:true});}
