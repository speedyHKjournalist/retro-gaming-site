"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"device8-mutations-"));
const groups=[{
    file:"d3d8proxy/d3d8_core_device.c",env:"D8WG_DEVICE_FRONTEND_SOURCE",test:"d8wg_device_frontend_lifetime_test.js",
    mutations:[
        ["reused wrapper not cleared","ZeroMemory(storage + 1, bytes);","(void)bytes;"],
        ["wrong-sized wrapper reused","while (*link && (*link)->bytes != bytes)","while (*link && (*link)->bytes == 0)"],
        ["wrapper cache not drained","while (device->retired_views)","while (0 && device->retired_views)"],
        ["wrapper not cached","device->retired_views = storage;","(void)storage;"],
        ["missing aggregate core pin","if (refs == 1) IUnknown_AddRef((IUnknown *)device->backend);","if (refs == 0) IUnknown_AddRef((IUnknown *)device->backend);"],
        ["repeated aggregate core pin","if (refs == 1) IUnknown_AddRef((IUnknown *)device->backend);","if (refs >= 1) IUnknown_AddRef((IUnknown *)device->backend);"],
        ["missing aggregate release","if (!refs) IUnknown_Release((IUnknown *)device->backend);","if (refs == 9999) IUnknown_Release((IUnknown *)device->backend);"],
        ["missing in-flight metadata pin","InterlockedIncrement(&device->metadata_refs);","(void)device->metadata_refs;"],
        ["missing in-flight metadata drop","core_device8_metadata_release(device);\n    return refs;","(void)device;\n    return refs;"],
        ["missing last-direct cleanup","refs == (ULONG)device->children && !device->cleaning","refs == (ULONG)device->children + 9999u && !device->cleaning"],
        ["missing child accounting","InterlockedIncrement(&core_device8(iface)->children);","(void)core_device8(iface)->children;"],
        ["missing child release accounting","InterlockedDecrement(&core_device8(iface)->children);","(void)core_device8(iface)->children;"],
        ["parent pin leak","IDirect3D8_Release(parent);","(void)parent;"],
        ["missing private shader hold release","d9wg_d3d9_core_shader_storage_release8(device->backend, shader->program);","(void)shader->program;"],
        ["fixture AddRef result","return IDirect3DDevice8_AddRef(iface);","return IDirect3DDevice8_AddRef(iface) + 1;"],
        ["fixture Release result","return IDirect3DDevice8_Release(iface);","return IDirect3DDevice8_Release(iface) + 1;"],
    ],
},{
    file:"d3d9proxy/d3d9_proxy.c",env:"D8WG_DEVICE_COMMON_SOURCE",test:"d8wg_device_common_state_test.js",
    mutations:[
        ["profile guard","device_compatibility_api(device) != 8 || !dst","device_compatibility_api(device) == 0 || !dst"],
        ["type guard","type > D3DSBT_VERTEXSTATE) return D3DERR_INVALIDCALL;","type > 99) return D3DERR_INVALIDCALL;"],
        ["VS scope too wide","i < 96; ++i) dst->has_vs_const_f[i]","i < 97; ++i) dst->has_vs_const_f[i]"],
        ["PS scope too wide","i < 8; ++i) dst->has_ps_const_f[i]","i < 9; ++i) dst->has_ps_const_f[i]"],
        ["texture scope too wide","i < 8; ++i) dst->has_texture[i]","i < 9; ++i) dst->has_texture[i]"],
        ["last stream omitted","i < 16; ++i) dst->has_stream[i]","i < 15; ++i) dst->has_stream[i]"],
        ["clip plane scope incomplete","i < 6; ++i) dst->has_clip_plane[i]","i < 5; ++i) dst->has_clip_plane[i]"],
        ["incremental indices omitted","COPY_MASK(has_indices);","(void)src->has_indices;"],
        ["incremental sampler omitted","COPY_MASK(has_sampler_state);","(void)src->has_sampler_state;"],
        ["incremental lights collapsed","entry->enable_mask = light->enable_mask;","entry->enable_mask = light->light_mask;"],
        ["uninitialized sparse light captured","if (!light->set) continue;","if (light->index == 0) continue;"],
        ["snapshot engine skipped","state_block_capture(dst);\n    return D3D_OK;","if (type == 99) state_block_capture(dst);\n    return D3D_OK;"],
        ["Reset epoch stale","*epoch = device->reset_epoch;","*epoch = device->reset_epoch + 1;"],
        ["recording observation inverted","*recording = device->recording_result != NULL;","*recording = device->recording_result == NULL;"],
        ["auto-depth observation lost","*auto_depth = device->present.EnableAutoDepthStencil;","*auto_depth = !device->present.EnableAutoDepthStencil;"],
    ],
}];
try{let count=0;
for(const group of groups){const source=fs.readFileSync(root+"/"+group.file,"utf8");
 for(const [name,from,to] of group.mutations){assert(source.includes(from),name+" anchor");const candidate=dir+"/candidate.c";const start=group.env==="D8WG_DEVICE_COMMON_SOURCE"?source.indexOf("HRESULT WINAPI d9wg_d3d9_core_record_common_state8("):0;fs.writeFileSync(candidate,source.slice(0,start)+source.slice(start).replace(from,to));
  const result=spawnSync(process.execPath,[__dirname+"/"+group.test],{encoding:"utf8",timeout:30000,env:{...process.env,[group.env]:candidate}});
  assert(!result.error,name+": "+result.error);assert.notEqual(result.status,0,name+" survived");
  const output=result.stdout+result.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+" compiler failure: "+output);
  assert(/Assertion failed: \([^\n]*\), function \w+, file test\.c, line \d+\.|test\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(output),name+" needs actual C assertion: "+output);++count;
 }
}
console.log("PASS Device8 frontend mutations: "+count+" strictly compiled lifetime/scope/Reset faults rejected by actual C assertions");
}finally{fs.rmSync(dir,{recursive:true,force:true});}
