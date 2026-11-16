"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../ddrawproxy/ddraw_proxy.c"), "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "ddraw-lock-mutants-"));
const cases = [
    ["legacy factory inherits extended unchecked locks", "surface->owner->extended_factory &&", "TRUE &&"],
    ["system texture size checks stay strict", "if (!system_texture && output_size != sizeof(DDSURFACEDESC)", "if (TRUE && output_size != sizeof(DDSURFACEDESC)"],
    ["system texture rectangles stay strict", "if (!system_texture && (rect->left < 0", "if (TRUE && (rect->left < 0"],
    ["video textures use unchecked locks", "(DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY)) ==\n            (DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY);", "DDSCAPS_TEXTURE) == DDSCAPS_TEXTURE;"],
    ["offscreen sysmem uses unchecked locks", "(DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY)) ==\n            (DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY);", "DDSCAPS_SYSTEMMEMORY) == DDSCAPS_SYSTEMMEMORY;"],
    ["caller descriptor size replaced", "description->dwSize = output_size;", "description->dwSize = sizeof(DDSURFACEDESC2);"],
    ["oversized descriptions use legacy shape", "if (output_size < sizeof(DDSURFACEDESC2))", "if (output_size != sizeof(DDSURFACEDESC2))"],
    ["undersized descriptions use modern shape", "if (output_size < sizeof(DDSURFACEDESC2))", "if (output_size == sizeof(DDSURFACEDESC))"],
    ["legacy thunk reintroduces strict size checks", "return surface_Lock(SURFACE7(iface), rect, \\\n            (DDSURFACEDESC2 *)description, flags, event);", "if (description && description->dwSize != sizeof(DDSURFACEDESC) && description->dwSize != sizeof(DDSURFACEDESC2)) return DDERR_INVALIDPARAMS; \\\n    return surface_Lock(SURFACE7(iface), rect, \\\n            (DDSURFACEDESC2 *)description, flags, event);"],
    ["zero-area rectangles rejected", "rect->right < rect->left || rect->bottom < rect->top", "rect->right <= rect->left || rect->bottom <= rect->top"],
    ["duplicate Lock accepted", "if (surface->dc || surface->lock_depth)", "if (surface->dc)"],
    ["invalid rectangle preserves stale pointer", "rect->right < rect->left || rect->bottom < rect->top)) {\n            description->lpSurface = NULL;", "rect->right < rect->left || rect->bottom < rect->top)) {\n            description->lpSurface = (void*)1;"],
    ["narrow descriptor overwritten", "    else *description = output;", "    *description = output;"],
    ["WRITEONLY skips GPU preservation", "if (surface->memory->gpu_dirty && !surface_readback(surface))", "if (surface->memory->gpu_dirty && !(flags & DDLOCK_WRITEONLY) && !surface_readback(surface))"],
    ["all locks skip GPU preservation", "if (surface->memory->gpu_dirty && !surface_readback(surface))", "if (FALSE && surface->memory->gpu_dirty && !surface_readback(surface))"],
    ["readback failure exposes stale shadow", "if (surface->memory->gpu_dirty && !surface_readback(surface))", "if (surface->memory->gpu_dirty && !surface_readback(surface) && FALSE)"],
    ["invalid rectangle triggers readback", "    if (rect) {\n        if (!system_texture && (rect->left < 0", "    if (surface->memory->gpu_dirty) surface_readback(surface);\n    if (rect) {\n        if (!system_texture && (rect->left < 0"],
    ["failed lock changes caller description", "    if (surface->memory->gpu_dirty && !surface_readback(surface))", "    *description = surface->desc;\n    if (surface->memory->gpu_dirty && !surface_readback(surface))"],
    ["failed lock commits flags", "    if (surface->memory->gpu_dirty && !surface_readback(surface))", "    surface->lock_flags = flags;\n    if (surface->memory->gpu_dirty && !surface_readback(surface))"],
    ["READONLY unexpectedly uploads", "if (!(surface->lock_flags & DDLOCK_READONLY)) {", "if (TRUE || !(surface->lock_flags & DDLOCK_READONLY)) {"],
    ["WRITEONLY silently drops upload", "if (!(surface->lock_flags & DDLOCK_READONLY)) {", "if (!(surface->lock_flags & (DDLOCK_READONLY | DDLOCK_WRITEONLY))) {"],
    ["partial upload covers entire surface", "    uploaded = surface->locked_rect;", "    uploaded = (RECT){0,0,(LONG)surface->width,(LONG)surface->height};"],
    ["partial lock ignores row offset", "(uintptr_t)(intptr_t)top * surface->pitch", "(uintptr_t)(intptr_t)top * 0u"],
];
try {for (const [label, before, after] of cases) {const start=source.indexOf("static HRESULT WINAPI surface_Lock("), end=source.indexOf("\n}",start)+2,lock=source.slice(start,end),segment=lock.includes(before)?lock:source; assert.equal(segment.split(before).length,2,label); const file = dir + "/proxy.c"; fs.writeFileSync(file, source.replace(segment,segment.replace(before, after))); const r = spawnSync(process.execPath, [path.join(__dirname, "ddraw_lock_preservation_test.js")], {env: {...process.env, DDRAW_LOCK_SOURCE: file}, encoding: "utf8"}); assert.notEqual(r.status, 0, "survived " + label); assert.match(r.stderr, /DDraw lock line/, "strict compilation and runtime failure required: " + label + "\n" + r.stderr); console.log("KILL " + label);} console.log("PASS DDraw lock preservation mutations: "+cases.length+"/"+cases.length+", strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
