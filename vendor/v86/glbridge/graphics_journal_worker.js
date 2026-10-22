globalThis.V86GL_BUILD_REVISION = "6a5936bfd189a5d6221e";
// Dedicated compression thread. Pages arrive by ownership transfer, so neither
// structured clone nor compression scans command bytes on the emulation thread.
(function() {
    "use strict";
    self.onmessage = async event => {
        const { buffer, length, measure } = event.data;
        const start = measure ? performance.now() : 0;
        let data = buffer, codec = 0;
        try {
            const blob = new Blob([new Uint8Array(buffer, 0, length)]);
            const compressed = await new Response(blob.stream().pipeThrough(new self.CompressionStream("gzip"))).arrayBuffer();
            if (compressed.byteLength < length) { data = compressed; codec = 1; }
        } catch (_) {
            // Compression is optional. Return the original owned page on failure.
        }
        if (data === buffer && length < buffer.byteLength) data = buffer.slice(0, length);
        self.postMessage({ buffer: data, codec, length: codec ? data.byteLength : length,
            compression_ms: measure ? performance.now() - start : null }, [data]);
    };
    self.postMessage({ ready: true });
})();
