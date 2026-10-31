"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), { spawnSync } = require("node:child_process");
const sources = { proxy: fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8"), driver: fs.readFileSync(path.join(__dirname, "../v86gl_driver/virtio_transport.h"), "utf8") }, directory = fs.mkdtempSync(path.join(os.tmpdir(), "gl-transport-mutants-"));
const mutants = [
    ["proxy", "failed open permanently disables retry", "static int open_v86gl(void) {\n    V86GLMapBuffer", "static int open_v86gl(void) {static int attempts;if(attempts++)return 0;\n    V86GLMapBuffer", "gl_transport_recovery_abi_test.js"],
    ["proxy", "failed submit closes owner", "g_have_last_surface=FALSE;\n        reset_pci_stream();", "close_v86gl();g_have_last_surface=FALSE;\n        reset_pci_stream();", "gl_transport_recovery_abi_test.js"],
    ["proxy", "unknown acceptance replays failed commands", "reset_pci_stream();\n        SetLastError(error);", "SetLastError(error);", "gl_transport_recovery_abi_test.js"],
    ["proxy", "errors force debugger output", "if (g_trace_debug_string) {", "if (force || g_trace_debug_string) {", "gl_transport_diagnostics_abi_test.js"],
    ["proxy", "binding is not resent after failure", "target && (!g_have_last_surface || window_changed)", "target && window_changed", "gl_wgl_lifecycle_abi_test.js"],
    ["driver", "missing arena retains registration", "t->registered = FALSE; return STATUS_DEVICE_NOT_READY;", "return STATUS_DEVICE_NOT_READY;", "gl_transport_diagnostics_abi_test.js"],
    ["driver", "missing renderer loses retryable status", "if(reply->status == 3u) return STATUS_DEVICE_NOT_READY;", "if(reply->status == 3u) return STATUS_UNSUCCESSFUL;", "gl_transport_diagnostics_abi_test.js"],
];
try {
    const env = { ...process.env };delete env.V86GL_PROXY_SOURCE;delete env.V86GL_TRANSPORT_SOURCE;
    for (const fixture of new Set(mutants.map(entry => entry[4]))) { const result = spawnSync(process.execPath, [path.join(__dirname, fixture)], { encoding: "utf8", timeout: 60000, env });assert.equal(result.status, 0, "pristine " + fixture + "\n" + result.stderr); }
    for (const [kind, name, before, after, fixture] of mutants) {
        const source = sources[kind];assert.equal(source.split(before).length - 1, 1, name + " has one current anchor");
        const file = path.join(directory, kind + ".c");fs.writeFileSync(file, source.replace(before, after));
        const result = spawnSync(process.execPath, [path.join(__dirname, fixture)], { encoding: "utf8", timeout: 60000, env: { ...env, [kind === "proxy" ? "V86GL_PROXY_SOURCE" : "V86GL_TRANSPORT_SOURCE"]: file } });assert.equal(result.error, undefined, name + " finishes");assert.notEqual(result.status, 0, name + " survived");
    }
    console.log("PASS transport recovery mutations: " + mutants.length + "/" + mutants.length + " killed, no stale anchors");
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
