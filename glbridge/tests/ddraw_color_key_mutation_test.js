"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-color-key-mut-"));
function mutate(s,name,old,replacement){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(s);assert(m,name);let end=s.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(s[end]==="{")depth++;if(s[end]==="}")depth--;}const body=s.slice(m.index,end);assert(body.includes(old),name+": "+old);return s.slice(0,m.index)+body.replace(old,replacement)+s.slice(end);}
function run(label,env){const r=spawnSync(process.execPath,[__dirname+"/ddraw_color_key_test.js"],{encoding:"utf8",timeout:30000,env:{...process.env,...env}}),out=r.stdout+r.stderr;assert(!r.error,label+": "+r.error);assert.notEqual(r.status,0,label+" survived");assert(!/error:|undefined reference|Undefined symbols/.test(out),label+" compiler failure: "+out);assert(/DDraw color key|Assertion|signal=/.test(out),label+" no runtime failure");}
try{let killed=0;const policy=fs.readFileSync(root+"/runtime/d9wg_ddraw_color_key.h","utf8");for(const[label,old,replacement]of[
 ["explicit range accepted","if (explicit_range && low != high)","if (0 && explicit_range && low != high)"],
 ["unflagged high retained","out->high = low;","out->high = high;"],
 ["canonical low changed","out->low = low;","out->low = 0;"],
 ["equal explicit key refused","low != high","(low != high || low == high)"],
]){assert(policy.includes(old),label);const file=dir+"/policy.h";fs.writeFileSync(file,policy.replace(old,replacement));run(label,{DDRAW_COLOR_KEY_POLICY:file});killed++;}
const source=fs.readFileSync(root+"/ddrawproxy/ddraw_proxy.c","utf8");for(const[label,fn,old,replacement]of[
 ["setter ignores explicit range","surface_SetColorKey","(flags & DDCKEY_COLORSPACE) != 0","0"],
 ["setter treats every high as explicit","surface_SetColorKey","(flags & DDCKEY_COLORSPACE) != 0","1"],
 ["setter uses caller range after normalization","surface_SetColorKey","key = &fixed_key;","(void)fixed_key;"],
 ["setter wrong refusal code","surface_SetColorKey","DDWG_TRACE_ERROR(DDERR_NOCOLORKEYHW)","DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS)"],
 ["setter stores wrong key kind","surface_SetColorKey","surface->color_key[kind] = *key;","surface->color_key[0] = *key;"],
 ["setter omits description presence","surface_SetColorKey","surface->desc.dwFlags |= description_flag;","(void)description_flag;"],
 ["setter description differs from cache","surface_SetColorKey","*surface_description_color_key(surface, kind) = *key;","*surface_description_color_key(surface, kind) = (DDCOLORKEY){1,2};"],
 ["failed setter refreshes overlay","surface_SetColorKey","    if (key) {","    if (key) { emit_overlay_state(surface);"],
 ["creation omits key preflight","ddraw_CreateSurfaceForVersion","if (!surface_request_color_keys_valid(request))","if (0 && !surface_request_color_keys_valid(request))"],
 ["creation skips fourth key","surface_request_color_keys_valid","i < 4u","i < 3u"],
 ["creation inspects unflagged keys","surface_request_color_keys_valid","(request->dwFlags & flags[i])","((request->dwFlags & flags[i]) || TRUE)"],
 ["creation wrong refusal code","ddraw_CreateSurfaceForVersion","DDWG_TRACE_ERROR(DDERR_NOCOLORKEYHW)","DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS)"],
]){const file=dir+"/frontend.c";fs.writeFileSync(file,mutate(source,fn,old,replacement));run(label,{DDRAW_COLOR_KEY_SOURCE:file});killed++;}
console.log("PASS DDraw color-key mutations: "+killed+"/16; shared canonicalization, explicit ranges, cache/description/wire agreement, overlay failure atomicity, full creation preflight, all selectors and ignored unflagged values");}finally{fs.rmSync(dir,{recursive:true,force:true});}
