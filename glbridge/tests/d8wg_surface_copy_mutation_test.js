"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-surface-mut-")),source=readComparisonSource(root+"/d3d8proxy/d3d8_proxy.c","utf8");
const rect=readComparisonSource(root+"/runtime/d9wg_copy_rects_bridge8.h","utf8");
const shared=readComparisonSource(root+"/runtime/d9wg_backbuffer_upload.h","utf8"),front=readComparisonSource(root+"/runtime/d9wg_front_buffer_bridge8.h","utf8");
function span(source,name){const m=new RegExp("static [^;{}]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return[m.index,end];}
const mutations=[
 ["destination overflow guard removed","copy_surface_rect","(UINT)point.x > to.Width || ",""],
 ["valid destination edge refused","copy_surface_rect","(UINT)(area.bottom - area.top) > to.Height - (UINT)point.y","(UINT)(area.bottom - area.top) >= to.Height - (UINT)point.y"],
 ["source boundary ignored","copy_surface_rect","(UINT)area.right > from.Width || ",""],
 ["GPU path falls through CPU mirror","copy_surface_rect","if (from.Pool == D3DPOOL_DEFAULT","if (0 && from.Pool == D3DPOOL_DEFAULT"],
 ["wrong GPU owner","copy_surface_rect","copy.device_handle = surface_device(source)->handle;","copy.device_handle = 0;"],
 ["wrong cube face","surface_gpu_location","*face = surface->face;","*face = 0;"],
 ["wrong cube CPU face","surface_texture_level","(D3DCUBEMAP_FACES)surface->face","(D3DCUBEMAP_FACES)0"],
 ["GPU write keeps stale mirror valid","copy_surface_rect","to_level->gpu_dirty = TRUE;","to_level->gpu_dirty = FALSE;"],
 ["skip source refresh","copy_surface_rect","HRESULT result = refresh_surface_pixels(source);","HRESULT result = D3D_OK;"],
 ["ignore failed source refresh","copy_surface_rect","if (FAILED(result))","if (0)"],
 ["CPU pools overwritten from GPU","refresh_surface_pixels","desc.Pool != D3DPOOL_DEFAULT || ",""],
 ["ignore GPU-written ordinary texture","refresh_surface_pixels","!(level && level->gpu_dirty)","TRUE"],
 ["refreshed mirror stays dirty","refresh_surface_pixels","level->gpu_dirty = FALSE;","level->gpu_dirty = TRUE;"],
 ["skip lock refresh","lock_surface_texture_level","result = refresh_surface_pixels(surface);","result = ((void)surface, D3D_OK);"],
 ["failed lock remains active","lock_surface_texture_level","level->locked = FALSE;","level->locked = TRUE;"],
 ["failed lock overwrites caller output","lock_surface_texture_level","level->locked = FALSE;","*out = locked; level->locked = FALSE;"],
 ["mandatory submission failure masked","submit_readback_batch_locked","return d9wg_transport_submit(g_process, present);","(void)present; return TRUE;"],
 ["transient back-buffer destination offset","d9wg_renderable_upload","copy.destination_top = area->top;","copy.destination_top = 0;"],
 ["transient upload ignores source stride","d9wg_renderable_upload","rows.source_pitch = pitch;","rows.source_pitch = ((void)pitch, rows.row_bytes);"],
 ["transient texture cleanup opcode","d9wg_renderable_upload","D9WG_OP_DESTROY_RESOURCE","D9WG_OP_SET_TEXTURE"],
 ["CPU upload omits rectangle origin","upload_surface_rect","pixels + area->top * pitch + area->left * pixel_bytes","pixels"],
 ["overlap uses live source","copy_surface_rect","if (input == output)","if (0)"],
 ["copy loses last byte","copy_surface_rect","UINT row_bytes = (area.right - area.left) * pixel_bytes","UINT row_bytes = (area.right - area.left) * pixel_bytes - 1"],
 ["zero-count bypasses format guard","device_copy_rects","source_desc.Format != destination_desc.Format || ",""],
 ["omitted points rejected","device_copy_rects","(count && !rects)","(count && (!rects || !points))"],
 ["foreign device accepted","device_copy_rects","if (surface_device(source) != device || surface_device(destination) != device)","if ((void)device, 0)"],
 ["front alpha stays transparent","device_get_front_buffer","output[y * destination_pitch + x * 4u + 3u] = 255;","output[y * destination_pitch + x * 4u + 3u] = 0;"],
 ["readonly Unlock uploads","unlock_texture_level","if (!(level_data->lock_flags & D3DLOCK_READONLY))","if (1)"],
 ["partial upload clears unknown pixels","copy_surface_rect","if (to_level && target.left == 0 && target.top == 0 && (UINT)target.right == to.Width && (UINT)target.bottom == to.Height)","if (to_level)"],
];
function rectText(s){s=s.split("copy_surface_rect").join("d9wg_copy_rect8_common");s=s.split("D8Surface").join("D9WG_RECT_SURFACE");s=s.split("D8TextureLevel").join("D9WG_RECT_LEVEL");s=s.split("IDirect3DSurface8").join("D9WG_RECT_IFACE");s=s.split("D3DSURFACE_DESC").join("D9WG_RECT_DESC");s=s.split("D8WG_TRACE_ERROR").join("D9WG_RECT_TRACE");s=s.split("surface_device").join("D9WG_RECT_OWNER");s=s.split("surface_get_desc").join("D9WG_RECT_GET_DESC");s=s.split("texture_format_layout").join("D9WG_RECT_LAYOUT");s=s.split("surface_gpu_location").join("D9WG_RECT_LOCATION");s=s.split("refresh_surface_pixels").join("D9WG_RECT_REFRESH");s=s.split("surface_pixels").join("D9WG_RECT_PIXELS");s=s.split("upload_surface_rect").join("D9WG_RECT_UPLOAD");s=s.split("source->depth_stencil").join("D9WG_RECT_DEPTH(source)");s=s.split("destination->depth_stencil").join("D9WG_RECT_DEPTH(destination)");s=s.split("to_level->gpu_dirty = TRUE").join("D9WG_RECT_DIRTY(to_level, TRUE)");s=s.split("to_level->gpu_dirty = FALSE").join("D9WG_RECT_DIRTY(to_level, FALSE)");return s;}
try{let killed=0;for(const[name,fn,old,replacement]of mutations){const isShared=fn==="d9wg_renderable_upload",isFront=fn==="device_get_front_buffer",isRect=fn==="copy_surface_rect"||fn==="device_copy_rects",original=isRect?rect:isFront?front:isShared?shared:source;const[a,b]=span(original,isRect?(fn==="copy_surface_rect"?"d9wg_copy_rect8_common":"d9wg_copy_rects8_common"):isFront?"d9wg_front_buffer8_common":fn),body=original.slice(a,b);assert(body.includes(isRect?rectText(old):old),name+" source match");const file=dir+"/candidate.c";let candidate=original.slice(0,a)+body.replace(isRect?rectText(old):old,isRect?rectText(replacement):replacement)+original.slice(b);if(isShared)candidate=candidate.replace(/#include "([^"]+)"/g,(_,p)=>'#include "'+path.resolve(root,"runtime",p)+'"');fs.writeFileSync(file,candidate);const r=spawnSync(process.execPath,[__dirname+"/d8wg_surface_copy_test.js"],{encoding:"utf8",timeout:20000,env:{...process.env,[isRect?"D8WG_RECT_BRIDGE_HEADER":isFront?"D8WG_FRONT_BRIDGE_HEADER":isShared?"D8WG_SURFACE_UPLOAD_HEADER":"D8WG_SURFACE_SOURCE"]:file}});assert(!r.error,name+": "+r.error);assert.notEqual(r.status,0,name+" survived");const output=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+" failed compile: "+output);assert(/D8 surface line \d+|Assertion failed: \([^\n]*\), function \w+, file surface\.c, line \d+\.|surface\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(output),name+" no actual C assertion: "+output);killed++;}console.log("PASS D3D8 surface mutations: "+killed+"/"+mutations.length+"; GPU identity, mirrors, exact CPU staging and overlap, lock/output/error semantics, CPU pools, bounds and opaque front readback");}finally{fs.rmSync(dir,{recursive:true,force:true});}
