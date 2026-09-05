#!/usr/bin/env node
// Unit tests for glbridge/gl-webgpu/gl_shader_translator.js -- the GLSL
// 1.10/1.20 front end.
//
// These assert on translation *structure*: which types an expression gets,
// where an implicit conversion was inserted, which uniforms and varyings a
// program exposes, and where glUniform* has to write. Whether the WGSL is
// something a compiler accepts is gl_shader_wgsl_validation_test.js's job --
// the split matters because a shader can be structurally right and still not
// compile, and the two failures need different fixes.

"use strict";

const assert = require("assert");
const t = require("../gl-webgpu/gl_shader_translator.js");

let passed = 0;
const failures = [];
function test(name, fn) {
    try { fn(); ++passed; } catch (error) { failures.push([name, error]); }
}

function compile(source, stage, options) {
    return t.compileShader(source, stage || "fragment", options || {});
}

function link(vsSource, fsSource, options) {
    const vs = compile(vsSource, "vertex");
    const fs = compile(fsSource, "fragment");
    assert.ok(vs.ok, "vertex: " + vs.log);
    assert.ok(fs.ok, "fragment: " + fs.log);
    return t.linkProgram(vs, fs, options || {});
}

const TRIVIAL_VS = "void main(void) { gl_Position = ftransform(); }";

test("depth prepass and shaded programs declare invariant clip positions", () => {
    const position = "attribute vec4 vvertex; uniform mat4 camprojmatrix;\n" +
        "void main() { gl_Position = camprojmatrix * vvertex; }";
    for (const fragment of ["void main() {}",
            "void main() { gl_FragColor = vec4(1.0); }"]) {
        const result = link(position, fragment);
        assert.ok(result.ok, result.log);
        assert.match(result.wgslVertex,
            /@invariant\s+@builtin\(position\) position/);
        assert.ok(!result.wgslFragment.includes("@invariant"),
            "invariance applies to vertex outputs, not fragment inputs");
    }
});

/* ---- preprocessor ---- */

test("#define with parameters expands", () => {
    const result = compile(
        "#define SCALE(v, k) ((v) * (k))\n" +
        "void main(void) { gl_FragColor = SCALE(vec4(1.0), 0.5); }");
    assert.ok(result.ok, result.log);
});

test("#if arithmetic and defined() are evaluated", () => {
    const source =
        "#define A 2\n" +
        "#if (A * 3) > 5 && defined(A)\n" +
        "void main(void) { gl_FragColor = vec4(1.0); }\n" +
        "#else\n" +
        "syntax error here\n" +
        "#endif\n";
    assert.ok(compile(source).ok);
});

test("nested conditionals track their parent's state", () => {
    const source =
        "#if 0\n" +
        "#if 1\n" +
        "this is not code\n" +
        "#endif\n" +
        "#endif\n" +
        "void main(void) { gl_FragColor = vec4(0.0); }\n";
    assert.ok(compile(source).ok);
});

test("a self-referential macro terminates", () => {
    assert.ok(compile("#define A A + 1\n" +
        "void main(void) { float x = 0.0; gl_FragColor = vec4(x); }").ok);
});

test("#version above 120 warns rather than failing", () => {
    const result = compile("#version 130\n" +
        "void main(void) { gl_FragColor = vec4(1.0); }");
    assert.ok(result.ok, result.log);
    assert.ok(result.warnings.some(w => w.indexOf("130") >= 0));
});

test("a required unknown extension is an error", () => {
    const result = compile("#extension GL_NV_nonexistent : require\n" +
        "void main(void) { gl_FragColor = vec4(1.0); }");
    assert.strictEqual(result.ok, false);
    assert.ok(result.log.indexOf("not supported") >= 0, result.log);
});

test("line numbers survive block comments", () => {
    const result = compile("/* one\ntwo\nthree */\nvoid main(void) { bad; }");
    assert.strictEqual(result.ok, false);
    assert.ok(result.log.indexOf("0:4:") >= 0, result.log);
});

/* ---- types and conversions ---- */

test("int converts to float implicitly, and the conversion is recorded", () => {
    const result = compile(
        "void main(void) { float x = 1; gl_FragColor = vec4(x); }");
    assert.ok(result.ok, result.log);
    const main = result.ast.decls.find(d => d.name === "main");
    const declaration = main.body.stmts[0].declarators[0];
    assert.ok(declaration.init.convertTo, "the int literal is marked for widening");
    assert.strictEqual(declaration.init.convertTo.name, "float");
});

