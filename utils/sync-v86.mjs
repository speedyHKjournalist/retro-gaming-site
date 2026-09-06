#!/usr/bin/env node
// Run after `make -C ../v86 build/libv86.js glbridge`.
import { copyFile, mkdir, readFile, writeFile } from "node:fs/promises";
import { resolve, join } from "node:path";
import { fileURLToPath } from "node:url";
import { createHash } from "node:crypto";
const site = fileURLToPath(new URL("../", import.meta.url));
const v86 = resolve(process.argv[2] || process.env.V86_ROOT || join(site, "../v86"));
const graphics = join(v86, "build/glbridge");
const manifest = JSON.parse(await readFile(join(graphics, "manifest.json"), "utf8"));
const output = join(site, "vendor/v86/glbridge");
// Validate every input before replacing any site assets.
const names = ["libv86.js", "v86.wasm"];
const core = await Promise.all(names.map(name => readFile(join(v86, "build", name))));
const assets = await Promise.all(manifest.files.map(name => readFile(join(graphics, name))));
await mkdir(output, { recursive: true });
for (let i = 0; i < names.length; i++) await writeFile(join(site, names[i]), core[i]);
for (let i = 0; i < manifest.files.length; i++) await writeFile(join(output, manifest.files[i]), assets[i]);
await copyFile(join(graphics, "manifest.json"), join(output, "manifest.json"));
const coreRevision = createHash("sha256").update(core[0]).digest("hex").slice(0, 20);
const appRevision = createHash("sha256").update(await readFile(join(site, "app.js"))).digest("hex").slice(0, 20);
const styleRevision = createHash("sha256").update(await readFile(join(site, "styles.css"))).digest("hex").slice(0, 20);
const page = join(site, "game.html");
const html = (await readFile(page, "utf8"))
    .replace(/src="libv86\.js[^\"]*"/, `src="libv86.js?v=${coreRevision}"`)
    .replace(/src="vendor\/v86\/glbridge\/libv86-webgpu\.js[^\"]*"/,
        `src="vendor/v86/glbridge/libv86-webgpu.js?v=${manifest.revision}"`)
    .replace(/src="app\.js[^\"]*"/, `src="app.js?v=${appRevision}"`)
    .replace(/href="styles\.css[^\"]*"/, `href="styles.css?v=${styleRevision}"`);
await writeFile(page, html);
console.log(`Synced v86 ${coreRevision}, WebGPU ${manifest.revision}`);
