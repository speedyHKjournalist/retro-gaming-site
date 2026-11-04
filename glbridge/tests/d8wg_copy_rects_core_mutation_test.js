"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"rect8-mut-"));
const core=fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8"),header=fs.readFileSync(root+"/runtime/d9wg_copy_rects_bridge8.h","utf8");
const plan=[
 ["destination bounds","header","(UINT)(area.right - area.left) > to.Width - (UINT)point.x","(UINT)(area.right - area.left) >= to.Width - (UINT)point.x"],
 ["source bounds","header","(UINT)area.right > from.Width || ",""],
 ["pool dispatch","header","from.Pool == D3DPOOL_DEFAULT && to.Pool == D3DPOOL_DEFAULT","from.Pool != D3DPOOL_DEFAULT && to.Pool == D3DPOOL_DEFAULT"],
 ["GPU source mip","header","copy.source_left = area.left;","copy.source_level = 0; copy.source_left = area.left;"],
 ["GPU target cube face","header","copy.destination_top = target.top;","copy.destination_face = 0; copy.destination_top = target.top;"],
 ["GPU owner","header","copy.device_handle = D9WG_RECT_OWNER(source)->handle;","copy.device_handle = 0;"],
 ["GPU filter","header","copy.filter_point = 1;","copy.filter_point = 0;"],
 ["GPU dirty flag","header","D9WG_RECT_DIRTY(to_level, TRUE);","D9WG_RECT_DIRTY(to_level, FALSE);"],
 ["failed source refresh","header","if (FAILED(result))","if (0)"],
 ["CPU copy bytes","header","UINT row_bytes = (area.right - area.left) * pixel_bytes","UINT row_bytes = (area.right - area.left) * pixel_bytes - 1"],
 ["overlap snapshot","header","if (input == output)","if (0)"],
 ["full target validity","header","if (to_level && target.left == 0 && target.top == 0 && (UINT)target.right == to.Width && (UINT)target.bottom == to.Height)","if (to_level)"],
 ["ordered array","header","&rects[index]","&rects[0]"],
 ["array destinations","header","points ? &points[index] : NULL","points ? &points[0] : NULL"],
 ["partial failure result","header","if (FAILED(hr))","if (0)"],
 ["zero count validates formats","header","source_desc.Format != destination_desc.Format || ",""],
 ["optional points","header","(count && !rects)","(count && (!rects || !points))"],
 ["cube face identity","core","*face = surface->cube_face;","*face = 0;"],
 ["CPU authoritative pools","core","desc.Pool != D3DPOOL_DEFAULT\n            || ",""],
 ["GPU ordinary texture refresh","core","!(level && !level->shadow_valid)","TRUE"],
 ["readback failure type","core","return !result ? D3D_OK : result == D9WG_READBACK_INVALID\n            ? D3DERR_INVALIDCALL : D3DERR_DRIVERINTERNALERROR;","return !result ? D3D_OK : D3DERR_DRIVERINTERNALERROR;","refresh_copy8"],
 ["core profile","core","device_compatibility_api(device_from_iface(iface)) != 8u","device_compatibility_api(device_from_iface(iface)) != 9u","d9wg_d3d9_core_copy_rects8"],
];
function span(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let e=s.indexOf("{",m.index)+1,d=1;for(;d;e++){if(s[e]==="{")d++;if(s[e]==="}")d--;}return[m.index,e];}
try{let killed=0;for(const[name,kind,from,to,fn]of plan){let candidate=kind==="core"?core:header;if(fn){const[a,b]=span(candidate,fn),body=candidate.slice(a,b);assert(body.includes(from),name);candidate=candidate.slice(0,a)+body.replace(from,to)+candidate.slice(b);}else{assert(candidate.includes(from),name);candidate=candidate.replace(from,to);}const file=dir+"/candidate.c";fs.writeFileSync(file,candidate);const run=spawnSync(process.execPath,[__dirname+"/d8wg_copy_rects_core_test.js"],{encoding:"utf8",timeout:60000,env:{...process.env,[kind==="core"?"D8WG_RECT_CORE_SOURCE":"D8WG_RECT_BRIDGE_HEADER"]:file}});assert(!run.error,name+": "+run.error);assert.notEqual(run.status,0,name+" survived");const output=run.stdout+run.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+" compiler failure: "+output);assert(/CopyRects8 core case \d+ line \d+:|Assertion failed: \([^\n]*\), function \w+, file rect\.c, line \d+\.|rect\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(output),name+" missing C assertion: "+output);killed++;}console.log("PASS CopyRects8 core mutations: "+killed+"/"+plan.length+"; each strictly compiled then failed an actual C semantic assertion");}finally{fs.rmSync(dir,{recursive:true,force:true});}
