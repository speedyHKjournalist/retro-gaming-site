#!/usr/bin/env node
// Checks the host's view of the OpenGL wire format against the guest that
// produces it.
//
// The opcodes, control records, flat argument lists, fixed record headers and
// protocol constants come from v86's gl_protocol.js (openglproxy/gl_protocol.h
// is generated from it); the GL enum values in gl_constants.js come from the
// guest's #defines. A single wrong field decodes every later argument at the
// wrong offset -- with a symptom that looks like anything at all -- so this
// file checks what is still written twice.

"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");
const wire = require(require("./host_paths.js").hostPath("gl-webgpu/gl_protocol.js"));
const constants = require(require("./host_paths.js").hostPath("gl-webgpu/gl_constants.js"));

const proxyPath = path.join(__dirname, "..", "openglproxy", "opengl32_proxy.c");
const source = fs.readFileSync(proxyPath, "utf8");

let passed = 0;
const failures = [];
function test(name, fn) {
    try { fn(); ++passed; } catch (error) { failures.push([name, error]); }
}

test("the guest takes its opcodes and protocol constants from gl_protocol.h", () => {
    assert.ok(/#include "gl_protocol\.h"/.test(source), "opengl32_proxy.c includes gl_protocol.h");
    assert.ok(!/\bGLFN_[A-Z0-9_]+\s*=\s*\d+/.test(source), "the guest numbers no opcode itself");
    for (const [name] of wire.DEFINES)
        assert.ok(!new RegExp("#define\\s+" + name + "\\b").test(source), "the guest defines " + name + " itself");
    assert.strictEqual(Object.keys(constants.GLFN).length, 222);
    assert.strictEqual(constants.GLFN, wire.GLFN, "gl_constants.js takes GLFN from gl_protocol.js");
    assert.strictEqual(constants.CTRL, wire.CTRL, "and CTRL");
});

// The window-state flags are D9WGWindowState's, which gl_executor.js decodes
test("the window-state flags match D9WG's", () => {
    const d3d9 = fs.readFileSync(path.join(__dirname, "../d3d9proxy/d3d9_protocol.h"), "utf8");
    const header = fs.readFileSync(path.join(__dirname, "../openglproxy/gl_protocol.h"), "utf8");
    for (const [gl, d9] of [["IS_WINDOW", "IS_WINDOW"], ["VISIBLE", "VISIBLE"], ["ICONIC", "ICONIC"],
            ["OCCLUDED", "OCCLUDED"], ["REGION", "REGION"]]) {
        const guest = header.match(new RegExp("#define V86GL_WINDOW_" + gl + " +\\(1u << (\\d+)\\)"));
        const d9wg = d3d9.match(new RegExp("#define D9WG_WINDOW_" + d9 + " +\\(1u << (\\d+)\\)"));
        assert.ok(guest && d9wg, gl + " is defined on both sides");
        assert.strictEqual(guest[1], d9wg[1], gl);
    }
});

test("every GL enum the host adds agrees with the guest where both define it", () => {
    assert.deepStrictEqual(constants.GL_CONFLICTS, [],
        "a registry value disagrees with the guest's own #define");
});

test("GL enum values match the guest's #define", () => {
    const defines = [...source.matchAll(
        /^#define\s+(GL_[A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+|\d+)\s*$/gm)];
    assert.ok(defines.length > 500, "the guest still defines the GL enums");
    for (const [, name, value] of defines) {
        const key = name.slice(3);
        const expected = value.startsWith("0x") ?
            parseInt(value, 16) : parseInt(value, 10);
        if (constants.GL[key] === undefined) continue;   // not all are needed
        assert.strictEqual(constants.GL[key], expected, name);
    }
});

test("every declarative signature is a contiguous argument list", () => {
    for (const [name, [glName, types]] of Object.entries(wire.SIGNATURES)) {
        assert.ok(/^[iufd]*$/.test(types),
            name + " has an unknown argument code: " + types);
        assert.ok(glName.length, name + " has no GL name");
        assert.strictEqual(wire.payloadBytes(types),
            [...types].reduce((n, t) => n + (t === "d" ? 8 : 4), 0));
    }
});

test("decodeArgs refuses a payload that is one byte short", () => {
    const types = "iiii";
    const buffer = new Uint8Array(15);
    const view = new DataView(buffer.buffer);
    const out = new Float64Array(8);
    assert.strictEqual(wire.decodeArgs(types, view, 0, 15, out), -1);
    const full = new Uint8Array(16);
    assert.strictEqual(
        wire.decodeArgs(types, new DataView(full.buffer), 0, 16, out), 4);
});

test("the response region matches the D9WG layout the D3D path uses", () => {
    const executor = require(require("./host_paths.js").hostPath("gl-webgpu/gl_executor.js"));
    // Both paths carve the same tail out of v86gl.sys's arena so that guest
    // memory looks the same whichever DLL is loaded (plan 6.2).
    assert.strictEqual(executor.GLWG_RESPONSE_REGION_BYTES, 4 * 1024 * 1024);
    assert.strictEqual(executor.GLWG_QUERY_REGION_BYTES, 16 * 1024);
    assert.strictEqual(executor.GLWG_READBACK_REGION_OFFSET, 16 * 1024);
    assert.strictEqual(executor.GLWG_HEARTBEAT_OFFSET,
        executor.GLWG_RESPONSE_REGION_BYTES - 16);
    const protocol = fs.readFileSync(
        path.join(__dirname, "..", "d3d9proxy", "d3d9_protocol.h"), "utf8");
    assert.ok(protocol.indexOf("D9WG_RESPONSE_REGION_BYTES (4u * 1024u * 1024u)") >= 0,
        "the D3D9 path still reserves the same four MiB");
});

/*
 * The hand-written records -- the ones with no declarative signature because
 * they carry a string, a blob or a synchronous answer -- are where the two
 * sides drifted apart without anyone noticing: the guest writes a struct, the
 * host reads constants, and a field inserted on one side moves every later
 * one on that side only. Nothing throws; the app is just handed the wrong
 * number, or a texture reads its pixels four bytes late.
 *
 * So the guest's struct is pinned here, field by field, next to the host
 * function that decodes it. Editing openglproxy fails this test, which is the
 * moment to open the matching handler.
 */
const GUEST_LAYOUTS = [
    ["emit_buffer_data", "BUFFER_DATA"],
    ["emit_buffer_sub_data", "BUFFER_SUB_DATA"],
    ["emit_draw_elements_direct", "DRAW_ELEMENTS_DIRECT"],
    ["glProgramStringARB", "PROGRAM_STRING_ARB"],
    ["emit_arb_program_parameter_fv", "PROGRAM_PARAMETER_FV_ARB"],
    ["emit_arb_program_parameter_dv", "PROGRAM_PARAMETER_DV_ARB"],
    ["emit_query_program_iv_arb", "QUERY_PROGRAM_IV_ARB"],
    ["emit_query_program_parameter_arb", "QUERY_PROGRAM_PARAMETER_FV_ARB"],
    ["emit_query_program_string_arb", "QUERY_PROGRAM_STRING_ARB"],
    ["emit_query_object_iv", "QUERY_OBJECT_IV"],
    ["emit_query_object_log", "QUERY_OBJECT_LOG"],
    ["emit_query_active", "QUERY_ACTIVE"],
    ["emit_query_location", "QUERY_LOCATION"],
    ["emit_query_uniform", "QUERY_UNIFORM"],
    ["emit_query_integer", "QUERY_INTEGER"],
    ["emit_query_error", "QUERY_ERROR"],
    ["emit_query_gl_string", "QUERY_GL_STRING"],
    ["emit_check_framebuffer_status", "CHECK_FRAMEBUFFER_STATUS"],
    ["emit_read_pixels", "READ_PIXELS"],
    ["glTexImage2D", "TEX_IMAGE_2D"],
    ["emit_vbo_attrib_pointer", "VERTEX_ATTRIB_POINTER_VBO"],
];

/* The first anonymous struct declared inside a guest function, as its field
 * names in order -- which is the same as their offsets, every field being a
 * 32-bit word except the ARB double parameters. */
function guestStructFields(functionName) {
    const at = source.search(new RegExp(
        "^[A-Za-z].*\\b" + functionName + "\\s*\\(", "m"));
    assert.ok(at >= 0, "openglproxy still defines " + functionName);
    const body = source.slice(at, at + 4000);
    let block = /struct \{\n([\s\S]*?)\n {4}\} /.exec(body);
    // A frequently emitted record may use a named payload typedef instead of
    // redeclaring the same anonymous struct in the function.  Pin that layout
    // too; otherwise the old regexp silently grabs the next unrelated struct
    // in the following function and reports a misleading protocol mismatch.
    const declaration = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s+payload\s*;/m.exec(body);
    if (declaration && (!block || declaration.index < block.index)) {
            const escaped = declaration[1].replace(/[.*+?^${}()|[\]\\]/g,
                "\\$&");
            block = new RegExp("typedef\\s+struct\\s*\\{\\n([^}]*)\\n\\}\\s*" +
                escaped + "\\s*;").exec(source);
    }
    assert.ok(block, functionName + " still declares its payload as a struct");
    return [...block[1].matchAll(
        /^\s*(?:const\s+)?[A-Za-z_][A-Za-z0-9_]*\s+([A-Za-z_][A-Za-z0-9_]*)/gm)]
        .map(match => match[1]);
}

test("the hand-written payload structs still have the fields the host decodes", () => {
    for (const [functionName, opcode] of GUEST_LAYOUTS) {
        assert.ok(constants.GLFN[opcode] !== undefined,
            opcode + " is not an opcode");
        assert.ok(wire.RECORDS[opcode], opcode + " has no fixed header in gl_protocol.js");
        // array fields count once, by name
        const fields = Object.keys(wire.RECORDS[opcode].fields);
        assert.deepStrictEqual(guestStructFields(functionName), fields,
            functionName + " changed shape: its struct and " + opcode +
            "'s header in gl_protocol.js have to agree");
    }
});

/* The two enumerations the guest and host both have to agree on by value, and
 * that a reader cannot tell apart by looking at either side alone. */
test("the object, location and parameter kinds are the numbers both sides were built with", () => {
    for (const [name, value] of [
            ["V86GL_OBJECT_KIND_SHADER", 1], ["V86GL_OBJECT_KIND_PROGRAM", 2],
            ["V86GL_OBJECT_KIND_QUERY", 3],
            ["V86GL_ACTIVE_KIND_UNIFORM", 1], ["V86GL_ACTIVE_KIND_ATTRIB", 2],
            ["V86GL_LOCATION_KIND_UNIFORM", 1], ["V86GL_LOCATION_KIND_ATTRIB", 2],
            ["V86GL_PROGRAM_PARAMETER_ENV", 1],
            ["V86GL_PROGRAM_PARAMETER_LOCAL", 2]]) {
        assert.strictEqual(wire.C[name], value, name + " is no longer " + value);
    }
});

test("the executor has a handler for every opcode it claims to implement", () => {
    const executor = require(require("./host_paths.js").hostPath("gl-webgpu/gl_executor.js"));
    const table = executor.buildHandlerTable();
    // buildHandlerTable() alone covers the state opcodes; the installers add
    // the rest, and the executor's constructor calls all of them.
    const { createFakeHost } = require(require("./host_paths.js").testPath("gl_fake_gpu.js"));
    const { host } = createFakeHost();
    const live = new executor.GLWebGPUExecutor(null, { host });
    let implemented = 0;
    const missing = [];
    for (const [name, opcode] of Object.entries(constants.GLFN)) {
        if (live.handlers[opcode]) ++implemented;
        else missing.push(name);
    }
    void table;
    assert.deepStrictEqual(missing, [],
        "every opcode the guest can send needs a handler or an explicit refusal");
    assert.strictEqual(implemented, 222);
});

for (const [name, error] of failures)
    console.error("FAIL: " + name + "\n    " + (error && error.message));
console.log(passed + " passed, " + failures.length + " failed");
process.exit(failures.length ? 1 : 0);
