"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const original = fs.readFileSync(path.join(__dirname,"../runtime/d9wg_vertex_format.h"),"utf8");
const mutations = [
    ["missing w becomes zero","value[3] = 1;","value[3] = 0;"],
    ["D3DCOLOR loses BGRA ordering","order[4] = {2,1,0,3}","order[4] = {0,1,2,3}"],
    ["normalized bytes are raw integers","if (type != 5) value[i] /= 255.0f;","if (type == 5) value[i] /= 255.0f;"],
    ["signed short treated as unsigned","(int32_t)(word ^ 32768u) - 32768","(int32_t)word"],
    ["SNORM16 minimum is not clamped","component == -32768 ? -1.0f :","0 ? -1.0f :"],
    ["SNORM10 minimum is not clamped","component < -511 ? -1.0f :","component < -512 ? -1.0f :"],
    ["packed 10-bit components overlap","word >> (i*10u)","word >> (i*8u)"],
    ["half exponent bias is wrong","exponent + 112u","exponent + 111u"],
    ["half subnormals are zero","if (!fraction) return sign;","if (fraction < 1024u) return sign;"],
];
const directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-format-mutants-"));
try {
    for(const [label,before,after] of mutations) {
        assert.equal(original.split(before).length,2,"unique format mutation: "+label);
        const file=path.join(directory,"format.h");fs.writeFileSync(file,original.replace(before,after));
        const run=spawnSync(process.execPath,[path.join(__dirname,"d9wg_vertex_format_test.js")],{encoding:"utf8",env:{...process.env,D9WG_VERTEX_FORMAT_SOURCE:file}});
        assert.notEqual(run.status,0,"survived: "+label);assert.match(run.stderr,/vertex format mismatch header line/,"must execute an assertion: "+label);
    }
    console.log("PASS packed vertex input mutations: 9/9, production sources untouched");
} finally {fs.rmSync(directory,{recursive:true,force:true});}
