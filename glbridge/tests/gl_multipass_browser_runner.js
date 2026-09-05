#!/usr/bin/env node
// Real GPU test, isolated from the game page and the user's browser profile.
// GL_CHROME can select a Chromium executable on non-macOS hosts.
"use strict";
const http = require("http"), fs = require("fs"), path = require("path");
const { spawn } = require("child_process");
const root = path.resolve(__dirname, "..");
const profile = fs.mkdtempSync(path.join(require("os").tmpdir(), "gl-multipass-"));
let browser, timeout;
const server = http.createServer((req, res) => {
    if (req.method === "POST" && req.url === "/__test_result") {
        let body = "";
        req.on("data", data => { body += data; });
        req.on("end", () => {
            res.end("OK");
            console.log(body);
            finish(JSON.parse(body).status === "PASS" ? 0 : 1);
        });
        return;
    }
    const pathname = new URL(req.url, "http://localhost").pathname;
    const file = path.resolve(root, "." + pathname);
    if (!file.startsWith(root + path.sep)) { res.writeHead(403).end(); return; }
    fs.readFile(file, (error, data) => {
        if (error) { res.writeHead(404).end(); return; }
        res.setHeader("Content-Type", file.endsWith(".js") ?
            "text/javascript" : "text/html");
        res.end(data);
    });
});
function finish(code) {
    clearTimeout(timeout);
    if (browser) browser.kill();
    server.close();
    process.exitCode = code;
}
server.listen(0, "127.0.0.1", () => {
    const url = `http://127.0.0.1:${server.address().port}/tests/gl_multipass_browser_test.html?report=1`;
    browser = spawn(process.env.GL_CHROME ||
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", [
        "--headless=new", "--enable-unsafe-webgpu", "--no-first-run",
        "--no-default-browser-check", "--disable-background-networking",
        `--user-data-dir=${profile}`, url,
    ], { stdio: ["ignore", "ignore", "pipe"] });
    let stderr = "";
    browser.stderr.on("data", data => { stderr = (stderr + data).slice(-4000); });
    browser.on("error", error => { console.error(error); finish(1); });
    timeout = setTimeout(() => {
        console.error("FAIL: real GPU test timed out\n" + stderr);
        finish(1);
    }, 60000);
});
