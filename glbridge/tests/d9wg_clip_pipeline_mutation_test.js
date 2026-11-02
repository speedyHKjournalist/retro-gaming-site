"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-clip-pipeline-mutants-"));
const mutations = [
    ["near-plane polygon uses OpenGL range", "clip", "case 4: return p[2];", "case 4: return (double)p[2]+p[3];", "triangle"],
    ["intersection attributes use squared parameter", "clip", "((double)b->data[i] - a->data[i])*t)", "((double)b->data[i] - a->data[i])*(t*t))", "triangle"],
    ["position writes keep stale processing records", "clip", "if (actual != expected) break;", "if (actual != expected && 0) break;", "buffer"],
    ["empty record batches never released", "clip", "if (!old->live)", "if (old->live == 0xffffffffu)", "buffer"],
    ["mapped depth lost before division", "clip_draw", "out[4]=processed_vp->min_z*out[3]+out[2]*(processed_vp->max_z-processed_vp->min_z);", "out[4]=out[2];", "draw"],
    ["index at range end accepted", "clip_draw_header", "index >= s->vertex_count", "index > s->vertex_count", "draw"],
    ["indices below MinVertexIndex accepted", "clip_draw_header", "index < s->min_vertex || ", "", "draw"],
    ["flat colour changes at clipped edges", "clip_draw", "if(s->flat_shading)", "if(s->flat_shading && 0)", "draw"],
    ["emission failure silently accepted", "clip_draw", "    if(queued && !emit(context,emitted_type,staging,queued,s->stride))return -2;\n    return 1;", "    if(queued)emit(context,emitted_type,staging,queued,s->stride);\n    return 1;", "draw"],
];
try {
    for (const [label, source, before, after, detector] of mutations) {
        const isHeader = source === "clip_draw_header";
        const original = fs.readFileSync(path.join(__dirname, "../runtime/d9wg_" + (isHeader ? "clip_draw.h" : source + ".c")), "utf8");
        assert.equal(original.split(before).length, 2, "unique C mutation: " + label);
        const caseDirectory = fs.mkdtempSync(path.join(directory, "case-"));
        const target = path.join(caseDirectory, isHeader ? "clip_draw.c" : source + ".c");
        if (isHeader) {
            fs.writeFileSync(path.join(caseDirectory, "d9wg_clip_draw.h"), original.replace(before, after));
            fs.copyFileSync(path.join(__dirname, "../runtime/d9wg_clip_draw.c"), target);
        } else fs.writeFileSync(target, original.replace(before, after));
        const result = spawnSync(process.execPath, [path.join(__dirname, "d9wg_clip_" + detector + "_test.js")],
            { encoding: "utf8", env: { ...process.env, [source === "clip" ? "D9WG_CLIP_SOURCE" : "D9WG_CLIP_DRAW_SOURCE"]: target } });
        assert.notEqual(result.status, 0, "survived: " + label);
        assert.match(result.stderr, /clip (triangle|buffer|draw) mismatch/, "must fail an executed assertion: " + label);
    }
    console.log("PASS C clip pipeline mutations: 9/9 semantic errors detected; production sources untouched");
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
