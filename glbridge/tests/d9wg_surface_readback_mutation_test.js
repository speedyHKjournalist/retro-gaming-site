"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-readback-mut-")),source=fs.readFileSync(root+"/runtime/d9wg_surface_readback.c","utf8");
const mutations=[
 ["wrong surface face","D9WGReadbackSurface command = *request;","D9WGReadbackSurface command = *request; command.face = 0;"],
 ["wrong mip","D9WGReadbackSurface command = *request;","D9WGReadbackSurface command = *request; command.level = 0;"],
 ["wrong request range","command.first_row = detail.first_row;","command.first_row = 0;"],
 ["wrong chunk rows","command.row_count = rows;","command.row_count = rows + 1;"],
 ["wrong chunk bytes","command.destination_bytes = detail.expected_bytes;","command.destination_bytes = detail.expected_bytes - 1;"],
 ["wrong response address","command.response_offset = D9WG_READBACK_REGION_OFFSET;","command.response_offset = 0;"],
 ["wrong destination offset","destination + detail.first_row * request->destination_pitch","destination"],
 ["omit last byte","sizeof(D9WGReadbackResponse), detail.expected_bytes);","sizeof(D9WGReadbackResponse), detail.expected_bytes - 1);"],
 ["accept stale id","detail.response_id != detail.request_id || ",""],
 ["accept failed status","detail.status != D9WG_RESPONSE_OK || ",""],
 ["accept wrong length","detail.actual_bytes != detail.expected_bytes","0"],
 ["zero request id","if (!id)\n            id = InterlockedIncrement(&process->response_request_id);",""],
 ["retain stale size","response->byte_count = 0;",""],
 ["retain reserved","response->reserved = 0;",""],
 ["no heartbeat renewal","start = GetTickCount();\n            }","(void)start;\n            }"],
 ["too short silence cap","D9WG_READBACK_SILENCE_MS)","5000u)"],
 ["unpin mapping while waiting","DWORD start = GetTickCount();","LeaveCriticalSection(&process->transport_lock); DWORD start = GetTickCount();"],
 ["reverse lock order","EnterCriticalSection(&process->readback_lock);\n    EnterCriticalSection(&process->transport_lock);","EnterCriticalSection(&process->transport_lock);\n    EnterCriticalSection(&process->readback_lock);"],
 ["ignore invalid pitch","request->destination_pitch < row_bytes || ",""],
 ["ignore destination capacity","row_count > destination_bytes / request->destination_pitch","destination_bytes == UINT32_MAX"],
 ["wrong accepted row count","detail.first_row += rows;","detail.first_row += detail.rows_per_chunk;"],
 ["mask response failure","result = D9WG_READBACK_RESPONSE;","result = D9WG_READBACK_OK;"],
 ["mask transport failure","result = D9WG_READBACK_TRANSPORT;\n    if (!d9wg_transport_open","result = D9WG_READBACK_OK;\n    if (!d9wg_transport_open"]
];
try{let killed=0;for(const[name,old,replacement]of mutations){assert.equal(source.split(old).length,2,name+" unique source");const file=dir+"/readback.c";fs.writeFileSync(file,source.replace(old,replacement));const r=spawnSync(process.execPath,[__dirname+"/d9wg_surface_readback_test.js"],{encoding:"utf8",timeout:20000,env:{...process.env,D9WG_READBACK_SOURCE:file}});assert(!r.error,name+": "+r.error);assert.notEqual(r.status,0,name+" survived");const output=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+" failed to compile: "+output);assert(/readback line|Assertion|signal=/.test(output),name+" no runtime failure");killed++;}const frontend=fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8"),first=frontend.indexOf("static BOOL submit_readback_batch_locked(BOOL present)\n{"),last=frontend.indexOf("static HRESULT WINAPI device_get_render_target_data",first),body=frontend.slice(first,last);
 const adapters=[
 ["mask mandatory submission failure","return d9wg_transport_submit(g_process, present);","(void)present; return TRUE;"],
 ["wrong frontend texture","request.texture_handle = rt->texture ? rt->texture->handle : 0;","request.texture_handle = 0;"],
 ["wrong frontend level","request.level = rt->level;","request.level = 0;"],
 ["wrong destination capacity","UINT destination_bytes = dst->shadow ? dst->byte_count : destination_level->byte_count;","UINT destination_bytes = UINT32_MAX;"],
 ["stale shadow fast path","source_level && source_level->shadow && source_level->shadow_valid","source_level && source_level->shadow"],
 ["ignore readback failure","if (result) {","if (0) {"],
 ["transparent front alpha","pixels[column * 4u + 3u] = 0xFFu;","pixels[column * 4u + 3u] = 0;"],
 ["missing destination validity","destination_level->shadow_valid = TRUE;","destination_level->shadow_valid = FALSE;"]
 ];
 for(const[name,old,replacement]of adapters){assert.equal(body.split(old).length,2,name);const file=dir+"/frontend.c";fs.writeFileSync(file,frontend.slice(0,first)+body.replace(old,replacement)+frontend.slice(last));const r=spawnSync(process.execPath,[__dirname+"/d9wg_surface_copy_readback_test.js"],{encoding:"utf8",timeout:20000,env:{...process.env,D9WG_READBACK_FRONTEND_SOURCE:file}});assert(!r.error,name+": "+r.error);assert.notEqual(r.status,0,name+" survived");const output=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+" failed to compile: "+output);assert(/surface copy line|Assertion|signal=/.test(output),name+" no runtime failure");killed++;}
 console.log("PASS shared readback mutations: "+killed+"/"+(mutations.length+adapters.length)+"; exact ranges/bytes, id/status/length, retained prefixes, invalid metadata, heartbeat deadlines, pinned lock order and actual frontend capacity/error/shadow/alpha handling");}finally{fs.rmSync(dir,{recursive:true,force:true});}
