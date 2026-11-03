"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../ddrawproxy/ddraw_proxy.c"), "utf8"), start = source.indexOf("static HRESULT WINAPI palette_SetEntries("), end = source.indexOf("\n/* ------------------------------------------------------------------", start), body = source.slice(start, end), dir = fs.mkdtempSync(path.join(os.tmpdir(), "ddraw-palette-mutants-"));
const cases = [
    ["visible palette never presents", "if (palette->owner &&", "if (0 && palette->owner &&"],
    ["unrelated palette presents", "palette->owner->primary->palette == palette", "(palette->owner->primary->palette == palette || 1)"],
    ["true-color primary presents", "palette->owner->primary->indexed &&", "(palette->owner->primary->indexed || 1) &&"],
    ["table not emitted", "emit_palette_table(palette);", "if (0) emit_palette_table(palette);"],
    ["resolve skipped", "!emit_blt_to_screen(palette->owner->primary, NULL)", "(0 && !emit_blt_to_screen(palette->owner->primary, NULL))"],
    ["Present skipped", "!emit_present_and_flush(palette->owner)", "(0 && !emit_present_and_flush(palette->owner))"],
    ["resolve failure ignored", "!emit_blt_to_screen(palette->owner->primary, NULL)", "(!emit_blt_to_screen(palette->owner->primary, NULL) && 0)"],
    ["Present failure ignored", "!emit_present_and_flush(palette->owner)", "(!emit_present_and_flush(palette->owner) && 0)"],
    ["partial update changes wrong entries", "palette->entries[start + index] = entries[index];", "palette->entries[index] = entries[index];"],
    ["empty end range refused", "start > 256u", "start >= 256u"],
];
try {for (const [label, before, after] of cases) {assert.equal(body.split(before).length, 2, label); const file = dir + "/proxy.c"; fs.writeFileSync(file, source.slice(0, start) + body.replace(before, after) + source.slice(end)); const r = spawnSync(process.execPath, [path.join(__dirname, "ddraw_palette_presentation_test.js")], {env: {...process.env, DDRAW_PALETTE_PRESENT_SOURCE: file}, encoding: "utf8"}); assert.notEqual(r.status, 0, "survived " + label); assert.match(r.stderr, /DDraw palette line/, "runtime assertion after strict compilation required: " + label + "\n" + r.stderr); console.log("KILL " + label);} console.log("PASS DDraw palette presentation mutations: 10/10, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
