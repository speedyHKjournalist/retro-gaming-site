"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d7wg-default-mutants-"));
const files = {device: ["ddrawproxy/d3d7_proxy.inc", "D7WG_STATE_BLOCK_SOURCE"], legacy: ["ddrawproxy/d3d_legacy_proxy.inc", "D7WG_LEGACY_STATE_SOURCE"], defaults: ["runtime/d9wg_legacy_device_defaults.h", "D7WG_DEFAULTS_HEADER"]};
const cases = [
    ["v1 key default", "defaults", "defaults.color_key = defaults.version == 1u;", "defaults.color_key = 0;"],
    ["v2 specular default", "defaults", "defaults.specular = defaults.version == 2u;", "defaults.specular = 0;"],
    ["legacy normalize default", "defaults", "defaults.normalize_normals = defaults.version < 7u;", "defaults.normalize_normals = 0;"],
    ["legacy blend shadow", "defaults", "defaults.texture_map_blend = defaults.version < 7u ? 2u : 0u;", "defaults.texture_map_blend = 2u;"],
    ["legacy mode default", "defaults", "defaults.legacy_texture_blending = defaults.version < 7u;", "defaults.legacy_texture_blending = 0;"],
    ["RGB alpha uses texture", "defaults", "return !bound || alpha_mask ? 2u : 3u;", "return !bound || alpha_mask ? 2u : 2u;"],
    ["ARGB alpha uses current", "defaults", "return !bound || alpha_mask ? 2u : 3u;", "return !bound || alpha_mask ? 3u : 3u;"],
    ["unbound alpha uses current", "defaults", "return !bound || alpha_mask ? 2u : 3u;", "return bound && alpha_mask ? 2u : 3u;"],
    ["initial obsolete blend replay", "device", "if (d3d7_render_state_valid(index) &&", "if ((d3d7_render_state_valid(index) || index == D3DRENDERSTATE_TEXTUREMAPBLEND) &&"],
    ["init drops creation version", "device", "d9wg_legacy_device_defaults(device->creation_version);", "d9wg_legacy_device_defaults(7);"],
    ["binding skips alpha fixup", "device", "BOOL alpha_ready = stage != 0u || d3d7_fixup_legacy_texture_alpha(device);", "BOOL alpha_ready = stage != 0u || TRUE;"],
    ["binding ignores explicit mode", "device", "if (!device->legacy_texture_blending ||", "if (FALSE ||"],
    ["same binding cannot repair alpha", "device", "return stage != 0u || d3d7_fixup_legacy_texture_alpha(device)", "return TRUE || stage != 0u || d3d7_fixup_legacy_texture_alpha(device)"],
    ["failed alpha emission commits shadow", "device", "if (!d3d7_emit_texture_stage_state(device, 0, D3DTSS_ALPHAOP, op))", "device->texture_stage_states[0][D3DTSS_ALPHAOP] = op; if (!d3d7_emit_texture_stage_state(device, 0, D3DTSS_ALPHAOP, op))"],
    ["same map reactivates mode", "device", "device->render_states[state] == value)", "device->render_states[state] == value && FALSE)"],
    ["changed map leaves mode disabled", "device", "device->legacy_texture_blending = TRUE;", "device->legacy_texture_blending = FALSE;"],
    ["same stage disables mode", "legacy", "if (previous == value) return D3D_OK;", "if (previous == value) {device->legacy_texture_blending = FALSE; return D3D_OK;}"],
    ["changed stage leaves mode active", "legacy", "device->legacy_texture_blending = FALSE;", "device->legacy_texture_blending = TRUE;"],
    ["legacy getter exposes modern defaults", "legacy", "*value = 0xffffffffu;", "*value = device->render_states[state];"],
    ["legacy ignored setter writes live", "legacy", "state == D3DRENDERSTATE_LOCALVIEWER)\n        return D3D_OK;", "state == D3DRENDERSTATE_LOCALVIEWER) {device->render_states[state] = value; return D3D_OK;}"],
];
try {
    for (const [label, kind, before, after] of cases) {
        const [file, key] = files[kind], source = fs.readFileSync(path.join(root, file), "utf8");
        assert.equal(source.split(before).length, 2, label);
        const target = path.join(dir, kind + (kind === "defaults" ? ".h" : ".inc")); fs.writeFileSync(target, source.replace(before, after));
        const r = spawnSync(process.execPath, [path.join(__dirname, "d7wg_state_block_test.js")], {env: {...process.env, [key]: target}, encoding: "utf8"});
        assert.notEqual(r.status, 0, "survived " + label);
        assert.match(r.stderr, /D3D7 state block line/, "strict compilation and a runtime assertion required: " + label + "\n" + r.stderr);
        console.log("KILL " + label);
    }
    console.log("PASS legacy D3D defaults mutations: 20/20, strict compilation followed by runtime rejection");
} finally {fs.rmSync(dir, {recursive: true, force: true});}
