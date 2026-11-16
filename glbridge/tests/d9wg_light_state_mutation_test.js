"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const header=path.resolve(__dirname,"../runtime/d9wg_light_state.h"),source=fs.readFileSync(header,"utf8"),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-light-mutants-"));
const mutations=[
 ["default direction reversed","0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x3f800000u","0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xbf800000u"],
 ["DWORD indices alias small slots","entry->index != index","(entry->index & 7u) != (index & 7u)"],
 ["inserting loses earlier nodes","entry->next = *list","entry->next = 0"],
 ["allocation failure aliases existing light","if (!entry) return 0;","if (!entry) return *list;"],
 ["release leaks nodes","D9WG_LIGHT_FREE(entry);","if (entry->index == 123456u) D9WG_LIGHT_FREE(entry);"]
];
try{for(const [label,before,after] of mutations){assert.equal(source.split(before).length,2,label);const file=path.join(directory,"light.h");fs.writeFileSync(file,source.replace(before,after));const run=spawnSync(process.execPath,[path.join(__dirname,"d9wg_light_state_test.js")],{env:{...process.env,D9WG_LIGHT_STATE_HEADER:file},encoding:"utf8"});assert.notEqual(run.status,0,"survived "+label);assert.match(run.stderr,/light state line/,"semantic runtime failure required: "+label);}console.log("PASS shared light-state mutations: 5/5; production sources untouched");}finally{fs.rmSync(directory,{recursive:true,force:true});}
