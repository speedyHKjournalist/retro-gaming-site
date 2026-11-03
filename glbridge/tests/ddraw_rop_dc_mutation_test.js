"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../ddrawproxy/ddraw_proxy.c"), "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "ddraw-rop-dc-mutants-"));
const cases = [
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
try {for (const [label, before, after, occurrence] of cases) {const count = source.split(before).length - 1; assert(occurrence === 0 ? count >= 1 : count === 1, label); const file = dir + "/proxy.c"; fs.writeFileSync(file, source.replace(before, after)); const r = spawnSync(process.execPath, [path.join(__dirname, "ddraw_rop_dc_test.js")], {env: {...process.env, DDRAW_ROP_DC_SOURCE: file}, encoding: "utf8"}); assert.notEqual(r.status, 0, "survived " + label); assert.match(r.stderr, /DDraw ROP\/DC line/, "strict compile and runtime assertion required: " + label + "\n" + r.stderr); console.log("KILL " + label);} console.log("PASS DDraw ROP/DC mutations: 11/11, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
