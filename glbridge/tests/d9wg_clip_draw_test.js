"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const root = path.resolve(__dirname,".."), directory = fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-clip-draw-"));
try {
    const input = path.join(directory,"draw.c"), binary = path.join(directory,"draw");
    fs.writeFileSync(input, `#include <stdio.h>
#include "tests/native/d9wg_clip_draw_cases.h"
int main(void){int error=clip_draw_cases();if(error){fprintf(stderr,"clip draw mismatch header line %d\\n",error);return 1;}
puts("PASS C processed draw: near-plane geometry, attributes, index validation, preserved depth and changed viewport");return 0;}
`);
    const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11","-Os","-ffp-contract=off","-Wall","-Wextra","-Werror","-I",root,"-I",path.join(root,"runtime"),
        input,process.env.D9WG_CLIP_DRAW_SOURCE || path.join(root,"runtime/d9wg_clip_draw.c"),path.join(root,"runtime/d9wg_clip.c"),path.join(root,"runtime/d9wg_math.c"),"-o",binary],{encoding:"utf8"});
    assert.equal(built.status,0,built.stderr.slice(0,3000)||String(built.error));
    const run = spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr.slice(0,1500)||String(run.error));process.stdout.write(run.stdout);
} finally {fs.rmSync(directory,{recursive:true,force:true});}
