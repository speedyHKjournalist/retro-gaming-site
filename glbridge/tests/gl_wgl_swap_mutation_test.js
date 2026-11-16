"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), { spawnSync } = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8"), fixture = path.join(__dirname, "gl_wgl_swap_abi_test.js"), directory = fs.mkdtempSync(path.join(os.tmpdir(), "gl-wgl-swap-mutants-"));
const mutants = [
    ["wrong HDC is presented", "submitted=emit_frame(hwnd);", "submitted=emit_frame(NULL);"],
    ["single buffer swaps", "if (!g_pixel_formats[format-1].double_buffer) return TRUE;", "if (FALSE) return TRUE;"],
    ["host interval ignored", "request.interval=(uint32_t)window_swap_interval(hwnd);", "request.interval=1;"],
    ["swap skips host response", "emit_pci_batch(FALSE) && wait_for_host_status(args+8,\"SwapBuffers\");", "emit_pci_batch(FALSE);"],
    ["negative interval accepted", "if (interval<0) {SetLastError(ERROR_INVALID_DATA);return FALSE;}", "if (FALSE) {SetLastError(ERROR_INVALID_DATA);return FALSE;}"],
    ["interval is not clamped", "if (interval>(int)V86GL_MAX_SWAP_INTERVAL)", "if (FALSE)"],
    ["interval has wrong default", "return stored ? (int)(stored-1u) : 1;", "return stored ? (int)(stored-1u) : 0;"],
    ["unsupported layers succeed", "if (planes!=WGL_SWAP_MAIN_PLANE)", "if (FALSE)"],
];
try {
    const env = { ...process.env };delete env.V86GL_PROXY_SOURCE;
    const baseline = spawnSync(process.execPath, [fixture], { encoding: "utf8", timeout: 60000, env });assert.equal(baseline.status, 0, "pristine swap fixture\n" + baseline.stderr);
    for (const [name, before, after] of mutants) {
        assert.equal(source.split(before).length - 1, 1, name + " has one current anchor");
        const file = path.join(directory, "opengl32_proxy.c");fs.writeFileSync(file, source.replace(before, after));
        const result = spawnSync(process.execPath, [fixture], { encoding: "utf8", timeout: 60000, env: { ...env, V86GL_PROXY_SOURCE: file } });assert.equal(result.error, undefined, name + " fixture finishes");assert.notEqual(result.status, 0, name + " survived");
    }
    console.log("PASS WGL swap mutations: " + mutants.length + "/" + mutants.length + " killed, no stale anchors");
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
