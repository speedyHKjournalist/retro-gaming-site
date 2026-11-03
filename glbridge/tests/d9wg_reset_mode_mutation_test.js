"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-reset-mode-mut-"));
function mutate(source, name, old, replacement) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name);
    let end = source.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) { if (source[end] === "{") depth++; if (source[end] === "}") depth--; }
    const part = source.slice(m.index, end); assert(part.includes(old), name + ": " + old);
    return source.slice(0, m.index) + part.replace(old, replacement) + source.slice(end);
}
function run(label, test, env) {
    const r = spawnSync(process.execPath, [__dirname + "/" + test + ".js"], {encoding: "utf8", env: {...process.env, ...env}, timeout: 30000});
    assert(!r.error, label + ": " + r.error); assert.notEqual(r.status, 0, label + " survived");
    assert(!/error:|undefined reference|Undefined symbols/.test(r.stdout + r.stderr), label + " compile failure: " + r.stderr);
    assert(/Reset window guard|Reset mode entry|Reset status|Assertion|assertion|signal=/.test(r.stdout + r.stderr), label + " no runtime failure");
}
try {
    let killed = 0;
    const header = fs.readFileSync(root + "/runtime/d9wg_window_reset.h", "utf8");
    for (const [label, fn, old, replacement] of [
        ["WM_SIZE forwarded", "d9wg_reset_window_proc", "if (message == WM_SIZE)", "if (0 && message == WM_SIZE)"],
        ["all messages swallowed", "d9wg_reset_window_proc", "if (message == WM_SIZE)", "if (1)"],
        ["Unicode forwarded through ANSI", "d9wg_reset_window_proc", "return scope->unicode ?", "return 0 ?"],
        ["forwarded payload changed", "d9wg_reset_window_proc", "window, message, wparam, lparam)", "window, message, wparam + 1, lparam)"],
        ["nested original procedure not unwrapped", "d9wg_window_reset_begin", "scope->previous_scope ? scope->previous_scope->forward_proc : scope->previous_proc", "scope->previous_proc"],
        ["foreign thread allowed", "d9wg_window_reset_begin", "GetWindowThreadProcessId(window, NULL) != GetCurrentThreadId()", "(0 && GetWindowThreadProcessId(window, NULL) != GetCurrentThreadId())"],
        ["unknown nested subclass overwritten", "d9wg_window_reset_begin", "if (scope->previous_scope &&", "if (0 && scope->previous_scope &&"],
        ["failed subclass published", "d9wg_window_reset_begin", "if (!previous && GetLastError())", "if (0 && !previous && GetLastError())"],
        ["failed property ignored", "d9wg_window_reset_begin", "if (!SetPropA(", "if (0 & !SetPropA("],
        ["parent scope lost", "d9wg_window_reset_end", "if (scope->previous_scope)", "if (0 && scope->previous_scope)"],
        ["changed application procedure clobbered", "d9wg_window_reset_end", "if (current == scope->installed_proc)", "if (1 || current == scope->installed_proc)"],
        ["scope property survives teardown", "d9wg_window_reset_end", "RemovePropA(window, D9WG_WINDOW_RESET_PROPERTY);", "(void)window;"],
    ]) {
        const file = dir + "/guard.h"; fs.writeFileSync(file, mutate(header, fn, old, replacement));
        run(label, "d9wg_window_reset_test", {D9WG_WINDOW_RESET_HEADER: file}); killed++;
    }
    const source = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8");
    for (const [label, fn, old, replacement] of [
        ["windowed restore omitted", "reset_window_mode", "result = restore_display_mode(device);", "result = TRUE;"],
        ["fullscreen setup omitted", "reset_window_mode", "result = claim_fullscreen_foreground(device, window);", "result = TRUE;"],
        ["old window style not restored", "reset_window_mode", "if (device->styled_window &&", "if (0 && device->styled_window &&"],
        ["failed guard ignored", "reset_window_mode", "if (!d9wg_window_reset_begin(", "if (0 & !d9wg_window_reset_begin("],
        ["first guard not unwound", "reset_window_mode", "while (prepared)", "while (prepared > 1)"],
        ["focus window unguarded", "reset_window_mode", "device->creation.hFocusWindow }", "NULL }"],
        ["mode failure swallowed", "reset_window_mode", "result = claim_fullscreen_foreground(device, window);", "claim_fullscreen_foreground(device, window); result = TRUE;"],
        ["16-bit fallback omitted", "claim_fullscreen_foreground", "if (result != DISP_CHANGE_SUCCESSFUL) {", "if (0 && result != DISP_CHANGE_SUCCESSFUL) {"],
        ["saved styles replaced by fullscreen styles", "claim_fullscreen_foreground", "if (device->styled_window != window)", "if (1)"],
        ["active mode ownership lost after failure", "claim_fullscreen_foreground", "if (result != DISP_CHANGE_SUCCESSFUL)\n        return FALSE;", "if (result != DISP_CHANGE_SUCCESSFUL) { device->display_mode_changed = FALSE; return FALSE; }"],
        ["desktop restoration failure swallowed", "restore_display_mode", "if (ChangeDisplaySettingsA(NULL, 0) != DISP_CHANGE_SUCCESSFUL)", "if (0 & (ChangeDisplaySettingsA(NULL, 0) != DISP_CHANGE_SUCCESSFUL))"],
    ]) {
        const file = dir + "/frontend.c"; fs.writeFileSync(file, mutate(source, fn, old, replacement));
        run(label, "d9wg_reset_mode_entry_test", {D9WG_RESET_MODE_FRONTEND: file}); killed++;
    }
    for (const [label, old, replacement] of [
        ["Reset ignores mode failure", "if (!reset_window_mode(device, window))", "if (0 & !reset_window_mode(device, window))"],
        ["Reset omits updated window report", "emit_window_state(device, window);", ""],
    ]) {
        const file = dir + "/frontend.c"; fs.writeFileSync(file, mutate(source, "device_reset", old, replacement));
        run(label, "d9wg_reset_status_test", {D9WG_RESET_STATUS_FRONTEND: file}); killed++;
    }
    console.log("PASS Reset mode mutations: " + killed + "/25; message dispatch/lifetime/Unicode/nesting, mode/style/foreground cleanup and both public Reset failure/report boundaries");
} finally { fs.rmSync(dir, {recursive: true, force: true}); }
