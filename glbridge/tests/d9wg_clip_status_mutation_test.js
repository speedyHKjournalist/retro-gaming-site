"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-status-mutants-"));
const mutations = [
    ["union replaces earlier vertices", "clip", "status->clip_union |= clip_union;", "status->clip_union = clip_union;"],
    ["intersection uses union", "clip", "status->clip_intersection &= clip_intersection;", "status->clip_intersection |= clip_intersection;"],
    ["default intersection starts empty", "clip", "status->clip_intersection = 0xffffffffu;", "status->clip_intersection = 0;"],
    ["legacy Get overwrites caller Z extents", "clip", "__builtin_memcpy(output, status, 24);", "__builtin_memcpy(output, status, 32);"],
    ["legacy EXTENTS3 incorrectly accepted", "clip", "if (value.flags & ~3u) return 0;", "if (0) return 0;"],
    ["native one-pixel extent margin lost", "clip", "screen[0] - 1", "screen[0]", 2],
    ["right viewport extent clamp omitted", "clip", "if (status->max_x > viewport->x + viewport->width)", "if (0 && status->max_x > viewport->x + viewport->width)"],
    ["legacy high status bits discarded", "clip", "(status->status & 0xff000000u)", "0u"],
    ["RHW buffers repeat status processing", "vertex_windows", "if (source->layout->position_transformed && (!source->status_screen || !clipping)) return 0;", "if (source->layout->position_transformed && (!source->status_screen || !clipping)) {status->clip_union=status->clip_intersection=0;return 0;}"],
    ["disabled draw keeps old union", "vertex_windows", "status->clip_union = status->clip_intersection = 0;", "status->clip_intersection = 0;"],
    ["declared range reduced to index count", "vertex", "uint32_t end = s->indices ? s->vertex_count : s->count;", "uint32_t end = s->count;"],
    ["screen depth unmapped a second time", "clip", "clip[2] = screen[2]*clip[3];", "clip[2] = (screen[2]-viewport->min_z)/(viewport->max_z-viewport->min_z)*clip[3];"],
    ["screen pixels tested as clip coordinates", "vertex", "record.code = d9wg_clip_outcode(clip);", "record.code = d9wg_clip_outcode(position);"],
    ["status user planes use object coordinates", "vertex", "if (state->clip_plane_mask) d9wg_transform_vector4(state->clip_world, position, world);", "if (state->clip_plane_mask) for(unsigned c=0;c<4;++c)world[c]=position[c];"],
];
try {
    for (const [label, source, before, after, occurrences = 1] of mutations) {
        const header = source === "vertex_windows", original = fs.readFileSync(path.join(__dirname, "../runtime/d9wg_" + source + (header ? ".h" : ".c")), "utf8");
        assert.equal(original.split(before).length, occurrences + 1, "unique mutation: " + label);
        const target = path.join(directory, source + (header ? ".h" : ".c"));fs.writeFileSync(target, original.replaceAll(before, after));
        const env = header ? "D9WG_VERTEX_WINDOWS_HEADER" : "D9WG_" + source.toUpperCase() + "_SOURCE";
        const result = spawnSync(process.execPath, [path.join(__dirname, "d9wg_clip_status_test.js")], { encoding: "utf8", env: { ...process.env, [env]: target } });
        assert.notEqual(result.status, 0, "survived: " + label);
        assert.match(result.stderr, /clip status mismatch/, "must fail executed semantics: " + label);
    }
    console.log("PASS ClipStatus mutations: 14/14 histories, defaults, ABI, extents, flags, spaces and transformed-buffer rules detected");
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
