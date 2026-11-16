"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), { spawnSync } = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8"), fixture = path.join(__dirname, "gl_wgl_fonts_layers_abi_test.js"), directory = fs.mkdtempSync(path.join(os.tmpdir(), "gl-honest-mutants-"));
const mutants = [
    ["Unicode uses ANSI glyph API", "unicode ? GetGlyphOutlineW : GetGlyphOutlineA", "unicode ? GetGlyphOutlineA : GetGlyphOutlineA"],
    ["undefined Unicode glyph is not empty", "found!=GDI_ERROR && index==0xFFFFu", "FALSE"],
    ["bitmap rows are not flipped", "metrics.gmBlackBoxY - 1u - row", "row"],
    ["font inherits caller PBO", "glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);", "(void)0;"],
    ["font failure reports success", "error=GetLastError();ok=FALSE;break;", "error=GetLastError();ok=TRUE;break;"],
    ["font leaves unpack state changed", "glPopClientAttrib();\n    if (!ok)", "if (!ok)"],
    ["outline reports success without geometry", "SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return FALSE;\n}", "SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return TRUE;\n}"],
    ["single-buffer layer described as double", "g_pixel_formats[format-1].double_buffer?LPD_DOUBLEBUFFER:0", "LPD_DOUBLEBUFFER"],
    ["residency skips empty Begin check", "if (emit_query_parameter(V86GL_PARAMETER_TEXTURE_RESIDENCY,0,0,V86GL_STATE_BOOLEAN,&resident)<=0) return GL_FALSE;", "(void)resident;", "gl_texture_residency_abi_test.js"],
    ["residency skips name validation", "if (emit_query_parameter(V86GL_PARAMETER_TEXTURE_RESIDENCY,textures[i],1,V86GL_STATE_BOOLEAN,&values[i])<=0)", "if (FALSE)", "gl_texture_residency_abi_test.js"],
    ["residency overwrites all-resident output", "if (!all && residences) CopyMemory(residences,values,(SIZE_T)n);", "if (residences) CopyMemory(residences,values,(SIZE_T)n);", "gl_texture_residency_abi_test.js"],
    ["residency ignores false CPU answer", "if (!values[i]) all=GL_FALSE;", "if (FALSE) all=GL_FALSE;", "gl_texture_residency_abi_test.js"],
];
try {
    const env = { ...process.env };delete env.V86GL_PROXY_SOURCE;
    for (const pristine of [fixture, path.join(__dirname, "gl_texture_residency_abi_test.js")]) {const baseline = spawnSync(process.execPath, [pristine], { encoding: "utf8", timeout: 60000, env });assert.equal(baseline.status, 0, "pristine result fixture\n" + baseline.stderr);}
    for (const [name, before, after, test = "gl_wgl_fonts_layers_abi_test.js"] of mutants) {
        assert.equal(source.split(before).length - 1, 1, name + " has one current anchor");
        const file = path.join(directory, "opengl32_proxy.c");fs.writeFileSync(file, source.replace(before, after));
        const result = spawnSync(process.execPath, [path.join(__dirname, test)], { encoding: "utf8", timeout: 60000, env: { ...env, V86GL_PROXY_SOURCE: file } });assert.equal(result.error, undefined, name + " fixture finishes");assert.notEqual(result.status, 0, name + " survived");
    }
    console.log("PASS honest WGL/GL query mutations: " + mutants.length + "/" + mutants.length + " killed, no stale anchors");
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
