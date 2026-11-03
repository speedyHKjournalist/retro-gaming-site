"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../ddrawproxy/ddraw_proxy.c"), "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "ddraw-primary-mutants-"));
const cases = [
    ["windowed primary not clipped", "if (source->primary && (object->cooperative_flags & DDSCL_NORMAL))", "if (FALSE && source->primary && (object->cooperative_flags & DDSCL_NORMAL))"],
    ["fullscreen clipped as windowed", "if (source->primary && (object->cooperative_flags & DDSCL_NORMAL))", "if (source->primary && ((object->cooperative_flags & DDSCL_NORMAL) || TRUE))"],
    ["offscreen surface clipped as primary", "if (source->primary && (object->cooperative_flags & DDSCL_NORMAL))", "if ((source->primary || TRUE) && (object->cooperative_flags & DDSCL_NORMAL))"],
    ["dirty intersection ignored", "if (dirty) {", "if (FALSE && dirty) {"],
    ["clipper window ignored", "? source->clipper->window : object->window;", "? object->window : object->window;"],
    ["failed origin accepted", "if (!probe.client_succeeded || !probe.origin_succeeded)", "if (!probe.client_succeeded || (FALSE && !probe.origin_succeeded))"],
    ["window right overflow", "right = (int64_t)probe.state.window_x + probe.state.client_width;", "right = (int32_t)((uint32_t)probe.state.window_x + probe.state.client_width);"],
    ["screen emitter bypasses clipped area", "if (!primary_present_area(source, dirty_rect, &area))", "area = (RECT){0,0,(LONG)source->width,(LONG)source->height}; if (FALSE && !primary_present_area(source, dirty_rect, &area))"],
    ["flip-chain front never presents", "return !surface->primary ||\n        (emit_blt_to_screen", "return !surface->primary || surface->flip_next != NULL ||\n        (emit_blt_to_screen"],
    ["Unlock skips presentation", "if (!present_changed_primary(surface, &uploaded))", "if (FALSE && !present_changed_primary(surface, &uploaded))"],
    ["ReleaseDC skips presentation", "if (present && !present_changed_primary(surface, NULL))", "if (FALSE && present && !present_changed_primary(surface, NULL))"],
    ["resolve failure ignored", "(emit_blt_to_screen(surface, dirty) &&", "((emit_blt_to_screen(surface, dirty) || TRUE) &&"],
    ["Present failure ignored", "emit_present_and_flush(surface->owner));", "(emit_present_and_flush(surface->owner) || TRUE));"],
];
try {for (const [label, before, after] of cases) {assert.equal(source.split(before).length, 2, label); const file = dir + "/proxy.c"; fs.writeFileSync(file, source.replace(before, after)); const r = spawnSync(process.execPath, [path.join(__dirname, "ddraw_primary_write_test.js")], {env: {...process.env, DDRAW_PRIMARY_WRITE_SOURCE: file}, encoding: "utf8"}); assert.notEqual(r.status, 0, "survived " + label); assert.match(r.stderr, /DDraw primary line/, "strict compile and runtime assertion required: " + label + "\n" + r.stderr); console.log("KILL " + label);} console.log("PASS DDraw primary write mutations: 13/13, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