test("float does not convert to int", () => {
    const result = compile("void main(void) { int x = 1.0; gl_FragColor = vec4(0.0); }");
    assert.strictEqual(result.ok, false);
    assert.ok(result.log.indexOf("cannot convert") >= 0, result.log);
});

test("matrix and vector multiplication agree on dimensions", () => {
    assert.ok(compile("uniform mat4 m;\nvoid main(void) {\n" +
        "  vec4 v = m * vec4(1.0);\n  gl_FragColor = v;\n}").ok);
    const bad = compile("uniform mat4 m;\nvoid main(void) {\n" +
        "  vec3 v = m * vec3(1.0);\n  gl_FragColor = vec4(v, 1.0);\n}");
    assert.strictEqual(bad.ok, false);
});

test("a swizzle out of range is rejected", () => {
    const result = compile(
        "void main(void) { vec2 v = vec2(1.0); gl_FragColor = vec4(v.xyzw); }");
    assert.strictEqual(result.ok, false);
    assert.ok(result.log.indexOf("out of range") >= 0, result.log);
});

test("a repeated component in an l-value swizzle is rejected", () => {
    const result = compile("void main(void) { vec4 v; v.xx = vec2(1.0); " +
        "gl_FragColor = v; }");
    assert.strictEqual(result.ok, false);
    assert.ok(result.log.indexOf("may not repeat") >= 0, result.log);
});

test("overload resolution prefers an exact match", () => {
    const result = compile(
        "float f(float x) { return x; }\n" +
        "float f(vec2 x) { return x.x; }\n" +
        "void main(void) { gl_FragColor = vec4(f(vec2(1.0))); }");
    assert.ok(result.ok, result.log);
});

test("an ambiguous call is an error, not a first-match win", () => {
    const result = compile(
        "float f(float a, vec2 b) { return a; }\n" +
        "float f(vec2 a, float b) { return b; }\n" +
        "void main(void) { gl_FragColor = vec4(f(1, 1)); }");
    assert.strictEqual(result.ok, false);
});

test("a reserved word is refused with its name", () => {
    const result = compile("void main(void) { int switch = 1; }");
    assert.strictEqual(result.ok, false);
    assert.ok(result.log.indexOf("switch") >= 0, result.log);
});

test("discard outside a fragment shader is refused", () => {
    const result = compile("void main(void) { discard; gl_Position = vec4(0.0); }",
        "vertex");
    assert.strictEqual(result.ok, false);
});

/* ---- usage tracking ---- */

test("compatibility attributes are recorded with their historical slots", () => {
    const result = compile(
        "void main(void) { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n" +
        "  gl_TexCoord[0] = gl_MultiTexCoord0; }", "vertex");
    assert.ok(result.ok, result.log);
    const attributes = result.usage.attributes;
    assert.strictEqual(attributes.get("gl_Vertex").location, 0);
    assert.strictEqual(attributes.get("gl_MultiTexCoord0").location, 8);
    assert.ok(result.usage.stateFields.has("mvp"));
    assert.strictEqual(result.usage.texCoordMax, 0);
});

test("ftransform() pulls in gl_Vertex even when the shader never names it", () => {
    const result = compile(TRIVIAL_VS, "vertex");
    assert.ok(result.ok, result.log);
    assert.ok(result.usage.attributes.has("gl_Vertex"));
    assert.ok(result.usage.stateFields.has("mvp"));
});

test("only the state a shader reads is requested", () => {
    const result = compile(
        "void main(void) { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }",
        "vertex");
    assert.deepStrictEqual([...result.usage.stateFields], ["mvp"]);
});

test("a sampler is a binding, not a uniform-block member", () => {
    const result = compile("uniform sampler2D tex;\nuniform float k;\n" +
        "void main(void) { gl_FragColor = texture2D(tex, vec2(0.0)) * k; }");
    assert.ok(result.ok, result.log);
    assert.strictEqual(result.usage.samplers.length, 1);
    assert.strictEqual(result.usage.samplers[0].name, "tex");
    assert.strictEqual(result.usage.uniforms.length, 1);
    assert.strictEqual(result.usage.uniforms[0].name, "k");
});

test("a texture fetch in uniform control flow is counted separately", () => {
    const result = compile("uniform sampler2D tex;\nvarying vec2 tc;\n" +
        "void main(void) {\n" +
        "  vec4 c = texture2D(tex, tc);\n" +
        "  if (tc.x > 0.5) { c += texture2D(tex, tc * 2.0); }\n" +
        "  gl_FragColor = c;\n}");
    assert.ok(result.ok, result.log);
    assert.strictEqual(result.usage.uniformSamples, 1);
    assert.strictEqual(result.usage.nonUniformSamples, 1,
        "the conditional fetch needs explicit gradients (plan 4.10)");
});

