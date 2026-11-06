"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"exclusive-mut-")),source=fs.readFileSync(root+"/runtime/d9wg_exclusive_window.c","utf8");
const mutations=[
    ["Reset leaves output minimized","ShowWindow(output, SW_RESTORE);","ShowWindow(output, SW_SHOW);"],
    ["NOWINDOWCHANGES ignored on entry","if (!(flags & D9WG_EXCLUSIVE_NO_WINDOW_CHANGES)) {","if (1) {"],
    ["visible output forced topmost","(flags & D9WG_EXCLUSIVE_D3D9) && IsWindowVisible(output)","0 && (flags & D9WG_EXCLUSIVE_D3D9) && IsWindowVisible(output)"],
    ["API9 iconic activation remains lost","|| (entry->flags & D9WG_EXCLUSIVE_D3D9)","|| (0 && (entry->flags & D9WG_EXCLUSIVE_D3D9))"],
    ["minimized app activation recovers early","(!wp || !IsIconic(window)\n                    || (entry->flags & D9WG_EXCLUSIVE_D3D9))","1"],
    ["actual foreground gate omitted","GetForegroundWindow() == window","1"],
    ["lost-only recovery gate omitted","candidate->state->lost && !candidate->changing","!candidate->changing"],
    ["window activation fallback omitted","msg == WM_ACTIVATE && LOWORD(wp)","msg == WM_ACTIVATE + 1 && LOWORD(wp)"],
    ["focus messages leak","hook->suppress && msg != WM_DISPLAYCHANGE","0 && hook->suppress && msg != WM_DISPLAYCHANGE"],
    ["lost state missing","if (entry->state) entry->state->lost = 1;","if (entry->state) entry->state->lost = 0;"],
    ["activation stays lost","} else {\n                    entry->state->lost = 0;","} else {\n                    entry->state->lost = 1;"],
    ["external display change ignored","entry->state->required = 1;\n            } else if","entry->state->required = 0;\n            } else if"],
    ["application replacement overwritten","if (current == exclusive_proc)","if (current)"],
    ["subscription freed during callback","if (entry) InterlockedIncrement(&entry->refs);","(void)entry;"],
    ["hook freed during callback","InterlockedIncrement(&hook->refs);\n    if (hook->suppress","(void)hook;\n    if (hook->suppress"],
    ["destroyed window property retained","RemovePropA(window, EXCLUSIVE_PROPERTY);","(void)window;"],
    ["retired subscription not freed","    release_subscription(entry);\n}\n\nBOOL d9wg_exclusive_window_enter","    (void)entry;\n}\n\nBOOL d9wg_exclusive_window_enter"],
    ["foreign-thread window accepted","GetWindowThreadProcessId(focus, NULL) != GetCurrentThreadId()","GetWindowThreadProcessId(focus, NULL) == GetCurrentThreadId() + 999u"],
];
try {
    for(const [label,before,after] of mutations) {
        assert.equal(source.split(before).length,2,label+" anchor");const file=dir+"/candidate.c";fs.writeFileSync(file,source.replace(before,after));
        const run=spawnSync(process.execPath,[__dirname+"/d9wg_exclusive_window_test.js"],{encoding:"utf8",timeout:30000,env:{...process.env,D9WG_EXCLUSIVE_SOURCE:file}});
        assert(!run.error,label);assert.notEqual(run.status,0,label+" survived");const output=run.stdout+run.stderr;
        assert(!/error:|undefined reference|Undefined symbols/.test(output),label+" compilation failure: "+output);
        assert(/Assertion failed|AddressSanitizer|runtime error:|Assertion.*failed/.test(output),label+" requires actual runtime failure: "+output);
    }
    console.log("PASS exclusive window mutations: "+mutations.length+" compiled faults rejected at runtime, including callback lifetime and state/dispatch invariants");
} finally {fs.rmSync(dir,{recursive:true,force:true});}
