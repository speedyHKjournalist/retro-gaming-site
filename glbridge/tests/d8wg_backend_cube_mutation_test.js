"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),source=fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"backend8-cube-mut-"));
function body(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return[m.index,b,s.slice(m.index,b)];}
const cases=[
 ["API8 failed lock output","cube_lock_rect","ZeroMemory(locked_rect, sizeof(*locked_rect));","(void)locked_rect;","lock"],
 ["WRITEONLY rejected","create_cube_texture_object","if ((usage & D3DUSAGE_WRITEONLY)","if ((0 && (usage & D3DUSAGE_WRITEONLY))"],
 ["DXT top-level alignment", "create_cube_texture_object", "block_width == 4u", "block_width == 400u"],
 ["creation mip metadata","emit_cube_texture_create","command.level_count = texture->level_count;","command.level_count = 1;"],
 ["reserved metadata","emit_cube_texture_create","command.reserved = 0;","command.reserved = 4;"],
 ["unknown autogen bit","emit_cube_texture_create","device_compatibility_api(device) == 8u","device_compatibility_api(device) == 7u"],
 ["profile","d9wg_d3d9_core_cube_texture8","!= 8u","!= 9u"],
 ["dispatch","d9wg_d3d9_core_cube_texture8","pool, 8u, out","pool, 9u, out"],
 ["edge ceiling","create_cube_texture_object","edge > 4096","edge > 8192"],
 ["API8 format","create_cube_texture_object","!supported_format8(format, FALSE)","(!supported_format8(format, FALSE) && format != 113u)"],
 ["depth usage","create_cube_texture_object","|| (usage & D3DUSAGE_DEPTHSTENCIL)","|| ((usage & D3DUSAGE_DEPTHSTENCIL) && 0)"],
 ["RT pool","create_cube_texture_object","&& (pool != D3DPOOL_DEFAULT","&& (pool > D3DPOOL_SCRATCH"],
 ["mip bound","create_cube_texture_object","if (levels > full_levels)","if (levels > full_levels && 0)"],
 ["failed output","create_cube_texture_object","*texture_out = NULL;","(void)texture_out;"],
 ["resource list","create_cube_texture_object","device->cube_textures = texture;","(void)texture;"],
 ["autogen filter","create_cube_texture_object","if (api == 9u && (usage & D3DUSAGE_AUTOGENMIPMAP))","if (usage & D3DUSAGE_AUTOGENMIPMAP)"],
 ["allocation face/mip index","create_cube_texture_object","face * levels + level","face * levels"],
 ["lock API","cube_lock_rect","device_compatibility_api(texture->device),","9u,","lock"],
 ["upload API","emit_cube_texture_update","device_compatibility_api(texture->device)","9u","lock"],
 ["read only","cube_unlock_rect","if (!(level_data->lock_flags & D3DLOCK_READONLY))","if (1)","lock"],
 ["unlock cleanup","cube_unlock_rect","level_data->locked = FALSE;","level_data->locked = TRUE;","lock"],
 ["lock face/mip index","cube_lock_rect","(UINT)face * texture->level_count + level","level","lock"],
 ["upload face/mip index","emit_cube_texture_update","face * texture->level_count + level","level","lock"],
 ["upload face selector","emit_cube_texture_update","level, face, rect","level, 0, rect","lock"],
 ["upload mip selector","emit_cube_texture_update","level, face, rect","0, face, rect","lock"],
 ["API8 BC alignment","cube_lock_rect","device_compatibility_api(texture->device) == 8u && block_width > 1u","device_compatibility_api(texture->device) == 7u && block_width > 1u","lock"],
 ["ordinary9 BC behavior","cube_lock_rect","device_compatibility_api(texture->device) == 8u && block_width > 1u","device_compatibility_api(texture->device) <= 9u && block_width > 1u","lock"],
 ["subrect pitch","emit_level_update_api","rows.source_pitch = level_data->row_pitch;","rows.source_pitch = rows.row_bytes;","lock"],
 ["upload failure","cube_unlock_rect","return result ? D3D_OK : D3DERR_DRIVERINTERNALERROR;","return result ? D3D_OK : D3D_OK;","lock"]
];
try{for(const[label,name,from,to,kind]of cases){const[a,b,f]=body(source,name);assert(f.includes(from),label);const file=dir+"/frontend.c";fs.writeFileSync(file,source.slice(0,a)+f.replace(from,to)+source.slice(b));const test="d8wg_backend_cube"+(kind==="lock"?"_lock":"")+"_test.js",r=spawnSync(process.execPath,[root+"/tests/"+test],{encoding:"utf8",timeout:180000,env:{...process.env,D8WG_BACKEND_CUBE_SOURCE:file}}),out=r.stdout+r.stderr;assert(r.status!==0&&!r.signal,label+" survived/abnormal "+out);assert(!/error:|undefined reference|error generated|errors generated/.test(out),label+" compiler failure is not a runtime mutation kill: "+out);assert(/Backend8 cube(?: lock)? case \d+ line \d+:/.test(out),label+" did not fail actual C semantic assertion: "+out);}
 console.log(`PASS backend8 cube mutations: ${cases.length}/${cases.length};strict compilation then actual constructor/lock/upload runtime assertions,profile/bounds/formats/mips/pools/output/list/storage/read-only/pitch/status guards`);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
