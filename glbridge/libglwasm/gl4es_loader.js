(function(global) {
    "use strict";

    const GL4ES_ASSET_VERSION = "war3blackfix-20260702";
    const factory = global.createV86GL4ES;
    if (typeof factory !== "function") {
        console.warn("[v86gl] gl4es factory not found; run glbridge/libglwasm/build_gl4es_module.sh first");
        return;
    }

    function createRenderer(canvas) {
        return factory({
            canvas: canvas || document.getElementById("v86gl_canvas"),
            locateFile(path) {
                const url = "glbridge/libglwasm/" + path;
                return path === "gl4es.wasm" ? `${url}?v=${GL4ES_ASSET_VERSION}` : url;
            },
        });
    }

    // The PCI bridge creates a new instance after every guest WGL teardown.
    // A new WASM instance is required because gl4es keeps process-wide state.
    global.createV86GL4ESRenderer = createRenderer;
    global.resetV86GL4ESRenderer = function(canvas) {
        const renderer = createRenderer(canvas);
        global.GL4ES = renderer;
        return renderer;
    };

    global.resetV86GL4ESRenderer(document.getElementById("v86gl_canvas"));
})(typeof window !== "undefined" ? window : globalThis);
