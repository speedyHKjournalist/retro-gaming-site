"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),source=fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"backend8-volume-mut-"));
function body(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return[m.index,b,s.slice(m.index,b)];}
const cases=[
 ["creation mip metadata","emit_volume_texture_create","command.level_count = texture->level_count;","command.level_count = 1;"],
 ["creation depth metadata","emit_volume_texture_create","command.depth = texture->depth;","command.depth = 1;"],
 ["profile","d9wg_d3d9_core_volume_texture8","!= 8u","!= 9u"],
 ["dispatch","d9wg_d3d9_core_volume_texture8","pool, 8u, out","pool, 9u, out"],
 ["width ceiling","create_volume_texture_object","width > 2048","width > 4096"],
 ["height ceiling","create_volume_texture_object","height > 2048","height > 4096"],
 ["depth ceiling","create_volume_texture_object","depth > 2048","depth > 4096"],
 ["API8 BC refusal","supported_volume_texture_format8","&& format != D3DFMT_DXT1","&& (format != D3DFMT_DXT1 || 1)"],
 ["target usage","create_volume_texture_object","|| (usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))","|| ((usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) && 0)"],
 ["mip bound","create_volume_texture_object","if (levels > full_levels)","if (levels > full_levels && 0)"],
 ["failed output","create_volume_texture_object","*out = NULL;","(void)out;"],
 ["resource list","create_volume_texture_object","device->volume_textures = texture;","(void)texture;"],
 ["lazy8 view allocation","create_volume_texture_object","if (api == 9u)","if (api >= 8u)"],
 ["API8 shadow OOM","create_volume_texture_object","if (api == 8u && !level_data->shadow)","if (api == 8u && !level_data->shadow && 0)"],
 ["lock API","volume_texture_lock_box","device_compatibility_api(texture->device),","9u,","lock"],
 ["upload API","emit_volume_texture_update","device_compatibility_api(texture->device) == 8u","device_compatibility_api(texture->device) == 7u","lock"],
 ["read only","volume_texture_unlock_box","if (!(level_data->lock_flags & D3DLOCK_READONLY))","if (1)","lock"],
 ["unlock cleanup","volume_texture_unlock_box","level_data->locked = FALSE;","level_data->locked = TRUE;","lock"],
 ["slice source","emit_volume_texture_update","z * level_data->slice_pitch","0u * level_data->slice_pitch","lock"],
 ["slice selector","emit_volume_texture_update","&slice, level, z, &rect","&slice, level, 0, &rect","lock"],
 ["API8 box alignment","volume_texture_lock_box","device_compatibility_api(texture->device) == 8u","device_compatibility_api(texture->device) == 7u","lock"],
 ["ordinary9 box alignment","volume_texture_lock_box","device_compatibility_api(texture->device) == 8u","device_compatibility_api(texture->device) <= 9u","lock"],
 ["box front offset","volume_texture_lock_box","area.Front * level_data->slice_pitch","0u * level_data->slice_pitch","lock"],
 ["slice pitch","volume_texture_lock_box","locked->SlicePitch = (INT)level_data->slice_pitch;","locked->SlicePitch = (INT)level_data->row_pitch;","lock"],
 ["upload failure","volume_texture_unlock_box","return result ? D3D_OK : D3DERR_DRIVERINTERNALERROR;","return result ? D3D_OK : D3D_OK;","lock"],
 ["descriptor size guard","d9wg_d3d9_core_volume_desc8","bytes != D9WG_D3D8_VOLUME_DESC_BYTES","bytes == 0u","desc"],
 ["descriptor depth factor","core_pack_volume_desc8","slice * desc->Depth, desc->Width","slice, desc->Width","desc"],
 ["descriptor overflow","core_pack_volume_desc8","(uint64_t)slice * desc->Depth > UINT32_MAX","((uint64_t)slice * desc->Depth > UINT32_MAX && 0)","desc"],
 ["descriptor provider error","d9wg_d3d9_core_volume_desc8","if (FAILED(hr)) return hr;","if (FAILED(hr)) return D3DERR_INVALIDCALL;","desc"],
 ["descriptor width","core_pack_volume_desc8","!desc->Width ||","(!desc->Width && 0) ||","desc"],
 ["view level identity","volume_texture_get_volume_level","texture->levels[level].level_volume","texture->levels[0].level_volume","view"],
 ["view public ref","volume_texture_get_volume_level","IDirect3DVolume9_AddRef(*out);","(void)out;","view"],
 ["view output versioning","volume_texture_get_volume_level","device_compatibility_api(texture->device) == 8u","device_compatibility_api(texture->device) <= 9u","view"]
];
try{for(const[label,name,from,to,kind]of cases){const[a,b,f]=body(source,name);assert(f.includes(from),label);const file=dir+"/frontend.c";fs.writeFileSync(file,source.slice(0,a)+f.replace(from,to)+source.slice(b));const test=kind==="desc"?"d8wg_volume_desc_bridge_test.js":kind==="view"?"d8wg_volume_view_test.js":"d8wg_backend_volume"+(kind==="lock"?"_lock":"")+"_test.js",r=spawnSync(process.execPath,[root+"/tests/"+test],{encoding:"utf8",timeout:180000,env:{...process.env,D8WG_BACKEND_VOLUME_SOURCE:file}}),out=r.stdout+r.stderr;assert(r.status!==0&&!r.signal,label+" survived/abnormal "+out);assert(!/error:|undefined reference|error generated|errors generated/.test(out),label+" compiler failure is not a runtime mutation kill: "+out);assert(/(?:Backend8 volume(?: lock)?|Volume desc bridge|Volume view) case \d+ line \d+:/.test(out),label+" did not fail actual C semantic assertion: "+out);}
 console.log(`PASS backend8 volume mutations: ${cases.length}/${cases.length};strict compilation then actual constructor/lock/upload runtime assertions,profile/bounds/formats/mips/pools/output/list/storage/read-only/pitch/status guards`);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
