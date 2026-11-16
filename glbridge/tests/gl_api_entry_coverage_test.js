"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), { execFileSync } = require("node:child_process");
const root = path.join(__dirname, "../openglproxy"), preprocessed = execFileSync(process.env.V86GL_CC || "i686-w64-mingw32-gcc", ["-E", "-P", "-std=gnu99", path.join(root, "opengl32_proxy.c")], { encoding: "utf8", maxBuffer: 16 * 1024 * 1024 });
const definitions = new Map([...preprocessed.matchAll(/__attribute__\(\(__stdcall__\)\)\s+((?:gl|wgl)\w+)\s*\([^;{}]*\)\s*\{/g)].map(match => [match[1], preprocessed.slice(match.index + match[0].length, match.index + match[0].length + 200)]));
const exported_entries = [...fs.readFileSync(path.join(root, "opengl32.def"), "utf8").matchAll(/^\s*((?:gl|wgl)\w+)@\d+/gm)].map(match => match[1]);
assert.equal(exported_entries.length, 831);assert.equal(new Set(exported_entries).size, 831);
for (const name of exported_entries) {
    assert.ok(definitions.has(name), name + " has a real ABI definition");
    if (name.startsWith("gl")) assert.match(definitions.get(name), /if \(!g_loaded_context\) return(?: \(0\))?;/, name + " ignores calls without a current context before touching arguments");
    assert.match(definitions.get(name), /^\s*V86GLAPIGuard\s+v86gl_call_guard\s+__attribute__\(\(cleanup\(v86gl_leave_api\),unused\)\)\s*=\s*v86gl_enter_api\(\);/, name + " takes the recursive call guard before reading arguments or mutable state");
}
console.log("PASS all 831 preprocessed GL/WGL entry points, including generated aliases, acquire the call guard");
