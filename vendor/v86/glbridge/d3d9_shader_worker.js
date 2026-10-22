globalThis.V86GL_BUILD_REVISION = "6a5936bfd189a5d6221e";
// M6 shader-bytecode translation worker. GPUShaderModule creation remains on
// the executor thread (WebGPU objects are device-owned), while parsing and
// WGSL generation happen here so a large effect set does not block input or
// presentation on its first load.

"use strict";

// The executor passes its cache-buster through to this worker. Mirroring it on
// the translator import keeps worker-generated WGSL in lockstep with the
// page's D3D9ShaderPipeline instead of silently reviving an older script.
importScripts("d3d9_shader_pipeline.js" + self.location.search);

self.onmessage = event => {
    const message = event.data || {};
    let result;
    try {
        result = self.D3D9ShaderPipeline.compileShader(
            new Uint32Array(message.tokens || []));
    } catch (error) {
        result = { ok: false,
            error: error && error.message ? error.message : String(error) };
    }
    self.postMessage({ id: message.id, result });
};
