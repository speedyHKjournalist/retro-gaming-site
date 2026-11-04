"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),sources={eight:fs.readFileSync(root+"/d3d8proxy/d3d8_proxy.c","utf8"),nine:fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8")},dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-caps-core-mut-"));
function mutate(source,name,before,after){const m=new RegExp("^[^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}const body=source.slice(m.index,end);assert(body.includes(before),name+" "+before);return source.slice(0,m.index)+body.replace(before,after)+source.slice(end);}
const devMask=sources.eight.match(/caps->DevCaps &= \([\s\S]*?\);/)[0],rasterMask=sources.eight.match(/caps->RasterCaps &= \([\s\S]*?\);/)[0];
const cases=[
    ["D3D8 bypasses shared table","eight","fill_caps","d9wg_d3d9_core_caps8(caps);","ZeroMemory(caps,sizeof(*caps));"],
    ["private prefix copy loses last word","nine","d9wg_d3d9_core_caps8","CopyMemory(caps8, &caps, D9WG_D3D8_CAPS_BYTES);","CopyMemory(caps8, &caps, D9WG_D3D8_CAPS_BYTES - 4);"],
    ["private prefix copy overwrites next word","nine","d9wg_d3d9_core_caps8","CopyMemory(caps8, &caps, D9WG_D3D8_CAPS_BYTES);","CopyMemory(caps8, &caps, D9WG_D3D8_CAPS_BYTES + 4);"],
    ["legacy ZBIAS mapping omitted","nine","d9wg_d3d9_core_caps8","caps.RasterCaps |= D9WG_D3D8_RASTER_ZBIAS;","(void)caps.RasterCaps;"],
    ["private caps depend on D3D9 shader profile","nine","d9wg_d3d9_core_caps8","fill_caps_for_profile(&caps, 0, FALSE);","fill_caps_for_profile(&caps, shader_model_version(), FALSE);"],
    ["private caps leak permissive D3D9 mode","nine","d9wg_d3d9_core_caps8","fill_caps_for_profile(&caps, 0, FALSE);","fill_caps_for_profile(&caps, 0, TRUE);"],
    ["D3D9-only device caps exposed","eight","fill_caps",devMask,"(void)caps->DevCaps;"],
    ["D3D9-only raster caps exposed","eight","fill_caps",rasterMask,"(void)caps->RasterCaps;"],
    ["D3D8 flat shade flags lost","eight","fill_caps","caps->ShadeCaps |= D3DPSHADECAPS_COLORFLATRGB | D3DPSHADECAPS_ALPHAFLATBLEND;","(void)caps->ShadeCaps;"],
    ["D3D8 alpha palette flag lost","eight","fill_caps","caps->TextureCaps |= D3DPTEXTURECAPS_ALPHAPALETTE;","(void)caps->TextureCaps;"],
    ["D3D8 volume addressing relation lost","eight","fill_caps","caps->VolumeTextureAddressCaps = caps->TextureAddressCaps;","caps->VolumeTextureAddressCaps = 0;"],
    ["D3D9 vertex constant limit exposed","eight","fill_caps","caps->MaxVertexShaderConst = D8WG_MAX_VS_CONSTANTS;","caps->MaxVertexShaderConst = 256;"],
    ["D3D9 shader version exposed","eight","fill_caps","caps->PixelShaderVersion = (DWORD)D3DPS_VERSION(1, 4);","caps->PixelShaderVersion = (DWORD)D3DPS_VERSION(2, 0);"],
    ["packed FVF count treated as flag mask","eight","fill_caps","caps->FVFCaps = coords | (caps->FVFCaps & D3DFVFCAPS_PSIZE);","caps->FVFCaps = (coords & 8u) | (caps->FVFCaps & D3DFVFCAPS_PSIZE);"],
    ["D3D8 stream array bound not applied","eight","fill_caps","if (caps->MaxStreams > D8WG_MAX_STREAMS) caps->MaxStreams = D8WG_MAX_STREAMS;","(void)caps->MaxStreams;"],
    ["old frozen texture limit bypasses backend","eight","fill_caps","caps->MaxPixelShaderValue = 8.0f;","caps->MaxPixelShaderValue = 8.0f;caps->MaxTextureWidth = 8192;"],
];
try{for(const[label,kind,name,before,after]of cases){const file=dir+"/proxy.c";fs.writeFileSync(file,mutate(sources[kind],name,before,after));const env={...process.env,[kind==="eight"?"D8WG_CAPS_CORE_SOURCE":"D8WG_CAPS_CORE_BACKEND_SOURCE"]:file},r=spawnSync(process.execPath,[__dirname+"/d8wg_caps_core_test.js"],{env,encoding:"utf8",timeout:30000}),out=r.stdout+r.stderr;assert(!r.error,label);assert.notEqual(r.status,0,"survived "+label);assert(!/error:|undefined reference|Undefined symbols/.test(out),label+" compile failure: "+out);assert.match(out,/D3D8 caps core line|Assertion.*failed|Assertion failed/,label+" must fail runtime assertion: "+out);console.log("KILL "+label);}console.log("PASS D3D8 caps/core mutations:16/16, strict compilation then runtime assertions");}finally{fs.rmSync(dir,{recursive:true,force:true});}