/* ---- linking ---- */

test("a program's uniform block places each member on a 16-byte boundary", () => {
    const program = link(TRIVIAL_VS,
        "uniform float a;\nuniform vec3 b;\nuniform mat4 m;\n" +
        "void main(void) { gl_FragColor = vec4(a) + vec4(b, 1.0) + m[0]; }");
    assert.ok(program.ok, program.log);
    const byName = new Map(program.reflection.uniforms.map(u => [u.name, u]));
    assert.strictEqual(byName.get("a").offsetBytes % 16, 0);
    assert.strictEqual(byName.get("b").offsetBytes % 16, 0);
    assert.strictEqual(byName.get("m").offsetBytes % 16, 0);
    assert.strictEqual(byName.get("m").matrixColumnStrideBytes, 16);
});

test("a GLSL program variant injects colour logic operations", () => {
    const program = link(TRIVIAL_VS,
        "void main(void) { gl_FragColor = vec4(0.25); }",
        { variant: { logicOp: 0x150A, colorTargets: 1 } });
    assert.ok(program.ok, program.log);
    assert.ok(program.wgslFragment.includes("let r = (~d)"));
    assert.ok(program.wgslFragment.includes("glApplyLogic(gl_FragColor"));
});

test("a uniform array of floats is widened to a vec4 stride", () => {
    const program = link(TRIVIAL_VS,
        "uniform float weights[4];\n" +
        "void main(void) { gl_FragColor = vec4(weights[2]); }");
    assert.ok(program.ok, program.log);
    const weights = program.reflection.uniforms.find(u => u.name === "weights");
    assert.strictEqual(weights.arraySize, 4);
    assert.strictEqual(weights.arrayStrideBytes, 16,
        "the uniform address space requires a 16-byte array stride");
});

test("uniform locations follow GL's array rule", () => {
    const program = link(TRIVIAL_VS,
        "uniform vec4 colors[3];\nuniform float k;\n" +
        "void main(void) { gl_FragColor = colors[1] * k; }");
    const colors = program.reflection.uniforms.find(u => u.name === "colors");
    const k = program.reflection.uniforms.find(u => u.name === "k");
    assert.strictEqual(k.location, colors.location + 3,
        "an array occupies one location per element");
});

test("a uniform declared differently in the two stages fails to link", () => {
    const vs = compile("uniform vec4 shared_value;\n" +
        "void main(void) { gl_Position = shared_value; }", "vertex");
    const fs = compile("uniform vec2 shared_value;\n" +
        "void main(void) { gl_FragColor = vec4(shared_value, 0.0, 1.0); }");
    const program = t.linkProgram(vs, fs, {});
    assert.strictEqual(program.ok, false);
    assert.ok(program.log.indexOf("different types") >= 0, program.log);
});

test("only varyings both stages use get a slot", () => {
    const program = link(
        "varying vec4 used;\nvarying vec4 unused;\n" +
        "void main(void) { gl_Position = gl_Vertex; used = vec4(1.0); " +
        "unused = vec4(2.0); }",
        "varying vec4 used;\nvoid main(void) { gl_FragColor = used; }");
    assert.ok(program.ok, program.log);
    assert.strictEqual(program.reflection.varyingSlots, 1,
        "a varying the fragment stage never reads costs nothing");
});

test("small varyings are packed into a shared slot", () => {
    const program = link(
        "varying float a;\nvarying vec2 b;\n" +
        "void main(void) { gl_Position = gl_Vertex; a = 1.0; b = vec2(2.0); }",
        "varying float a;\nvarying vec2 b;\n" +
        "void main(void) { gl_FragColor = vec4(a, b, 1.0); }");
    assert.ok(program.ok, program.log);
    assert.strictEqual(program.reflection.varyingSlots, 1,
        "a float and a vec2 fit in one vec4 slot");
});

