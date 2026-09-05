#!/usr/bin/env node
"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const read = relative => fs.readFileSync(path.join(root, relative), "utf8");

const shared = read("diagnostic_trace.h");
const d3d8 = read("d3d8proxy/d3d8_proxy.c");
const ddraw = [
    read("ddrawproxy/ddraw_proxy.c"),
    read("ddrawproxy/d3d7_proxy.inc"),
    read("ddrawproxy/d3d_legacy_proxy.inc"),
].join("\n");
const d3d8Build = read("d3d8proxy/build_diagnostic.sh");
const ddrawBuild = read("ddrawproxy/build_diagnostic.sh");
const opengl = read("openglproxy/opengl32_proxy.c");
const openglBuild = read("openglproxy/build_diagnostic.sh");

assert.match(shared, /AddVectoredExceptionHandler/,
    "diagnostic DLLs must record first-chance exceptions");
assert.match(shared, /EXCEPTION code=%08lX[\s\S]*?eip=%08lX[\s\S]*?esp=%08lX/,
    "the XP x86 exception trace must include instruction and stack pointers");
assert.match(shared, /HRESULT function=%s line=%d hr=%08lX/,
    "HRESULT failures must identify their source site");
assert.match(shared, /MEM reason=%s/,
    "process attach/detach must include guest memory state");
assert.match(shared, /GetTempPathA/,
    "a read-only application directory needs a trace fallback");
assert.match(shared,
    /g_v86wg_trace_exe_path,[\s\S]*?v86wg_diagnostic_try_directory\(directory\)[\s\S]*?g_v86wg_trace_self_path/,
    "the trace must prefer the game executable directory before the DLL directory");
assert.match(shared, /FILE_APPEND_DATA[\s\S]*?OPEN_ALWAYS/,
    "a second DLL attach in the same PID must append instead of erasing trace");
assert.doesNotMatch(shared, /CREATE_ALWAYS/,
    "diagnostic trace files must never be truncated on DLL reload");
assert.match(shared, /DBG_PRINTEXCEPTION_C[\s\S]*?EXCEPTION_CONTINUE_SEARCH/,
    "OutputDebugString exceptions must be filtered from the exception trace");
assert.match(shared, /RESULT function=%s line=%d hr=%08lX/,
    "selected display calls must record successful returns as well as errors");

assert.match(d3d8Build, /-DD8WG_DIAGNOSTIC_TRACE=1/);
assert.match(ddrawBuild, /-DDDWG_DIAGNOSTIC_TRACE=1/);
assert.match(openglBuild, /-DV86GL_DIAGNOSTIC_TRACE=1/);
assert.match(d3d8Build, /d3d8-diagnostic\.dll/);
assert.match(ddrawBuild, /ddraw-diagnostic\.dll/);
assert.match(openglBuild, /opengl32-diagnostic\.dll/);
assert.match(opengl, /v86wg_diagnostic_process_attach\(hinst\)/,
    "the OpenGL diagnostic build must open its trace from DllMain");
assert.match(opengl, /PROCESS_DETACH[\s\S]*?v86wg_diagnostic_process_detach/,
    "the OpenGL diagnostic build must flush its trace at process detach");

for (const [name, source, macro, minimum] of [
    ["D3D8", d3d8, "D8WG_TRACE_ERROR", 300],
    ["DirectDraw/D3D1-7", ddraw, "DDWG_TRACE_ERROR", 450],
]) {
    const wrapped = [...source.matchAll(new RegExp("\\b" + macro + "\\(", "g"))];
    assert.ok(wrapped.length >= minimum,
        name + " has too few traced error sites: " + wrapped.length);
    assert.doesNotMatch(source,
        /return\s+(?:(?:DD|D3D)ERR_[A-Z0-9_]+|E_[A-Z0-9_]+|CLASS_E_[A-Z0-9_]+)\s*;/,
        name + " contains a bare explicit HRESULT failure return");
    assert.match(source, /v86wg_diagnostic_process_attach\(instance\)/,
        name + " diagnostic build must open its trace at process attach");
    assert.match(source,
        /v86wg_diagnostic_process_detach\(reserved, pending_commands\)/,
        name + " diagnostic build must flush its tail at process detach");
}

assert.match(d3d8, /TRANSPORT OPEN_FAIL[\s\S]*?win32=%lu/,
    "D3D8 must preserve the Win32 reason when v86gl.sys is unavailable");
assert.match(d3d8, /TRANSPORT MAP_FAIL[\s\S]*?win32=170/,
    "D3D8 must distinguish a busy DMA mapping from a missing driver");
assert.match(d3d8, /TRANSPORT SUBMIT_FAIL[\s\S]*?descriptor_bytes=%lu/,
    "D3D8 must trace failed IOCTL submissions with batch context");
assert.match(d3d8,
    /SHADER VALIDATE REFUSE kind=%s minor=%u offset=%u[\s\S]*?opcode=%04X/,
    "D3D8 shader rejection must identify the opcode and token offset");
assert.match(ddraw, /could not open [^\n]{0,24}v86gl \(Win32 error %lu\)/,
    "DirectDraw must preserve the Win32 reason when v86gl.sys is unavailable");
assert.match(ddraw,
    /#define UNSUPPORTED\(name, result\)[\s\S]*?DDWG_TRACE_ERROR\(result\)/,
    "deliberate DirectDraw and legacy D3D refusals must enter the trace");
for (const marker of [
    "CALL DirectDrawCreate",
    "CALL ddraw_SetCooperativeLevel",
    "CALL ddraw_SetDisplayMode",
    "CALL ddraw_CreateSurface",
    "CALL surface_Lock",
    "CALL surface_Unlock",
    "CALL surface_Blt",
    "CALL surface_Flip",
    "SURFACE STORAGE_CREATE",
    "SURFACE UPLOAD_BEGIN",
    "PRESENT BEGIN",
    "BATCH SUBMIT_BEGIN",
    "TRANSPORT OPEN_OK",
]) {
    assert.ok(ddraw.includes(marker),
        "DirectDraw diagnostic trace is missing marker: " + marker);
}
assert.match(ddraw, /count <= 8[\s\S]*?count - 1[\s\S]*?POLL name=%s/,
    "busy polling must be sampled instead of producing an unbounded log");

console.log("legacy_proxy_diagnostic_trace_test: ok (" +
    [...d3d8.matchAll(/\bD8WG_TRACE_ERROR\(/g)].length + " D3D8, " +
    [...ddraw.matchAll(/\bDDWG_TRACE_ERROR\(/g)].length +
    " DirectDraw/D3D1-7 traced sites)");
