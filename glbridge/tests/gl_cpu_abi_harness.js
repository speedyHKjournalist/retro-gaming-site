"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), os = require("node:os"), path = require("node:path");
const { execFileSync, spawn } = require("node:child_process");
const { hostPath, testPath } = require("./host_paths.js");
const { GLShadow } = require(hostPath("gl-webgpu/gl_shadow.js"));
require(hostPath("gl-webgpu/gl_shadow_objects.js")); require(hostPath("gl-webgpu/gl_shadow_images.js")); require(hostPath("gl-webgpu/gl_shadow_arrays.js"));
require(hostPath("gl-webgpu/gl_shadow_names.js"));
require(hostPath("gl-webgpu/gl_shadow_queries.js"));
const { GLWebGPUExecutor } = require(hostPath("gl-webgpu/gl_executor.js"));
const { createFakeHost } = require(testPath("gl_fake_gpu.js"));
const { GLStream, GL } = require(testPath("gl_stream_builder.js"));
const P = require(hostPath("gl-webgpu/gl_protocol.js"));
function body(source, name, prefix) {
    let at = source.indexOf(prefix + name + "(");
    while (at >= 0 && source.indexOf(";", at) < source.indexOf("{", at)) at = source.indexOf(prefix + name + "(", at + 1);
    assert.ok(at >= 0, name);
    let end = source.indexOf("{", at) + 1, depth = 1;
    for (; depth; ++end) { if (source[end] === "{") ++depth; if (source[end] === "}") --depth; }
    return source.slice(at, end);
}
const transport = `
static unsigned char wire[700000]; static uint32_t wire_size, error;
static void v86gl_set_error(GLenum value) { if(!error)error=value; }
static uint32_t read_u32le(const uint8_t* p) { uint32_t v; memcpy(&v,p,4); return v; }
static int reserve_pci_record(uint16_t opcode,const void* payload,uint32_t size,uint8_t** out) {
    (void)payload; assert(size+8<sizeof(wire)); uint32_t header=size>=0xFFFF?8:4;
    uint16_t small=size>=0xFFFF?0xFFFF:(uint16_t)size; memcpy(wire,&opcode,2); memcpy(wire+2,&small,2);
    if(header==8)memcpy(wire+4,&size,4); wire_size=header+size; memset(wire+header,0,size); *out=wire+header; return 1;
}
static int emit_pci_batch(BOOL present) {
#ifdef GL_CPU_ABI_BEFORE_SEND
    GL_CPU_ABI_BEFORE_SEND;
#endif
    (void)present; assert(fwrite(&wire_size,4,1,stdout)==1); assert(fwrite(wire,1,wire_size,stdout)==wire_size); fflush(stdout);
    assert(fread(wire,1,wire_size,stdin)==wire_size); return 1;
}
static void emit_gl_call(uint16_t opcode,const void* payload,uint32_t size) {
    uint8_t* args; reserve_pci_record(opcode,NULL,size,&args); if(size)memcpy(args,payload,size); assert(emit_pci_batch(0));
}
`;
async function run(code, check) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "gl-cpu-abi-"));
    try {
        fs.writeFileSync(path.join(dir, "test.c"), code);
        execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-I", path.join(__dirname, "../openglproxy"), path.join(dir, "test.c"), "-o", path.join(dir, "test")]);
        for (const copied of [false, true]) {
            const shadow = new GLShadow(), { host } = createFakeHost(), gpu = new GLWebGPUExecutor(null, { host, strict: true }); await gpu.initialize();
            gpu.options.writeGuestMemory = (offset, data, metadata) => metadata.guestBytes.set(data, offset);
            const initial = new GLStream().makeCurrent(1, 0, 0, 64, 64, 1, 1).bytes(); shadow.process(initial); gpu.submit(initial, {});
            const child = spawn(path.join(dir, "test")); let pending = Buffer.alloc(0), stderr = "", failure, count = 0;
            child.stderr.on("data", b => { stderr += b; });
            child.stdout.on("data", bytes => {
                pending = Buffer.concat([pending, bytes]);
                try { while (pending.length >= 4 && pending.length >= 4 + pending.readUInt32LE(0)) {
                    const length = pending.readUInt32LE(0), command = Uint8Array.from(pending.subarray(4, 4 + length)); pending = pending.subarray(4 + length); ++count;
                    shadow.process(command); const answered = command.slice(), view = new DataView(command.buffer);
                    const fn = view.getUint16(0, true), at = view.getUint16(2, true) === 0xFFFF ? 8 : 4;
                    const query = fn === P.GLFN.QUERY_OBJECT_BATCH ? "QUERY_OBJECT_BATCH" :
                        fn === P.GLFN.QUERY_OBJECT_IV && view.getUint32(at + P.F.QUERY_OBJECT_IV.object_kind, true) === P.C.V86GL_OBJECT_KIND_QUERY ? "QUERY_OBJECT_IV" : null;
                    const cpuAnswered = !query || view.getUint32(at + P.F[query].status, true) !== P.C.V86GL_SYNC_QUERY_STATUS_PENDING;
                    gpu.submit(copied ? command.slice() : command, { guestBytes: command });
                    const finish = () => { if (cpuAnswered) assert.deepEqual(command, answered, "renderer changed CPU answers"); child.stdin.write(command); };
                    if (query && view.getUint32(at + P.F[query].status, true) === P.C.V86GL_SYNC_QUERY_STATUS_PENDING)
                        gpu.idle().then(finish).catch(error => { failure = error; child.kill(); });
                    else finish();
                } } catch (error) { failure = error; child.kill(); }
            });
            const timer = setTimeout(() => child.kill(), 30000);
            const status = await new Promise((resolve, reject) => { child.once("error", reject); child.once("close", resolve); }); clearTimeout(timer);
            if (failure) throw failure; assert.equal(status, 0, stderr); assert.equal(pending.length, 0); assert.ok(count > 0);
            assert.equal(gpu.stats.refusals, 0); assert.equal(gpu.hostErrors.length, 0); if (check) check(shadow, gpu);
        }
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
}
module.exports = { body, transport, run, GL };
