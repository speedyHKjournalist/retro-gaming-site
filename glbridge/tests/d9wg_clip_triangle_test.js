"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const root = path.resolve(__dirname,".."), directory = fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-clip-triangles-"));
try {
    const input = path.join(directory,"triangles.c"), binary = path.join(directory,"triangles");
    fs.writeFileSync(input, `#include <stdio.h>
#include "tests/native/d9wg_clip_triangle_cases.h"
int main(void){int error=clip_cases(1000);if(!error)error=clip_triangle_cases(100000);if(error){fprintf(stderr,"clip triangle mismatch header line %d\\n",error);return 1;}
puts("PASS C primitive clipping: analytic intersections, boundary corners and 100000 homogeneous/barycentric cases");return 0;}
`);
    const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11","-Os","-ffp-contract=off","-Wall","-Wextra","-Werror","-I",root,"-I",path.join(root,"runtime"),
        input,process.env.D9WG_CLIP_SOURCE || path.join(root,"runtime/d9wg_clip.c"),"-o",binary],{encoding:"utf8"});
    assert.equal(built.status,0,built.stderr.slice(0,3000)||String(built.error));
    const run = spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr.slice(0,1500)||String(run.error));process.stdout.write(run.stdout);
} finally {fs.rmSync(directory,{recursive:true,force:true});}
