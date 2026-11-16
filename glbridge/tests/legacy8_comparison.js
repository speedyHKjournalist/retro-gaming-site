"use strict";
// Explicit historical comparison reader. Only the retired Device8 backend is
// frozen; shared headers and the production D3D9/DDraw core remain live.
// Root8 and the new Device8/resource facade tests must NOT use this reader.
const fs = require("node:fs"), path = require("node:path");
const retired = path.resolve(__dirname, "../d3d8proxy/d3d8_proxy.c");
const fixture = path.resolve(__dirname, "fixtures/d3d8_legacy_proxy.c");
let checked = false;
exports.readComparisonSource = (file, ...args) => {
    if (typeof file === "string" && path.resolve(file) === retired) {
        if (!checked) {
            const hash = require("node:crypto").createHash("sha256").update(fs.readFileSync(fixture)).digest("hex");
            require("node:assert/strict").equal(hash, "547b478204b9c175456b9fae6d79780e4f8323ba89fe5fca77228f633689cda2",
                "The retired Device8 reference must remain the exact 26eb8f7 source");
            checked = true;
        }
        return fs.readFileSync(fixture, ...args);
    }
    return fs.readFileSync(file, ...args);
};
