"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"state8-mut-"));
const files={header:root+"/runtime/d9wg_state_bridge8.h",frontend:root+"/d3d8proxy/d3d8_proxy.c",backend:root+"/d3d9proxy/d3d9_proxy.c"},source=Object.fromEntries(Object.entries(files).map(([k,p])=>[k,readComparisonSource(p,"utf8")]));
function body(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return[m.index,b,s.slice(m.index,b)];}
const cases=[];
for(const name of['LINEPATTERN','ZVISIBLE','EDGEANTIALIAS','SOFTWAREVERTEXPROCESSING','PATCHSEGMENTS','POSITIONORDER','NORMALORDER'])cases.push(["deleted "+name,"header","d9wg_render_state8","case D3D8RS_"+name+":","case 1024u + D3D8RS_"+name+":"]);
for(const name of['ADDRESSU','ADDRESSV','ADDRESSW','BORDERCOLOR','MAGFILTER','MINFILTER','MIPFILTER','MIPMAPLODBIAS','MAXMIPLEVEL','MAXANISOTROPY'])cases.push(["sampler "+name,"header","d3d8_stage_state_to_sampler_state","return D3D9SAMP_"+name+";","return 13u;"]);
cases.push(
 ["ZBIAS target","header","d9wg_render_state8","translated.state = D3D9RS_DEPTHBIAS;","translated.state = 47u;"],
 ["ZBIAS clamp","header","d9wg_render_state8","value > 16u ? 16u : value","value > 8u ? 8u : value"],
 ["ZBIAS sign","header","d9wg_render_state8","* D3D8_ZBIAS_TO_DEPTHBIAS_STEP;","* -D3D8_ZBIAS_TO_DEPTHBIAS_STEP;"],
 ["raw value","header","d9wg_render_state8","D9WG_STATE8_RENDER, state, value","D9WG_STATE8_RENDER, state, value ^ 1u"],
 ["TSS value","header","d9wg_texture_stage_state8","sampler ? sampler : state, value","sampler ? sampler : state, value ^ 1u"],
 ["TSS cascade","header","d9wg_texture_stage_state8","D9WG_STATE8_SAMPLER : D9WG_STATE8_STAGE","D9WG_STATE8_SAMPLER : D9WG_STATE8_SAMPLER"],
 ["frontend render reserved","frontend","emit_render_state","command.reserved = 0;","command.reserved = 1;"],
 ["frontend render payload","frontend","emit_render_state","command.value = translated.value;","command.value = value;"],
 ["frontend sampler payload","frontend","emit_texture_stage_state","command.state = translated.state;","command.state = (UINT)state;"],
 ["core render profile","backend","d9wg_d3d9_core_set_render_state8","!= 8u","!= 9u"],
 ["core stage profile","backend","d9wg_d3d9_core_set_texture_stage_state8","!= 8u","!= 9u"],
 ["core stage bound","backend","d9wg_d3d9_core_set_texture_stage_state8","stage >= D9WG_TEXTURE_STAGES8","stage > D9WG_TEXTURE_STAGES8"],
 ["core TSS bound","backend","d9wg_d3d9_core_set_texture_stage_state8","state >= D9WG_TEXTURE_STAGE_STATES8","state > D9WG_TEXTURE_STAGE_STATES8"],
 ["core drop","backend","d9wg_d3d9_core_set_render_state8","translated.kind == D9WG_STATE8_DROP","translated.kind == 9u"],
 ["core render value","backend","d9wg_d3d9_core_set_render_state8","translated.state, translated.value","translated.state, value"],
 ["core sampler target","backend","d9wg_d3d9_core_set_texture_stage_state8","IDirect3DDevice9_SetSamplerState(iface, stage,","IDirect3DDevice9_SetTextureStageState(iface, stage,"],
 ["core sampler stage","backend","d9wg_d3d9_core_set_texture_stage_state8","IDirect3DDevice9_SetSamplerState(iface, stage,","IDirect3DDevice9_SetSamplerState(iface, stage ^ 1u,"],
 ["core cascade value","backend","d9wg_d3d9_core_set_texture_stage_state8","(D3DTEXTURESTAGESTATETYPE)translated.state, translated.value","(D3DTEXTURESTAGESTATETYPE)translated.state, value ^ 1u"]
);
try{for(const[label,key,name,from,to]of cases){const s=source[key],[a,b,f]=body(s,name);assert(f.includes(from),label);const file=dir+"/mutation."+(key==="header"?"h":"c");fs.writeFileSync(file,s.slice(0,a)+f.replace(from,to)+s.slice(b));const env={...process.env,[key==="header"?"D8WG_STATE_BRIDGE_HEADER":key==="frontend"?"D8WG_STATE_SOURCE8":"D8WG_STATE_SOURCE9"]:file},r=spawnSync(process.execPath,[root+"/tests/d8wg_state_bridge_test.js"],{env,encoding:"utf8",timeout:180000}),out=r.stdout+r.stderr;assert(r.status!==0&&!r.signal,label+" survived/abnormal "+out);assert(!/error:|undefined reference|Undefined symbols|error generated|errors generated/.test(out),label+" compilation failure is not a mutation kill "+out);assert(/State bridge8 case \d+ line \d+:/.test(out),label+" missing actual C assertion "+out);}
 console.log(`PASS shared API8 state mutations: ${cases.length}/${cases.length};strict compile then actual C assertions for seven drops,ten samplers,ZBIAS/raw bits,frontend payload,private profile/bounds/dispatch/failure`);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
