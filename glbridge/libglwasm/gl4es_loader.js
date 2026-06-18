(function(global) {
    "use strict";

    const factory = global.createV86GL4ES;
    if (typeof factory !== "function") {
        console.warn("[v86gl] gl4es factory not found; run glbridge/libglwasm/build_gl4es_module.sh first");
        return;
    }

    global.GL4ES = factory({
        canvas: document.getElementById("v86gl_canvas"),
        locateFile(path) {
            return "glbridge/libglwasm/" + path;
        },
    });
})(typeof window !== "undefined" ? window : globalThis);
