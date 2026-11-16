"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"state-replay-mut-"));
const core=fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8"),front=fs.readFileSync(root+"/d3d8proxy/d3d8_core_device.c","utf8");
const cases=[];
const start=core.indexOf("static HRESULT state_block_apply_shared("),end=core.indexOf("static HRESULT state_block_apply(",start),body=core.slice(start,end);
for(const name of ["SetRenderState","SetTextureStageState","SetTexture","SetSamplerState","SetTransform","SetMaterial","SetClipPlane","SetNPatchMode","SetLight","LightEnable","SetViewport","SetScissorRect","SetVertexShader","SetPixelShader","SetVertexShaderConstantF","SetPixelShaderConstantF","SetVertexShaderConstantI","SetPixelShaderConstantI","SetVertexShaderConstantB","SetPixelShaderConstantB","SetVertexDeclaration","SetFVF","SetStreamSourceFreq"]){
 const marker="APPLY_STATE(IDirect3DDevice9_"+name+"(",at=body.indexOf(marker);assert(at>=0,name);
 let i=at+"APPLY_STATE(".length,depth=1;for(;depth;i++){if(body[i]==="(")depth++;if(body[i]===")")depth--;}
 const old=body.slice(at,i),replacement=old.slice("APPLY_STATE(".length,-1);
 cases.push(["ignored "+name,"core",core.slice(0,start)+body.replace(old,replacement)+core.slice(end)]);
}
function change(label,which,old,replacement){const source=which==="core"?core:front;assert(source.includes(old),label);cases.push([label,which,source.replace(old,replacement)]);}
change("palette failure swallowed","core","if (FAILED(hr)) return hr;\n    }\n    if (apply8) APPLY_STATE", "if (0 && FAILED(hr)) return hr;\n    }\n    if (apply8) APPLY_STATE");
change("index failure swallowed","core","if (FAILED(result)) return result;\n    }\n#undef APPLY_STATE", "if (0 && FAILED(result)) return result;\n    }\n#undef APPLY_STATE");
change("index notification lost","core","if (apply8) result = apply8(context, D9WG_APPLY8_INDICES, 0, result);","if (0 && apply8) result = apply8(context, D9WG_APPLY8_INDICES, 0, result);");
change("API8 double sampler replay","core","if (!apply8) for (slot = 0;","for (slot = 0;");
change("API8 double declaration replay","core","if (!apply8 && block->has_vertex_format)","if (block->has_vertex_format)");
change("API8 render override omitted","core","if (apply8) APPLY_STATE(apply8(context, D9WG_APPLY8_RENDER, 0, D3D_OK));","if (0 && apply8) APPLY_STATE(apply8(context, D9WG_APPLY8_RENDER, 0, D3D_OK));");
change("raw state discarded on committed emit failure","front","if (SUCCEEDED(hr) || hr == D3DERR_DRIVERINTERNALERROR) {\n        raw->render_mask", "if (SUCCEEDED(hr)) {\n        raw->render_mask");
change("raw TSS discarded on committed emit failure","front","if (SUCCEEDED(hr) || hr == D3DERR_DRIVERINTERNALERROR) {\n        raw->texture_mask", "if (SUCCEEDED(hr)) {\n        raw->texture_mask");
change("index base loses sign bit","front","device->live.base = raw->base;","device->live.base = raw->base & 0x7fffffffu;");
change("index base updated on rejected call","front","raw->indices_mask && (SUCCEEDED(result) || result == D3DERR_DRIVERINTERNALERROR)","raw->indices_mask");
change("raw render prefix continues after failure","front","if (FAILED(hr)) return hr;\n        }\n        break;\n    case D9WG_APPLY8_TEXTURE_STAGE:","if (0 && FAILED(hr)) return hr;\n        }\n        break;\n    case D9WG_APPLY8_TEXTURE_STAGE:");
change("raw TSS prefix continues after failure","front","if (FAILED(hr)) return hr;\n        }\n        break;\n    case D9WG_APPLY8_VERTEX:","if (0 && FAILED(hr)) return hr;\n        }\n        break;\n    case D9WG_APPLY8_VERTEX:");
change("detached shader refs not retained","front","core_raw8_capture(&raw, &block->raw);","if (0) core_raw8_capture(&raw, &block->raw);");
change("apply device pin missing","front","core_device8_AddRef(iface);\n    HRESULT hr = d9wg_d3d9_core_apply_state_block_frontend8","if (0) core_device8_AddRef(iface);\n    HRESULT hr = d9wg_d3d9_core_apply_state_block_frontend8");
change("apply device pin leaked","front","core_device8_Release(iface);\n    return hr;\n}\nstatic HRESULT WINAPI core_device8_DeleteStateBlock", "if (0) core_device8_Release(iface);\n    return hr;\n}\nstatic HRESULT WINAPI core_device8_DeleteStateBlock");
try{for(const[label,which,source]of cases){const file=dir+"/candidate.c";fs.writeFileSync(file,source);const run=spawnSync(process.execPath,[__dirname+"/"+(which==="core"?"d9wg":"d8wg")+"_state_replay_failure_test.js"],{encoding:"utf8",env:{...process.env,[which==="core"?"D9WG_REPLAY_SOURCE":"D8WG_REPLAY_SOURCE"]:file}});const output=run.stdout+run.stderr;assert(!run.error,label);assert.notEqual(run.status,0,"survived "+label);assert(!/error:|undefined reference|Undefined symbols|errors generated/.test(output),"compile failure is not a kill: "+label+"\n"+output);assert.match(output,/state replay failure line|raw8 replay failure line|Assertion.*failed|Assertion failed/,label+" requires actual C runtime assertion\n"+output);console.log("KILL "+label);}console.log("PASS state replay mutations: "+cases.length+"/"+cases.length+", strict compilation then native runtime failures");}finally{fs.rmSync(dir,{recursive:true,force:true});}
