"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../ddrawproxy/ddraw_proxy.c"), "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "ddraw-lock-mutants-"));
const cases = [
    ["WRITEONLY skips GPU preservation", "if (surface->memory->gpu_dirty && !surface_readback(surface))", "if (surface->memory->gpu_dirty && !(flags & DDLOCK_WRITEONLY) && !surface_readback(surface))"],
    ["all locks skip GPU preservation", "if (surface->memory->gpu_dirty && !surface_readback(surface))", "if (FALSE && surface->memory->gpu_dirty && !surface_readback(surface))"],
    ["readback failure exposes stale shadow", "if (surface->memory->gpu_dirty && !surface_readback(surface))", "if (surface->memory->gpu_dirty && !surface_readback(surface) && FALSE)"],
    ["invalid rectangle triggers readback", "    if (rect) {\n        if (rect->left < 0", "    if (surface->memory->gpu_dirty) surface_readback(surface);\n    if (rect) {\n        if (rect->left < 0"],
    ["failed lock changes caller description", "    if (surface->memory->gpu_dirty && !surface_readback(surface))", "    *description = surface->desc;\n    if (surface->memory->gpu_dirty && !surface_readback(surface))"],
    ["failed lock commits flags", "    if (surface->memory->gpu_dirty && !surface_readback(surface))", "    surface->lock_flags = flags;\n    if (surface->memory->gpu_dirty && !surface_readback(surface))"],
    ["READONLY unexpectedly uploads", "if (!(surface->lock_flags & DDLOCK_READONLY)) {", "if (TRUE || !(surface->lock_flags & DDLOCK_READONLY)) {"],
    ["WRITEONLY silently drops upload", "if (!(surface->lock_flags & DDLOCK_READONLY)) {", "if (!(surface->lock_flags & (DDLOCK_READONLY | DDLOCK_WRITEONLY))) {"],
    ["partial upload covers entire surface", "    uploaded = surface->locked_rect;", "    uploaded = (RECT){0,0,(LONG)surface->width,(LONG)surface->height};"],
    ["partial lock ignores row offset", "surface->shadow + top * surface->pitch", "surface->shadow + top * 0u"],
];
try {for (const [label, before, after] of cases) {assert.equal(source.split(before).length, 2, label); const file = dir + "/proxy.c"; fs.writeFileSync(file, source.replace(before, after)); const r = spawnSync(process.execPath, [path.join(__dirname, "ddraw_lock_preservation_test.js")], {env: {...process.env, DDRAW_LOCK_SOURCE: file}, encoding: "utf8"}); assert.notEqual(r.status, 0, "survived " + label); assert.match(r.stderr, /DDraw lock line/, "strict compilation and runtime failure required: " + label + "\n" + r.stderr); console.log("KILL " + label);} console.log("PASS DDraw lock preservation mutations: 10/10, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
