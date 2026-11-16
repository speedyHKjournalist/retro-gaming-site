"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), { spawnSync } = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8"), directory = fs.mkdtempSync(path.join(os.tmpdir(), "gl-thread-mutations-"));
const mutants = [
    ["missing API lock", "EnterCriticalSection(&g_api_lock);guard.held = TRUE;", "guard.held = FALSE;"],
    ["global current context", "target = v86gl_current_context();", "target = g_loaded_context;"],
    ["lost previous client workspace", "if (g_loaded_context && g_loaded_context->used) save_guest_context_state(g_loaded_context);", "if (g_loaded_context && g_loaded_context->used) (void)0;"],
    ["wrong restored workspace", "if (target) load_guest_context_state(target);", "if (target) (void)0;"],
    ["TLS failure reported as success", "if (!TlsSetValue(g_context_tls, context)) {", "if (FALSE) {"],
    ["surface uses another thread's context", "V86GLWGLContext* current_context = v86gl_current_context();", "V86GLWGLContext* current_context = &g_wgl_contexts[0];"],
    ["private map names alias", "state->name == name && state->share_group == scope", "state->name == name"],
    ["live shared map freed", "if (g_wgl_contexts[i].used && g_wgl_contexts[i].share_group == share_group) return;", "if (FALSE) return;"],
    ["group deletion frees unrelated maps", "!state->used || state->share_group != share_group", "!state->used"],
    ["callback performs transport IO", "InterlockedExchange(&g_window_dirty, 1);", "InterlockedExchange(&g_window_dirty, 1);emit_pci_batch(TRUE);", "gl_window_callbacks_abi_test.js"],
    ["monitoring replaces application procedure", "g_current_hwnd=hwnd;watch_window_state();", "g_current_hwnd=hwnd;watch_window_state();SetWindowLongA(hwnd,0,0);", "gl_window_callbacks_abi_test.js"],
    ["window dispatcher bypasses arena lock", "Sleep(50);EnterCriticalSection(&g_api_lock);", "Sleep(50);", "gl_window_dispatcher_abi_test.js"],
    ["window dispatcher lacks DLL reference", "if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)(uintptr_t)window_dispatch_thread, &module)) return;", "module = (HMODULE)(uintptr_t)1;", "gl_window_dispatcher_abi_test.js"],
    ["window dispatcher leaks after last context", "if (!g_wgl_context_count) {", "if (FALSE) {", "gl_window_dispatcher_abi_test.js"],
    ["destruction requires message delivery", "if (hwnd && !IsWindow(hwnd)) mark_window_change(hwnd,TRUE);", "if (FALSE) mark_window_change(hwnd,TRUE);", "gl_wgl_lifecycle_abi_test.js"],
    ["only one unrealized group member moves", "g_wgl_contexts[i].share_group==previous_group) g_wgl_contexts[i].share_group=first->share_group;", "g_wgl_contexts[i].share_group==previous_group && &g_wgl_contexts[i]==second) g_wgl_contexts[i].share_group=first->share_group;", "gl_wgl_context_ops_abi_test.js"],
    ["already shared current context is rejected", "if (first->share_group==second->share_group)", "if (FALSE)", "gl_wgl_context_ops_abi_test.js"],
    ["sharing ignores pixel format", "if (first->pixel_format!=second->pixel_format)", "if (FALSE)", "gl_wgl_context_ops_abi_test.js"],
    ["copy accepts current destination", "if (destination->owner_thread)", "if (FALSE)", "gl_wgl_context_ops_abi_test.js"],
    ["unimplemented state copy reports success", "if (mask) {SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return FALSE;}", "if (FALSE) {SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return FALSE;}", "gl_wgl_context_ops_abi_test.js"],
    ["unsupported layer creates a context", "if (layer) {SetLastError(ERROR_INVALID_PARAMETER);return NULL;}", "if (FALSE) {SetLastError(ERROR_INVALID_PARAMETER);return NULL;}", "gl_wgl_context_ops_abi_test.js"],
];
try {
    const baseline_env = { ...process.env };delete baseline_env.V86GL_PROXY_SOURCE;
    for (const fixture of new Set(mutants.map(entry => entry[3] || "gl_thread_guard_abi_test.js"))) {
        const baseline = spawnSync(process.execPath, [path.join(__dirname, fixture)], { encoding: "utf8", timeout: 60000, env: baseline_env });
        assert.equal(baseline.status, 0, fixture + " must pass before mutation\n" + baseline.stderr);
    }
    let killed = 0;
    for (const [name, before, after, fixture = "gl_thread_guard_abi_test.js"] of mutants) {
        assert.equal(source.split(before).length - 1, 1, name + " mutation anchor is current");
        const file = path.join(directory, "opengl32_proxy.c");fs.writeFileSync(file, source.replace(before, after));
        const result = spawnSync(process.execPath, [path.join(__dirname, fixture)], { encoding: "utf8", timeout: 60000, maxBuffer: 1024 * 1024, env: { ...process.env, V86GL_PROXY_SOURCE: file } });
        assert.equal(result.error, undefined, name + " fixture must finish");assert.notEqual(result.status, 0, name + " survived");killed++;
    }
    console.log("PASS guest thread/window mutations: " + killed + "/" + mutants.length + " killed, no stale anchors");
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
