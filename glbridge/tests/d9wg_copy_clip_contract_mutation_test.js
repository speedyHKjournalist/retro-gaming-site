"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),os=require("node:os"),path=require("node:path"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-copy-clip-mut-"));
const cases=[
 ["source owner","device_stretch_rect","source->device != device || destination->device != device","destination->device != device"],
 ["destination pool","device_stretch_rect","|| destination->pool != D3DPOOL_DEFAULT",""],
 ["source pool","device_stretch_rect","source->pool != D3DPOOL_DEFAULT || ",""],
 ["filter","device_stretch_rect","&& filter != D3DTEXF_LINEAR","&& filter != D3DTEXF_LINEAR && filter != 3"],
 ["texture target","device_stretch_rect","destination->texture_child || source->texture_child","source->texture_child"],
 ["RT to plain","device_stretch_rect","|| (source_usage & D3DUSAGE_RENDERTARGET)",""],
 ["API8 profile","device_stretch_rect","device_compatibility_api(device) == 9u","device_compatibility_api(device) >= 8u"],
 ["set alias","device_set_clip_plane","index = D9_MAX_CLIP_PLANES - 1u;","index = D9_MAX_CLIP_PLANES - 2u;"],
 ["get alias","device_get_clip_plane","index = D9_MAX_CLIP_PLANES - 1u;","index = D9_MAX_CLIP_PLANES - 2u;"],
 ["API8 clip profile","device_set_clip_plane","device_compatibility_api(device) == 9u","device_compatibility_api(device) >= 8u"],
 ["recorded isolation","device_set_clip_plane","if (state_block_record_clip_plane(device, index, plane)) return D3D_OK;","if (state_block_record_clip_plane(device, index, plane)) {}"]
];
try{for(const[label,name,from,to]of cases){const start=source.indexOf("static HRESULT WINAPI "+name+"("),end=source.indexOf("\n}",start)+2,body=source.slice(start,end);assert(start>=0&&body.includes(from),label);const file=dir+"/proxy.c";fs.writeFileSync(file,source.slice(0,start)+body.replace(from,to)+source.slice(end));const r=spawnSync(process.execPath,[root+"/tests/d9wg_copy_clip_contract_test.js"],{encoding:"utf8",timeout:30000,env:{...process.env,D9WG_COPY_CLIP_SOURCE:file}});assert(r.status!==0&&!r.signal&&!/error:/.test(r.stdout+r.stderr)&&/Copy\/clip contract line/.test(r.stderr),label+": "+r.stdout+r.stderr);}console.log(`PASS copy/clip mutations: ${cases.length}/${cases.length}; real C contract assertions, strict compilation, ASan/UBSan`);}finally{fs.rmSync(dir,{recursive:true,force:true});}
