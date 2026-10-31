"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), { spawnSync } = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8"), fixture = path.join(__dirname, "gl_wgl_lifecycle_abi_test.js"), directory = fs.mkdtempSync(path.join(os.tmpdir(), "gl-wgl-lifecycle-mutants-"));
const mutants = [
    ["failed binding retains current context", "release_current_binding();SetLastError(error);return FALSE;", "SetLastError(error);return FALSE;"],
    ["NULL context incorrectly validates HDC", "if (!ctx) {release_current_binding();SetLastError(ERROR_SUCCESS);return TRUE;}", "if (!hdc && !ctx) {release_current_binding();SetLastError(ERROR_SUCCESS);return TRUE;}"],
    ["own-current deletion is rejected", "if (context->owner_thread && context->owner_thread != GetCurrentThreadId()) {", "if (context->owner_thread) {"],
    ["foreign-current deletion is accepted", "if (context->owner_thread && context->owner_thread != GetCurrentThreadId()) {", "if (FALSE) {"],
    ["no-context pointer is read", "#define V86GL_GL_CALL_VOID() V86GL_API_CALL(); if (!g_loaded_context) return", "#define V86GL_GL_CALL_VOID() V86GL_API_CALL()"],
    ["no-context query executes", "#define V86GL_GL_CALL_VALUE(value) V86GL_API_CALL(); if (!g_loaded_context) return (value)", "#define V86GL_GL_CALL_VALUE(value) V86GL_API_CALL()"],
];
try {
    const env = { ...process.env };delete env.V86GL_PROXY_SOURCE;
    const baseline = spawnSync(process.execPath, [fixture], { encoding: "utf8", timeout: 60000, env });assert.equal(baseline.status, 0, "pristine lifecycle fixture\n" + baseline.stderr);
    for (const [name, before, after] of mutants) {
        assert.equal(source.split(before).length - 1, 1, name + " has one current anchor");
        const file = path.join(directory, "opengl32_proxy.c");fs.writeFileSync(file, source.replace(before, after));
        const result = spawnSync(process.execPath, [fixture], { encoding: "utf8", timeout: 60000, env: { ...env, V86GL_PROXY_SOURCE: file } });assert.equal(result.error, undefined, name + " fixture finishes");assert.notEqual(result.status, 0, name + " survived");
    }
    console.log("PASS WGL lifecycle mutations: " + mutants.length + "/" + mutants.length + " killed, no stale anchors");
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
