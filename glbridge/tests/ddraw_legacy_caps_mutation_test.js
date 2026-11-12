"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-caps-mut-"));
const cases=[
 ["API descriptor sizes merged","legacy","d3d_legacy_caps_size","if (version == 1u)","if (FALSE && version == 1u)"],
 ["old view exposes new tail","legacy","d3d_legacy_caps_pair","if (size < sizeof(*hal))","if (FALSE && size < sizeof(*hal))"],
 ["software reports hardware descriptor","legacy","d3d_legacy_caps_pair","d3d_legacy_fill_caps(hel);","d3d_legacy_fill_caps(hal); d3d_legacy_fill_caps(hel);"],
 ["software colour model wrong","legacy","d3d_legacy_caps_pair","? D3DCOLOR_MONO : D3DCOLOR_RGB;","? D3DCOLOR_RGB : D3DCOLOR_RGB;"],
 ["HAL fallback claims raster caps","legacy","d3d_legacy_caps_pair","hel->dwDevCaps = D3DDEVCAPS_FLOATTLVERTEX;","hel->dwDevCaps = hal->dwDevCaps;"],
 ["old inventory exposes newer classes","legacy","d3d_legacy_enum_devices","version < devices[index].min_version || version > devices[index].max_version","FALSE"],
 ["callback cancellation ignored","legacy","d3d_legacy_enum_devices","== D3DENUMRET_CANCEL)","== -12345)"],
 ["old result buffers rejected","legacy","d3d_legacy_find_device","if (size != prefix + 2u * d3d_legacy_caps_size(1u) &&","if (size != sizeof(*result) || (size != prefix + 2u * d3d_legacy_caps_size(1u) &&"],
 ["caller result stride ignored","legacy","d3d_legacy_find_device","stride = (size - prefix) / 2u;","stride = sizeof(D3DDEVICEDESC);"],
 ["unmatched GUID erases output","legacy","d3d_legacy_find_device","return DDWG_TRACE_ERROR(DDERR_NOTFOUND);","{ ZeroMemory(result, size); return DDWG_TRACE_ERROR(DDERR_NOTFOUND); }"],
 ["hardware filter ignored","legacy","d3d_legacy_find_device","!!search->bHardware != !!guid_equal(guid, &IID_IDirect3DHALDevice)","FALSE"],
 ["overlap destroys matched GUID","legacy","d3d_legacy_find_device","result->guid = selected;","result->guid = *guid; (void)selected;"],
 ["software7 reports DP2 hardware interface","seven","d3d7_fill_caps_for","D3DDEVCAPS_DRAWPRIMITIVES2 | D3DDEVCAPS_DRAWPRIMITIVES2EX);","D3DDEVCAPS_DRAWPRIMITIVES2);"],
 ["modern enum cancellation ignored","seven","d3d7_EnumDevices","== D3DENUMRET_CANCEL)","== -12345)"],
];
try{for(const[label,kind,name,from,to]of cases){const source=fs.readFileSync(root+"/ddrawproxy/"+(kind==="legacy"?"d3d_legacy_proxy.inc":"d3d7_proxy.inc"),"utf8"),m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}let body=source.slice(m.index,end);if(label==="old result buffers rejected"){const close="size != sizeof(*result))";assert.equal(body.split(close).length,2);body=body.replace(close,"size != sizeof(*result)))");}assert(body.includes(from),label);if(label!=="unmatched GUID erases output")assert.equal(body.split(from).length,2,label);body=body.replace(from,to);const file=dir+"/mut.inc";fs.writeFileSync(file,source.slice(0,m.index)+body+source.slice(end));const r=spawnSync(process.execPath,[__dirname+"/ddraw_legacy_caps_test.js"],{encoding:"utf8",env:{...process.env,[kind==="legacy"?"DDRAW_LEGACY_CAPS_SOURCE":"DDRAW_CAPS7_SOURCE"]:file}});assert.notEqual(r.status,0,label);assert.match(r.stderr,/legacy caps line|AddressSanitizer: stack-buffer-overflow/,label+": must compile and fail bounds/profile assertions\n"+r.stderr.slice(-1600));assert(!/error: /.test(r.stderr),r.stderr);console.log("KILL "+label);}console.log("PASS legacy capability mutations: "+cases.length+"/"+cases.length+" compiled implementations rejected by ABI/profile/bounds checks");}finally{fs.rmSync(dir,{recursive:true,force:true});}
