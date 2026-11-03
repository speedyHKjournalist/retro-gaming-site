"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = ["ddraw_proxy.c", "d3d7_proxy.inc", "d3d_legacy_proxy.inc"].map(n => fs.readFileSync(root + "/ddrawproxy/" + n, "utf8")).join("\n"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "texture-load-mutants-"));
const cases = [
    ["destination subset suffix ignored", "d3d_texture_mip_subset", "return found && !destination;", "return found || !destination;"],
    ["only top mip copied", "d3d_texture_load_mip_subset", "destination = destination->mip_next;", "destination = NULL;"],
    ["right mip coordinate rounded down", "d3d_texture_next_coordinates", "((int64_t)rect->right + 1) / 2", "(int64_t)rect->right / 2"],
    ["bottom mip coordinate rounded down", "d3d_texture_next_coordinates", "((int64_t)rect->bottom + 1) / 2", "(int64_t)rect->bottom / 2"],
    ["destination point not divided", "d3d_texture_next_coordinates", "point->x /= 2; point->y /= 2;", "point->x /= 1; point->y /= 1;"],
    ["palette object replaced", "d3d_texture_copy_palette", "return palette_SetEntries", "destination->palette = source->palette;\n    return palette_SetEntries"],
    ["Device7 requires destination palette", "d3d_texture_load_mip_subset", "destination, source, FALSE", "destination, source, TRUE"],
    ["legacy permits missing destination palette", "d3d_texture_load_legacy", "dst, src, TRUE", "dst, src, FALSE"],
    ["Device7 only copies source-blit key", "d3d_texture_load_mip_subset", "d3d_texture_copy_keys(destination, source, TRUE)", "d3d_texture_copy_keys(destination, source, FALSE)"],
    ["legacy overwrites all keys", "d3d_texture_load_legacy", "d3d_texture_copy_keys(dst, src, FALSE)", "d3d_texture_copy_keys(dst, src, TRUE)"],
    ["absent source keys erase destination", "d3d_texture_copy_keys", "if (!(source->color_key_present & (1u << kind))) continue;", "if (FALSE && !(source->color_key_present & (1u << kind))) continue;"],
    ["ALLOCONLOAD retained", "d3d_texture_load_legacy", "dst->desc.ddsCaps.dwCaps &= ~DDSCAPS_ALLOCONLOAD;", "dst->desc.ddsCaps.dwCaps |= DDSCAPS_ALLOCONLOAD;"],
    ["legacy rejects unequal mip lengths", "d3d_texture_load_legacy", "/* The older public interfaces", "if (dst || src) return D3DERR_TEXTURE_BADSIZE;\n    /* The older public interfaces"],
    ["empty cube selection copies all faces", "d3d_texture_load7", "!(flags & (DDSCAPS2_CUBEMAP_POSITIVEX << face))", "(FALSE && !(flags & (DDSCAPS2_CUBEMAP_POSITIVEX << face)))"],
    ["unselected cube face not preflighted", "d3d_texture_load7", "if (src && (!dst || !d3d_texture_mip_subset(dst, src)))", "if (src && (flags & (DDSCAPS2_CUBEMAP_POSITIVEX << face)) && (!dst || !d3d_texture_mip_subset(dst, src)))"],
    ["cube/type mismatch accepted", "d3d_texture_load7", "source_cube != destination_cube)", "(FALSE && source_cube != destination_cube))"],
    ["cube flags shifted to wrong face", "d3d_texture_load7", "if (!src || !(flags & (DDSCAPS2_CUBEMAP_POSITIVEX << face))) continue;", "if (!src || !(flags & (DDSCAPS2_CUBEMAP_POSITIVEX << ((face + 1) % 6)))) continue;"],
    ["caller coordinates mutated", "d3d_texture_load7", "if (destination_point) point = *destination_point;", "if (destination_point) {point = *destination_point; ((POINT *)destination_point)->x = 0;}"],
    ["external memory never synchronized", "d3d_texture_copy_region", "if (destination->external_memory && !surface_readback(destination))", "if (FALSE && destination->external_memory && !surface_readback(destination))"],
    ["copy transport failure ignored", "d3d_texture_copy_region", "0, 0, 0.0f)) return DDWG_TRACE_ERROR(DDERR_GENERIC);", "0, 0, 0.0f) && FALSE) return DDWG_TRACE_ERROR(DDERR_GENERIC);"],
];
function replace(name, before, after) {const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;} const body = source.slice(m.index, end); assert.equal(body.split(before).length, 2, name + ": " + before); return source.slice(0, m.index) + body.replace(before, after) + source.slice(end);}
try {for (const [label, name, before, after] of cases) {const file = dir + "/proxy.c"; fs.writeFileSync(file, replace(name, before, after)); const r = spawnSync(process.execPath, [path.join(__dirname, "d7wg_texture_load_test.js")], {env: {...process.env, D7WG_TEXTURE_LOAD_SOURCE: file}, encoding: "utf8"}); assert.notEqual(r.status, 0, "survived " + label); assert.match(r.stderr, /texture load line/, "strict compilation then runtime assertion required: " + label + "\n" + r.stderr); console.log("KILL " + label);} console.log("PASS texture Load mutations: 20/20, strict compilation and runtime assertions");} finally {fs.rmSync(dir, {recursive: true, force: true});}
