"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../ddrawproxy/ddraw_proxy.c"), "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "ddraw-rop-dc-mutants-"));
const cases = [
    ["compressed public effects silently accepted", "if (source && format_is_compressed(destination->format) &&", "if (FALSE && source && format_is_compressed(destination->format) &&"],
    ["compressed BltFast unknown flags accepted", "if (format_is_compressed(destination->format) &&", "if (FALSE && format_is_compressed(destination->format) &&"],
    ["cross-owner format conversion accepted", "if (source->owner != destination->owner && source->format != destination->format)", "if (FALSE && source->owner != destination->owner && source->format != destination->format)", 0],
    ["BltFast clipper rejection omitted", "if (destination->clipper) return DDWG_TRACE_ERROR(DDERR_BLTFASTCANTCLIP);", "if (FALSE && destination->clipper) return DDWG_TRACE_ERROR(DDERR_BLTFASTCANTCLIP);"],
    ["BltFast cross-format rejection omitted", "if (destination->clipper) return DDWG_TRACE_ERROR(DDERR_BLTFASTCANTCLIP);\n    if (source->owner != destination->owner && source->format != destination->format)", "if (destination->clipper) return DDWG_TRACE_ERROR(DDERR_BLTFASTCANTCLIP);\n    if (FALSE && source->owner != destination->owner && source->format != destination->format)"],
    ["missing clip list ignored", "if (FAILED(clip_result)) return DDWG_TRACE_ERROR(clip_result);", "if (FALSE && FAILED(clip_result)) return DDWG_TRACE_ERROR(clip_result);"],
    ["empty clip list becomes unclipped", "if (!clip_count && !destination->clipper)", "if (!clip_count)"],
    ["non-GDI format accepted", "(!surface->indexed && (!(surface->desc.ddpfPixelFormat.dwFlags & DDPF_RGB) ||", "(FALSE && (!(surface->desc.ddpfPixelFormat.dwFlags & DDPF_RGB) ||"],
    ["missing DC has wrong error", "if (!surface->dc) return DDWG_TRACE_ERROR(DDERR_NODC);", "if (!surface->dc) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);"],
    ["locked GetDC rejected", "    if (surface->memory->gpu_dirty && !surface_readback(surface))", "    if (surface->lock_depth) return DDWG_TRACE_ERROR(DDERR_SURFACEBUSY);\n    if (surface->memory->gpu_dirty && !surface_readback(surface))", 0],
    ["mixed depth/color copy accepted", "if (format_is_depth(source->format) != format_is_depth(destination->format))", "if (FALSE && format_is_depth(source->format) != format_is_depth(destination->format))"],
    ["mismatched depth layout copy accepted", "if (format_is_depth(source->format) && source->format != destination->format)", "if (FALSE && format_is_depth(source->format) && source->format != destination->format)"],
    ["color fill accepts depth", "if (!fx || source || format_is_depth(destination->format))", "if (!fx || source)"],
    ["depth fill ignores supplied source", "if (!fx || source) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);", "if (!fx) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);"],
    ["depth fill clamps instead of masks", "depth = (fill_color & mask) >> shift;", "depth = fill_color > maximum ? maximum : fill_color;"],
    ["invalid destination emitted", "if (effective_destination.left < 0 || effective_destination.top < 0 ||", "if (FALSE && (effective_destination.left < 0 || effective_destination.top < 0 ||"],
    ["source required before ROP", "    if (flags & DDBLT_ROP) {", "    if (!source) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);\n    if (flags & DDBLT_ROP) {"],
    ["WHITENESS becomes black", "fill_color = fx->dwROP == BLACKNESS ? 0 : destination->indexed ?", "fill_color = TRUE || fx->dwROP == BLACKNESS ? 0 : destination->indexed ?"],
    ["ARGB white sets alpha", "0xffu : destination->desc.ddpfPixelFormat.dwRBitMask |", "0xffu : 0xff000000u | destination->desc.ddpfPixelFormat.dwRBitMask |"],
    ["P8 white is zero", "0xffu : destination->desc.ddpfPixelFormat.dwRBitMask |", "0u : destination->desc.ddpfPixelFormat.dwRBitMask |"],
    ["unsupported ROP accepted", "if (fx->dwROP != SRCCOPY && fx->dwROP != BLACKNESS &&", "if (FALSE && fx->dwROP != SRCCOPY && fx->dwROP != BLACKNESS &&"],
    ["ROP mutates caller fill field", "fill_color = fx->dwROP == BLACKNESS ? 0 : destination->indexed ?", "fx->dwFillColor = 0;\n        fill_color = fx->dwROP == BLACKNESS ? 0 : destination->indexed ?"],
    ["primary overrides own palette", "if (!palette && surface->owner && surface->owner->primary)", "if ((TRUE || !palette) && surface->owner && surface->owner->primary)"],
    ["primary fallback omitted", "if (!palette && surface->owner && surface->owner->primary)", "if (FALSE && !palette && surface->owner && surface->owner->primary)"],
    ["DC red channel copied from blue", "info.table.colors[index].rgbRed =\n                        palette->entries[index].peRed;", "info.table.colors[index].rgbRed =\n                        palette->entries[index].peBlue;"],
    ["DC fallback binds surface palette", "        info.header.biClrUsed = 256;", "        surface->palette = palette;\n        info.header.biClrUsed = 256;"],
    ["failed ROP emission ignored", "return DDWG_TRACE_RESULT(ok ? DD_OK : DDERR_GENERIC);", "return DDWG_TRACE_RESULT((ok || TRUE) ? DD_OK : DDERR_GENERIC);", 0],
];
try {for (const [label, before, after, occurrence] of cases) {const count = source.split(before).length - 1; assert(occurrence === 0 ? count >= 1 : count === 1, label); const file = dir + "/proxy.c"; let mutant=source.replace(before, after);if(label==="invalid destination emitted")mutant=mutant.replace("effective_destination.bottom <= effective_destination.top)","effective_destination.bottom <= effective_destination.top))");fs.writeFileSync(file, mutant); const r = spawnSync(process.execPath, [path.join(__dirname, "ddraw_rop_dc_test.js")], {env: {...process.env, DDRAW_ROP_DC_SOURCE: file}, encoding: "utf8"}); assert.notEqual(r.status, 0, "survived " + label); assert.match(r.stderr, /DDraw ROP\/DC line/, "strict compile and runtime assertion required: " + label + "\n" + r.stderr); console.log("KILL " + label);} console.log("PASS DDraw ROP/DC mutations: "+cases.length+"/"+cases.length+", strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
