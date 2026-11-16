"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=readComparisonSource(root+"/runtime/d9wg_window_state.c","utf8"),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-window-mutants-"));
const cases=[
 ["geometry omitted", "cache", "if (a[i] != b[i]) return 0;", "if ((i < 16u) && a[i] != b[i]) return 0;"],
 ["foreground omitted", "cache", "if (a[i] != b[i]) return 0;", "if ((i < 8u || i >= 12u) && a[i] != b[i]) return 0;"],
 ["region omitted", "cache", "i < bytes; ++i)\n        if (a[i]", "i < sizeof(D9WGWindowState); ++i)\n        if (a[i]"],
 ["inactive tail compared", "cache", "uint32_t bytes = (uint32_t)D9WG_WINDOW_STATE_BYTES(report);", "uint32_t bytes = sizeof(*report);",2],
 ["cache never valid", "cache", "cache->valid = 1;", "cache->valid = 0;"],
 ["invalidation ignored", "cache", "cache->valid = 0;", "cache->valid = cache->valid;"],
 ["region copy lost", "cache", "i < count; ++i)", "i < 0u * count; ++i)"],
 ...[8,9,7].map(api=>["API "+api+" failed emit cached",api,"if (result) d9wg_window_cache_commit(","if (1) d9wg_window_cache_commit("])
];
try{for(const [name,kind,before,after,count=1]of cases){const original=kind==="cache"?source:readComparisonSource(root+"/"+(kind===7?"ddraw":"d3d"+kind)+"proxy/"+(kind===7?"ddraw":"d3d"+kind)+"_proxy.c","utf8");assert.equal(original.split(before).length,count+1,name);const file=path.join(directory,"mutated.c");fs.writeFileSync(file,original.replaceAll(before,after));const env={...process.env,...(kind==="cache"?{D9WG_WINDOW_CACHE_SOURCE:file}:{D9WG_WINDOW_MUTATION_API:String(kind),D9WG_WINDOW_FRONTEND_SOURCE:file})};const run=spawnSync(process.execPath,[path.join(__dirname,"d9wg_window_state_test.js")],{env,encoding:"utf8"});assert.notEqual(run.status,0,"survived "+name);assert.match(run.stderr,/window state line/,"native runtime assertion required: "+name);}}finally{fs.rmSync(directory,{recursive:true,force:true});}
console.log("PASS shared window report mutations: 10/10 complete identity, active region bytes, lifecycle/copy and three frontend failed-send cache paths");
