"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-user-mutants-"));
const mutations = [
    ["object space substituted for world space", "vertex", "d9wg_transform_vector4(vertex_state->clip_world, position, world_position);", "for(unsigned c=0;c<4;++c)world_position[c]=position[c];", "native"],
    ["negative user half-space accepted", "clip", "vertex->user_distance[p] < 0", "vertex->user_distance[p] > 0", "native"],
    ["outside user vertices left in screen format", "clip", "if (vertex->code & 0xfc0u)", "if (0)", "native"],
    ["indexed blending loses world transforms", "blend", "world[i] += transformed[i]*weights[bone];", "world[i] += position[i]*weights[bone];", "user"],
    ["user planes omitted from primitive clipping", "clip", "if (plane >= 6 && !(user_mask & (1u << (plane - 6u)))) continue;", "if (plane >= 6) continue;", "user", 2],
    ["intersections lose interpolated user distance", "clip", "((double)b->data[i] - a->data[i])*t)", "((double)b->data[i] - a->data[i])*(i>=D9WG_CLIP_USER_FIRST?0:t))", "user"],
    ["screen-space draws keep host user planes", "vertex_windows", "guard->set_mask(guard->context, 0)", "guard->set_mask(guard->context, guard->mask)", "mask"],
    ["original user-plane state not restored", "vertex_windows", "set_mask(context, guard.mask)", "set_mask(context, 0)", "mask"],
    ["restore failure swallowed", "vertex_windows", "if (guard.suspended && !set_mask(context, guard.mask)) result = -2;", "if (guard.suspended) set_mask(context, guard.mask);", "mask"],
];
try {
    for (const [label, source, before, after, detector, occurrences = 1] of mutations) {
        const header = source === "vertex_windows", original = fs.readFileSync(path.join(__dirname, "../runtime/d9wg_" + source + (header ? ".h" : ".c")), "utf8");
        assert.equal(original.split(before).length, occurrences + 1, "unique mutation: " + label);
        const target = path.join(directory, source + (header ? ".h" : ".c"));fs.writeFileSync(target, original.replaceAll(before, after));
        const env = header ? "D9WG_VERTEX_WINDOWS_HEADER" : "D9WG_" + source.toUpperCase() + "_SOURCE";
        const test = detector === "mask" ? "d9wg_clip_mask_test.js" : "d9wg_clip_user" + (detector === "native" ? "_native" : "") + "_test.js";
        const result = spawnSync(process.execPath, [path.join(__dirname, test)], { encoding: "utf8", env: { ...process.env, [env]: target } });
        assert.notEqual(result.status, 0, "survived: " + label);
        assert.match(result.stderr, /native user-plane mismatch|clip user mismatch|clip mask mismatch/, "must fail executed semantics: " + label);
    }
    console.log("PASS user-plane mutations: 9/9 wrong spaces, storage, geometry, interpolation and state/error handling detected");
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