test("too many varyings fails to link with a message naming the limit", () => {
    let vs = "";
    let fs = "";
    for (let i = 0; i < 20; ++i) {
        vs += "varying vec4 v" + i + ";\n";
        fs += "varying vec4 v" + i + ";\n";
    }
    vs += "void main(void) { gl_Position = gl_Vertex;";
    fs += "void main(void) { vec4 sum = vec4(0.0);";
    for (let i = 0; i < 20; ++i) {
        vs += " v" + i + " = vec4(float(" + i + "));";
        fs += " sum += v" + i + ";";
    }
    vs += " }";
    fs += " gl_FragColor = sum; }";
    const program = link(vs, fs);
    assert.strictEqual(program.ok, false);
    assert.ok(program.log.indexOf("D-12") >= 0, program.log);
});

test("gl_Color in the fragment stage is fed by gl_FrontColor", () => {
    const program = link(
        "void main(void) { gl_Position = gl_Vertex; gl_FrontColor = gl_Color; }",
        "void main(void) { gl_FragColor = gl_Color; }");
    assert.ok(program.ok, program.log);
    assert.ok(program.wgslFragment.indexOf("gl_Color = fsin.v0") >= 0,
        program.wgslFragment);
});

test("a variant changes the cache key and the generated code", () => {
    const vs = compile(TRIVIAL_VS, "vertex");
    const fs = compile("void main(void) { gl_FragColor = vec4(1.0, 0.0, 0.0, 0.5); }");
    const plain = t.linkProgram(vs, fs, {});
    const tested = t.linkProgram(vs, fs, { variant: { alphaTest: "greater" } });
    assert.notStrictEqual(plain.cacheKey, tested.cacheKey);
    assert.ok(tested.wgslFragment.indexOf("discard") >= 0);
    assert.ok(plain.wgslFragment.indexOf("discard") < 0);
    assert.ok(tested.reflection.stateFields.indexOf("alphaRef") >= 0);
});

test("a programmable polygon-stipple variant injects the fragment test", () => {
    const vs = compile(TRIVIAL_VS, "vertex");
    const fs = compile("void main(void) { gl_FragColor = vec4(1.0); }");
    const plain = t.linkProgram(vs, fs, {});
    const stippled = t.linkProgram(vs, fs,
        { variant: { polygonStipple: true } });
    assert.ok(stippled.ok, stippled.log);
    assert.notStrictEqual(plain.cacheKey, stippled.cacheKey);
    assert.ok(stippled.reflection.stateFields.includes("polygonStipple"));
    assert.ok(stippled.wgslFragment.indexOf("stippleByteIndex") >= 0);
});

test("gl_FragData decides the colour target count", () => {
    const program = link(TRIVIAL_VS,
        "void main(void) { gl_FragData[0] = vec4(1.0); gl_FragData[2] = vec4(0.0); }");
    assert.ok(program.ok, program.log);
    assert.strictEqual(program.reflection.colorTargets, 3);
});

test("writing both gl_FragColor and gl_FragData is refused", () => {
    const program = link(TRIVIAL_VS,
        "void main(void) { gl_FragColor = vec4(1.0); gl_FragData[0] = vec4(0.0); }");
    assert.strictEqual(program.ok, false);
});

test("the generated vertex shader always remaps depth and flips Y", () => {
    const program = link(TRIVIAL_VS, "void main(void) { gl_FragColor = vec4(1.0); }");
    assert.ok(program.wgslVertex.indexOf(
        "clip.z = (clip.z + clip.w) * 0.5;") >= 0);
    assert.ok(program.wgslVertex.indexOf("clip.y = -clip.y;") >= 0);
});

test("out parameters become pointers with a write-back", () => {
    const program = link(TRIVIAL_VS,
        "void split(vec4 v, out vec3 rgb, out float a) { rgb = v.rgb; a = v.a; }\n" +
        "void main(void) {\n" +
        "  vec3 c; float alpha;\n" +
        "  split(vec4(1.0), c, alpha);\n" +
        "  gl_FragColor = vec4(c, alpha);\n}");
    assert.ok(program.ok, program.log);
    assert.ok(program.wgslFragment.indexOf("ptr<function") >= 0);
    assert.ok(program.wgslFragment.indexOf("&_t") >= 0,
        "the argument is copied into a temporary and written back");
});

test("mod() is emitted with GLSL's floor semantics", () => {
    const program = link(TRIVIAL_VS,
        "void main(void) { gl_FragColor = vec4(mod(-1.5, 1.0)); }");
    assert.ok(program.wgslFragment.indexOf("floor(") >= 0,
        "'%' would give C's truncating remainder, which differs for negatives");
});

for (const [name, error] of failures)
    console.error("FAIL: " + name + "\n    " + (error && error.message));
console.log(passed + " passed, " + failures.length + " failed");
process.exit(failures.length ? 1 : 0);
