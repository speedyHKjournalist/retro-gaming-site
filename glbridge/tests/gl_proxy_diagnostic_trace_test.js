#!/usr/bin/env node
"use strict";

/*
 * The OpenGL diagnostic DLL must produce a usable log with no guest setup.
 * A guest game is started from a .bat behind a portable launcher and the v86
 * disk is a cold-booted in-memory overlay, so any environment variable the
 * trace depends on is both awkward to set and lost on the next run.  These
 * assertions lock that default in, together with the budget that keeps an
 * immediate-mode frame from turning the log into a memory leak.
 */

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const read = relative => fs.readFileSync(/^(gl-webgpu|d3d8-webgpu|d3d9-webgpu)\//.test(relative) ? require("./host_paths.js").hostPath(relative) : path.join(root, relative), "utf8");

const proxy = read("openglproxy/opengl32_proxy.c");
const build = read("openglproxy/build_diagnostic.sh");
const shipping = read("openglproxy/build.sh");
const readme = read("openglproxy/README.md");
const writer = read("diagnostic_trace.h");

assert.match(build, /-DV86GL_DIAGNOSTIC_TRACE=1/,
    "the diagnostic build must define the trace switch");
assert.match(build, /opengl32-diagnostic\.dll/,
    "the diagnostic build must produce a separately named DLL");
assert.ok(!/V86GL_DIAGNOSTIC_TRACE/.test(shipping),
    "the shipping DLL must compile the file trace out entirely");

assert.match(proxy, /v86wg_diagnostic_process_attach\(hinst\)/,
    "the diagnostic build must open its trace at process attach");
assert.match(proxy, /v86wg_diagnostic_process_detach\(/,
    "the diagnostic build must flush its tail at process detach");
assert.match(proxy, /#define V86WG_DIAGNOSTIC_BUFFER_BYTES 65536u/,
    "only the OpenGL diagnostic build opts into bounded batched writes");
assert.match(writer, /g_v86wg_trace_flush_ms = 1000;/,
    "normal diagnostic frame checkpoints must default to one second");
assert.match(proxy, /g_trace_calls_this_frame = 0;[\s\S]*?v86wg_diagnostic_checkpoint\(\);/,
    "frame summaries must checkpoint rather than flush the disk every frame");
assert.match(writer, /static void v86wg_diagnostic_process_detach[\s\S]*?v86wg_diagnostic_flush\(\);/,
    "normal exit must drain the batch before closing the log");
assert.match(readme, /V86GL_TRACE_FLUSH_MS=0/,
    "the synchronous diagnostic escape hatch must remain documented");

/* The default: tracing without an environment variable. */
assert.match(proxy,
    /#if V86GL_DIAG_ENABLED\s+g_trace_enabled = transport != V86GL_TRACE_FLAG_OFF;/,
    "the diagnostic build must trace unless the guest opts out");
assert.match(proxy,
    /g_trace_calls_enabled = g_trace_enabled && calls != V86GL_TRACE_FLAG_OFF;/,
    "per-call detail must be on by default in the diagnostic build");
assert.match(proxy, /#else\s+g_trace_enabled = g_trace_debug_string;/,
    "the shipping build must stay silent unless the guest asks");

/* V86GL_TRACE_CALLS keeps both of its escape hatches. */
assert.match(proxy, /V86GL_TRACE_FLAG_UNSET[\s\S]*?V86GL_TRACE_FLAG_OFF[\s\S]*?V86GL_TRACE_FLAG_ON/,
    "the trace flags must distinguish unset from an explicit 0");
assert.match(proxy, /g_trace_call_budget = calls == V86GL_TRACE_FLAG_ON \? 0u :/,
    "V86GL_TRACE_CALLS=1 must still remove the per-frame budget");

/* The budget itself. */
assert.match(proxy, /#define V86GL_TRACE_CALL_BUDGET_DEFAULT \d+u/,
    "the per-call trace must ship with a bounded default");
assert.match(proxy,
    /g_trace_calls_this_frame >= g_trace_call_budget[\s\S]*?g_trace_calls_suppressed\+\+/,
    "an exhausted budget must count what it drops");
assert.match(proxy, /CALLS suppressed=%lu budget=%lu/,
    "a frame that dropped per-call detail must say so");
assert.match(proxy, /g_trace_calls_this_frame = 0;/,
    "the per-call budget must be replenished each frame");

/* Every per-call site goes through the gate, none reads a raw flag. */
const gated = [...proxy.matchAll(/\bv86gl_trace_call_allowed\(\)/g)].length;
assert.ok(gated >= 30,
    "too few per-call trace sites route through the budget gate: " + gated);
assert.ok(!/if \(g_trace_calls(?:_enabled)?\)/.test(proxy),
    "a per-call trace site must not test the enable flag directly");
const rawFlagReads = [...proxy.matchAll(/\bg_trace_calls_enabled\b/g)].length;
/* Declaration, the two build-branch assignments, the gate's own read and the
 * MODE banner.  Anything more means a call site is bypassing the budget. */
assert.ok(rawFlagReads <= 5,
    "the per-call enable flag leaked outside the gate: " + rawFlagReads +
    " references");

/* OutputDebugString raises a first-chance exception per line with no debugger
 * attached, so the now-default trace must not pay for it. */
assert.match(proxy,
    /if \(force \|\| g_trace_debug_string\) \{\s+lstrcatA\(message, "\\r\\n"\);\s+OutputDebugStringA\(message\);/,
    "the debug-string path must stay behind the environment variable");

for (const marker of [
    "PROCESS_ATTACH", "MODE", "FRAME id=", "GLERROR", "HISTOGRAM",
    "SUBMIT_FAILED", "READBACK_TIMEOUT", "ARB_PROGRAM", "QUERY_BATCH",
    "SURFACE", "WGL",
]) {
    assert.ok(proxy.includes(marker),
        "OpenGL diagnostic summary is missing marker: " + marker);
}

assert.ok(!/only when a full per-record trace is required/.test(readme),
    "the README must no longer present tracing as opt-in");
assert.match(readme, /V86GL_TRACE_CALL_BUDGET/,
    "the README must document the per-frame budget");

console.log("gl_proxy_diagnostic_trace_test: ok (" + gated +
    " budgeted per-call sites)");
