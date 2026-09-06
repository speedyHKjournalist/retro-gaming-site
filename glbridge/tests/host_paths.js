"use strict";
const path = require("node:path");
const root = path.resolve(process.env.V86_ROOT || path.join(__dirname, "../../../v86"));
exports.hostPath = relative => path.join(root, "src/browser/glbridge", relative);
exports.testPath = relative => path.join(root, "tests/glbridge", relative);
