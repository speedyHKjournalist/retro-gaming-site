"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-palette-mutants-"));
const mutations=[
 ["runtime/d9wg_palette_state.h","D9WG_PALETTE_HEADER","implicit entries black","colors ? colors[i] : UINT32_MAX","colors ? colors[i] : 0"],
 ["runtime/d9wg_palette_state.h","D9WG_PALETTE_HEADER","palette alpha uses red","entry[3] << 24","entry[0] << 24"],
 ["runtime/d9wg_palette_state.h","D9WG_PALETTE_HEADER","old small index limit","65536u","256u"],
 ["runtime/d9wg_palette_state.h","D9WG_PALETTE_HEADER","entries alias within a page","page ? page->entries[index & 255] : 0;","page ? page->entries[0] : 0;"],
 ["runtime/d9wg_palette_state.h","D9WG_PALETTE_HEADER","OOM publishes partial page","if (fresh) D9WG_PALETTE_FREE(page);","if (fresh) store->pages[index >> 8] = page;"],
 ["runtime/d9wg_palette_state.h","D9WG_PALETTE_HEADER","release leaks pages","D9WG_PALETTE_FREE(page);store->pages[i] = 0;","if (i == 999) D9WG_PALETTE_FREE(page);store->pages[i] = 0;"],
 ["d3d8proxy/d3d8_proxy.c","D8WG_PALETTE_SOURCE","D3D8 recording mutates live selection","if (device->recording_state_block) {\n        device->recording_state_block->state.palette_mask","if (0 && device->recording_state_block) {\n        device->recording_state_block->state.palette_mask"],
 ["d3d9proxy/d3d9_proxy.c","D9WG_PALETTE_SOURCE","D3D9 recording mutates live selection","device->recording_result->has_palette = TRUE;","device->current_palette = index;\n    device->recording_result->has_palette = TRUE;"]
];
try{for(const [relative,variable,label,before,after] of mutations){const source=readComparisonSource(path.join(root,relative),"utf8");assert.equal(source.split(before).length,2,label);const file=path.join(directory,path.basename(relative));fs.writeFileSync(file,source.replace(before,after));const run=spawnSync(process.execPath,[path.join(__dirname,"d9wg_palette_state_test.js")],{env:{...process.env,[variable]:file},encoding:"utf8"});assert.notEqual(run.status,0,"survived "+label);assert.match(run.stderr,/palette API [89] line/,"semantic runtime failure required: "+label);}
 console.log("PASS shared palette mutations: 8/8; defaults, alpha, ID range, page isolation, atomic OOM, release and actual D3D8/9 recording paths");
}finally{fs.rmSync(directory,{recursive:true,force:true});}
