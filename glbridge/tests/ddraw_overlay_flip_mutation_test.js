"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../ddrawproxy/ddraw_proxy.c"), "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "ddraw-overlay-mutants-"));
const cases = [
    ["overlay overwrites primary base", "if (surface->primary && !emit_blt_to_screen(surface, NULL))", "if (!emit_blt_to_screen(surface, NULL))"],
    ["overlay source not rebound", "if (!emit_overlay_state(overlay)) return FALSE;", "if (FALSE && !emit_overlay_state(overlay)) return FALSE;"],
    ["destination relationship ignored", "!surface_flip_contains(root, overlay->overlay_destination)", "(TRUE || !surface_flip_contains(root, overlay->overlay_destination))"],
    ["hidden/offscreen Flip presents", "*visible = root->primary;", "*visible = root->primary || TRUE;"],
    ["named target selects first back", "surface_swap_flip_storage(surface, override_surface);", "surface_swap_flip_storage(surface, surface->flip_next);"],
    ["named self accepted", "override_surface == surface ||", "(override_surface == surface && FALSE) ||"],
    ["unrelated named target accepted", "!surface_flip_contains(surface, override_surface)", "(FALSE && !surface_flip_contains(surface, override_surface))"],
    ["storage pointer not moved", "a->handle = b->handle; a->storage = b->storage;", "a->handle = b->handle; a->storage = a->storage;"],
    ["memory pointer not moved", "a->memory = b->memory; a->shadow = b->shadow;", "a->memory = a->memory; a->shadow = b->shadow;"],
    ["upload debt not moved", "a->upload_pending = b->upload_pending;", "a->upload_pending = a->upload_pending;"],
    ["external-memory status not moved", "a->external_memory = b->external_memory;", "a->external_memory = a->external_memory;"],
    ["failed overlay update ignored", "if (!emit_overlay_state(overlay)) return FALSE;", "if (!emit_overlay_state(overlay) && FALSE) return FALSE;"],
    ["failed Present ignored", "if (visible && !emit_present_and_flush(surface->owner))", "if (visible && !emit_present_and_flush(surface->owner) && FALSE)"],
];
try {for (const [label, before, after] of cases) {assert.equal(source.split(before).length, 2, label); const file = dir + "/proxy.c"; fs.writeFileSync(file, source.replace(before, after)); const r = spawnSync(process.execPath, [path.join(__dirname, "ddraw_overlay_flip_test.js")], {env: {...process.env, DDRAW_OVERLAY_FLIP_SOURCE: file}, encoding: "utf8"}); assert.notEqual(r.status, 0, "survived " + label); assert.match(r.stderr, /DDraw overlay flip line/, "strict compile then runtime assertion required: " + label + "\n" + r.stderr); console.log("KILL " + label);} console.log("PASS DDraw overlay Flip mutations: 13/13, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
