"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-window-watch-mutants-")),original=fs.readFileSync(root+"/runtime/d9wg_window_watch.c","utf8");
const cases=[
 ["wrong object accepted","if (object != OBJID_WINDOW || child != CHILDID_SELF || !window)","if (0 && (object != OBJID_WINDOW || child != CHILDID_SELF || !window))"],
 ["child events accepted","child != CHILDID_SELF","0 * child != CHILDID_SELF"],
 ["foreground event omitted","    if (event != EVENT_SYSTEM_FOREGROUND) {","    if (event == EVENT_SYSTEM_FOREGROUND) return;\n    if (event != EVENT_SYSTEM_FOREGROUND) {"],
 ["location event omitted","&& event != EVENT_OBJECT_LOCATIONCHANGE)","&& TRUE)"],
 ["eight-object limit restored","    for (;;) {\n        if (worker != current", "    unsigned limit = 0;\n    for (;;) {\n        if (++limit > 8u) break;\n        if (worker != current"],
 ["recursive notifications allowed","if (dispatching) {","if (0 && dispatching) {"],
 ["reentrant refresh lost","if (pending)\n        queue_refresh_locked(worker);","if (0 && pending)\n        queue_refresh_locked(worker);"],
 ["batched flush omitted","if (changed && flush)","if (0 && changed && flush)"],
 ["epoch wrap reset lost","entry->epoch = 0;","entry->epoch = epoch;"],
 ["new registration notified too early","watch->epoch = dispatching ? epoch : 0;","watch->epoch = 0;"],
 ["unhooks on removing thread","        worker->stopping = TRUE;","        UnhookWinEvent(worker->foreground);\n        worker->stopping = TRUE;"],
 ["old worker clears new generation","if (worker == current)\n        worker = NULL;","if (worker)\n        worker = NULL;"],
 ["startup retry loses backoff","if (!d9wg_retry_ready(now, failed_at, retry_armed))","if (0 && !d9wg_retry_ready(now, failed_at, retry_armed))"],
 ["startup wait holds transport lock","    LeaveCriticalSection(lock);\n    /* Registration", "    /* Registration" ,"        WaitForSingleObject(ready, WINDOW_RETRY_MS);","        WaitForSingleObject(ready, WINDOW_RETRY_MS);\n        LeaveCriticalSection(lock);"],
 ["worker created for every add","if (worker && !worker->stopping)\n        return TRUE;","if (0 && worker && !worker->stopping)\n        return TRUE;"],
 ["stale generation hook accepted","if (current && hook && (hook == current->foreground || hook == current->window))","if (current && hook)"],
 ["missing timer retry omitted","if (start_worker_locked() && worker->queue_ready && !worker->timer","if (0 && start_worker_locked() && worker->queue_ready && !worker->timer"],
 ["removal ignored","if (*link) {","if (0 && *link) {"],
 ["module address ownership lost","GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS","0u"],
];
try{
 for(const [name,before,after,secondBefore,secondAfter]of cases){assert.equal(original.split(before).length,2,name);let mutated=original.replace(before,after);if(secondBefore){assert.equal(mutated.split(secondBefore).length,2,name);mutated=mutated.replace(secondBefore,secondAfter);}const file=dir+"/mutated.c";fs.writeFileSync(file,mutated);const run=spawnSync(process.execPath,[path.join(__dirname,"d9wg_window_watch_test.js")],{env:{...process.env,D9WG_WINDOW_WATCH_SOURCE:file},encoding:"utf8",timeout:65000});assert.notEqual(run.status,0,"survived "+name);assert.match(run.stderr,/window watch line/,"native runtime assertion required: "+name);}
 for(const api of [8,9,7]){const folder=api===7?"ddraw":"d3d"+api,source=fs.readFileSync(root+"/"+folder+"proxy/"+folder+"_proxy.c","utf8"),before="    d9wg_window_watch_retry();";assert.equal(source.split(before).length,api===8?3:2);const file=dir+"/frontend.c";fs.writeFileSync(file,source.replace(before,"    /* optional retry lost */"));const run=spawnSync(process.execPath,[path.join(__dirname,"d9wg_window_present_test.js")],{env:{...process.env,D9WG_WINDOW_PRESENT_API:String(api),D9WG_WINDOW_PRESENT_SOURCE:file},encoding:"utf8"});assert.notEqual(run.status,0,"survived API "+api+" retry loss");assert.match(run.stderr,/window present line/);}
 const schedules=[...[8,9,7].map(api=>[api,"emit_present_and_flush","present"]),[8,"emit_surface_update_and_flush","state"],[9,"emit_window_state","state"],[7,"emit_window_state_showing","state"],[7,"emit_window_state_not_showing","state"]];
 for(const [api,name,kind]of schedules){const folder=api===7?"ddraw":"d3d"+api,source=fs.readFileSync(root+"/"+folder+"proxy/"+folder+"_proxy.c","utf8"),match=source.match(new RegExp("static [^\\n]*\\b"+name+"\\([^;]*?\\)\\n\\{[\\s\\S]*?\\n}"));assert(match,name);const call=api===8&&kind==="present"?/    capture_surface\([^;]+;/:/    d9wg_window_probe_capture\([^;]+;/;let body=match[0];const lock="    EnterCriticalSection(&g_transport_lock);\n";assert(body.indexOf(lock)<body.search(call));body=body.replace(lock,"").replace(call,capture=>capture+"\n"+lock.trimEnd());const file=dir+"/interleaved.c";fs.writeFileSync(file,source.replace(match[0],body));const run=spawnSync(process.execPath,[path.join(__dirname,"d9wg_window_"+kind+"_test.js")],{env:{...process.env,...(kind==="present"?{D9WG_WINDOW_PRESENT_API:String(api),D9WG_WINDOW_PRESENT_SOURCE:file}:{D9WG_WINDOW_MUTATION_API:String(api),D9WG_WINDOW_FRONTEND_SOURCE:file})},encoding:"utf8"});assert.notEqual(run.status,0,"survived capture serialization "+api+"/"+name);assert.match(run.stderr,new RegExp("window "+kind+" line"));}
 const d9file=root+"/d3d9proxy/d3d9_proxy.c",d9source=fs.readFileSync(d9file,"utf8"),maintain=d9source.match(/static BOOL maintain_fullscreen_foreground\([^;]*?\)\n\{[\s\S]*?\n}/);assert(maintain);const file=dir+"/activation.c";fs.writeFileSync(file,d9source.replace(maintain[0],maintain[0].replace("    LeaveCriticalSection(&g_transport_lock);","    /* activation lock retained */")));const activation=spawnSync(process.execPath,[path.join(__dirname,"d9wg_window_present_test.js")],{env:{...process.env,D9WG_WINDOW_PRESENT_API:"9",D9WG_WINDOW_PRESENT_SOURCE:file},encoding:"utf8"});assert.notEqual(activation.status,0,"survived locked activation");assert.match(activation.stderr,/window present line: activation held transport lock/);

}finally{fs.rmSync(dir,{recursive:true,force:true});}
console.log("PASS actual window watcher mutations: 30/30 event filters, 64-device delivery, reentrancy/lifetime/generations, same-thread unhooks, startup/timer backoff and three Present retry paths, serialized capture/Present/report ordering and unlocked activation");
