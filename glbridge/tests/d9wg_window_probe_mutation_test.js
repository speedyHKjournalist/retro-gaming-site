"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-window-probe-mutants-"));
const cases=[
 ["duplicate client query","probe","    POINT origin = {0, 0};","    GetClientRect(window, &client);\n    POINT origin = {0, 0};"],
 ["lost foreground","probe","state->foreground_hwnd = (uint32_t)(uintptr_t)GetForegroundWindow();","state->foreground_hwnd = 0;"],
 ["lost outer geometry","probe","probe->outer_succeeded = GetWindowRect(window, &outer) != 0;","probe->outer_succeeded = 0;"],
 ["failed origin treated as valid","probe","probe->origin_succeeded = ClientToScreen(window, &origin) != 0;","ClientToScreen(window, &origin); probe->origin_succeeded = 1;"],
 ["partial region lost","probe","probe->region.rect_count = region.count;","probe->region.rect_count = 0;"],
 ["region screen origin added","probe","probe->region.rects[i][j] = region.rects[i][j];","probe->region.rects[i][j] = region.rects[i][j] + state->window_x;"],
 ["iconic region queried","probe","&& !(state->flags & D9WG_WINDOW_ICONIC) && probe->client_succeeded","&& probe->client_succeeded"],
 ["D3D8 iconic fallback changed","geometry","|| (state->flags & D9WG_WINDOW_ICONIC)","|| 0"],
 ["D3D9 outer fallback lost","geometry","&& state->window_width && state->window_height)","&& 0)"],
 ["DDraw zero dimension fallback lost","geometry","if (state->client_width) geometry->width = state->client_width;","geometry->width = state->client_width;"],
 ["client-sized report lost","geometry","report->state.window_width = report->state.client_width = geometry.width;","report->state.client_width = geometry.width;"],
 ["no-surface origin lost","geometry","report->state.window_x = probe->outer_x;report->state.window_y = probe->outer_y;","report->state.window_x = 0;report->state.window_y = 0;"],
 ...[8,9,7].map(api=>["API "+api+" duplicate Present capture",api,"    d9wg_window_probe_geometry(&probe, D9WG_WINDOW_GEOMETRY_"+(api===7?"DDRAW":"D3D"+api)+",","    d9wg_window_probe_capture((uintptr_t)"+(api===7?"object->window":"window")+", 0, &probe);\n    d9wg_window_probe_geometry(&probe, D9WG_WINDOW_GEOMETRY_"+(api===7?"DDRAW":"D3D"+api)+",",api===9?2:1]),
 ["D3D9 post-reclaim capture omitted",9,"    if (maintain_fullscreen_foreground(device, window, &probe))\n        d9wg_window_probe_capture", "    if (0 && maintain_fullscreen_foreground(device, window, &probe))\n        d9wg_window_probe_capture"],
 ["D3D9 covered window steals foreground",9,"if ((probe->state.flags & D9WG_WINDOW_OCCLUDED) != 0)","if (0)"],
];
try{for(const [name,kind,before,after,count=1]of cases){
 const file=kind==="probe"||kind==="geometry"?root+"/runtime/d9wg_window_"+kind+".c":root+"/"+(kind===7?"ddraw":"d3d"+kind)+"proxy/"+(kind===7?"ddraw":"d3d"+kind)+"_proxy.c",original=readComparisonSource(file,"utf8");assert.equal(original.split(before).length,count+1,name);const mutant=dir+"/mutated.c";fs.writeFileSync(mutant,original.replaceAll(before,after));const env={...process.env,...(typeof kind==="number"?{D9WG_WINDOW_PRESENT_API:String(kind),D9WG_WINDOW_PRESENT_SOURCE:mutant}:{[kind==="probe"?"D9WG_WINDOW_PROBE_SOURCE":"D9WG_WINDOW_GEOMETRY_SOURCE"]:mutant})};const run=spawnSync(process.execPath,[path.join(__dirname,typeof kind==="number"?"d9wg_window_present_test.js":"d9wg_window_probe_test.js")],{env,encoding:"utf8"});assert.notEqual(run.status,0,"survived "+name);assert.match(run.stderr,/window (probe|present) line/,"native runtime assertion required: "+name);
}}finally{fs.rmSync(dir,{recursive:true,force:true});}
console.log("PASS shared Win32 window probe mutations: 17/17 query reuse, foreground, coordinate/region failures, three API geometry policies and fullscreen reclaim");
