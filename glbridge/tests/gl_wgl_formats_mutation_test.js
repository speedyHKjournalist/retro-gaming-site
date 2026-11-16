"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), { spawnSync } = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8"), directory = fs.mkdtempSync(path.join(os.tmpdir(), "gl-wgl-format-mutants-"));
const mutants = [
    ["memory DC accepted", "if (type != OBJ_DC) {", "if (type == OBJ_MEMDC) return TRUE; if (type != OBJ_DC) {"],
    ["unset format treated as set", "if (require_format && !pixel_format_valid(format))", "if (FALSE && require_format && !pixel_format_valid(format))"],
    ["format can change on a window", "(previous && previous!=format)", "FALSE"],
    ["alpha counted as RGB", "pfd->cColorBits=24;", "pfd->cColorBits=32;"],
    ["all minimum constraints use equality", "value>=requested", "value==requested"],
    ["boolean constraint ignored", "if (kind==2) return !!value == (requested != 0.0);", "if (kind==2) return TRUE;"],
    ["float constraints ignored", "if (!pixel_attribute_matches(format,(int)floats[i],floats[i+1])) match=FALSE;", "if (FALSE) match=FALSE;"],
    ["unknown attribute accepted", "if (!pixel_attribute_kind(integers[i]))", "if (FALSE)"],
    ["query layer ignored", "if (layer && !pixel_attribute_ignores_layer(attribute))", "if (FALSE && layer && !pixel_attribute_ignores_layer(attribute))"],
    ["number-of-formats validates ignored index", "if (attribute==0x2000) continue;", "if (FALSE && attribute==0x2000) continue;"],
    ["single buffer marked double", "g_pixel_formats[format-1].double_buffer?PFD_DOUBLEBUFFER:0", "PFD_DOUBLEBUFFER"],
    ["context format mismatch accepted", "if (context->pixel_format != format) {", "if (FALSE) {", "gl_wgl_lifecycle_abi_test.js"],
];
try {
    const env = { ...process.env };delete env.V86GL_PROXY_SOURCE;
    for (const fixture of ["gl_wgl_formats_abi_test.js", "gl_wgl_lifecycle_abi_test.js"]) {
        const result = spawnSync(process.execPath, [path.join(__dirname, fixture)], { encoding: "utf8", timeout: 60000, env });assert.equal(result.status, 0, "pristine " + fixture + "\n" + result.stderr);
    }
    for (const [name, before, after, fixture = "gl_wgl_formats_abi_test.js"] of mutants) {
        assert.equal(source.split(before).length - 1, 1, name + " has one current anchor");
        const file = path.join(directory, "opengl32_proxy.c");fs.writeFileSync(file, source.replace(before, after));
        const result = spawnSync(process.execPath, [path.join(__dirname, fixture)], { encoding: "utf8", timeout: 60000, env: { ...env, V86GL_PROXY_SOURCE: file } });assert.equal(result.error, undefined, name + " finishes");assert.notEqual(result.status, 0, name + " survived");
    }
    console.log("PASS WGL format mutations: " + mutants.length + "/" + mutants.length + " killed, no stale anchors");
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
