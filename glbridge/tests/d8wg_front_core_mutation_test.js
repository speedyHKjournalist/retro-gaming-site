"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"front8-mut-")),source=fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8'),bridge=fs.readFileSync(root+'/runtime/d9wg_front_buffer_bridge8.h','utf8');
function body(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return[m.index,b,s.slice(m.index,b)];}
const cases=[
 ['private profile','core','d9wg_d3d9_core_get_front_buffer8','!= 8u','!= 9u'],
 ['source device','core','readback_front8','request.device_handle = surface->device->handle;','request.device_handle = 0;'],
 ['read format','core','readback_front8','request.format = format;','request.format = surface->format;'],
 ['read width','core','readback_front8','request.width = surface->width;','request.width = surface->width + 1u;'],
 ['read capacity','core','readback_front8','pixels, bytes,','pixels, bytes + 1u,'],
 ['read HRESULT','core','readback_front8','result == D9WG_READBACK_INVALID','result != D9WG_READBACK_INVALID'],
 ['source row count','core','readback_front8','surface->width * pixel_bytes, surface->height','surface->width * pixel_bytes, surface->height + 1u'],
 ['allocation zero','core','front_pixels8','HEAP_ZERO_MEMORY, bytes','0, bytes'],
 ['level byte capacity','core','front_pixels8','level->byte_count);','level->byte_count + 1u);'],
 ['lazy API format','core','front_pixels8','d9wg_texture_level_layout(8u,','d9wg_texture_level_layout(9u,'],
 ['2D upload level','core','upload_front8','surface->texture, surface->level, area','surface->texture, surface->level + 1u, area'],
 ['cube face','core','upload_front8','surface->cube_face, surface->level, area','surface->cube_face ^ 1u, surface->level, area'],
 ['backbuffer target','core','upload_front8','format, 0, 0, 0,','format, 1, 0, 0,'],
 ['front owner','bridge','d9wg_front_buffer8_common','D9WG_FRONT_OWNER(destination) != device','(D9WG_FRONT_OWNER(destination) != device && 0)'],
 ['destination locked','bridge','d9wg_front_buffer8_common','destination->locked ||','(destination->locked && 0) ||'],
 ['level locked','bridge','d9wg_front_buffer8_common','(level && level->locked)','(level && level->locked && 0)'],
 ['depth','bridge','d9wg_front_buffer8_common','D9WG_FRONT_DEPTH(destination) ||','(D9WG_FRONT_DEPTH(destination) && 0) ||'],
 ['larger destination','bridge','d9wg_front_buffer8_common','desc.Width < source_desc.Width','desc.Width != source_desc.Width'],
 ['width bounds','bridge','d9wg_front_buffer8_common','desc.Width < source_desc.Width','(desc.Width < source_desc.Width && 0)'],
 ['force alpha','bridge','d9wg_front_buffer8_common','output[y * destination_pitch + x * 4u + 3u] = 255;','output[y * destination_pitch + x * 4u + 3u] = 0;'],
 ['row preservation','bridge','d9wg_front_buffer8_common','source_desc.Width * pixel_bytes);','source_desc.Width * pixel_bytes - 1u);'],
 ['read failure','bridge','d9wg_front_buffer8_common','if (SUCCEEDED(result))','if (1)'],
 ['dirty on upload failure','bridge','d9wg_front_buffer8_common','D9WG_FRONT_DIRTY(level, TRUE);','D9WG_FRONT_DIRTY(level, FALSE);'],
 ['partial validity','bridge','d9wg_front_buffer8_common','level && desc.Width == source_desc.Width && desc.Height == source_desc.Height','level'],
 ['source view release','bridge','d9wg_front_buffer8_common','D9WG_FRONT_RELEASE(source_iface);','(void)source_iface;']
];
try{for(const[label,kind,name,from,to]of cases){const original=kind==='core'?source:bridge,[a,b,f]=body(original,name);assert(f.includes(from),label);const file=dir+'/candidate.'+(kind==='core'?'c':'h');fs.writeFileSync(file,original.slice(0,a)+f.replace(from,to)+original.slice(b));const env={...process.env,[kind==='core'?'D8WG_FRONT_CORE_SOURCE':'D8WG_FRONT_BRIDGE_HEADER']:file},r=spawnSync(process.execPath,[root+'/tests/d8wg_front_core_test.js'],{env,encoding:'utf8',timeout:180000}),out=r.stdout+r.stderr;assert(r.status!==0&&!r.signal,label+' survived/abnormal '+out);assert(!/error:|undefined reference|Undefined symbols|errors generated/.test(out),label+' compiler failure is notruntime kill '+out);assert(/Front8 core case \d+ line \d+:/.test(out)||/Assertion failed: \([^\n]*\), function free_shadow, file front\.c, line \d+\./.test(out),label+' missing actual C assertion or exact allocation guard '+out);}
 console.log(`PASS shared/private API8 front mutations: ${cases.length}/${cases.length};strict compile then C assertions forprofiles/requests/formats/capacity/rowguards/alpha/dirtystate/views/CPUallocation/2D-cube-backbuffer upload`);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
