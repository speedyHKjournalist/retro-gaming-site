const fs = require('fs');
const path = require('path');

const inputFile = "starcraft.iso";
const chunkSize = 1048576; // 1MB

if (!fs.existsSync(inputFile)) {
    console.error(`错误: 找不到文件 ${inputFile}`);
    process.exit(1);
}

const stats = fs.statSync(inputFile);
const totalSize = stats.size;
const fd = fs.openSync(inputFile, 'r');

// 关键修正：手动分割文件名和多重后缀
// 我们寻找第一个出现 .iso 的位置，或者直接硬编码你需要的后缀
const suffix = ".iso";
const baseName = inputFile.endsWith(suffix) 
    ? inputFile.slice(0, -suffix.length) 
    : path.parse(inputFile).name;

console.log(`开始分割文件: ${inputFile}`);
console.log(`目标格式: ${baseName}-开始-结束${suffix}`);

let start = 0;
let partCount = 0;

while (start < totalSize) {
    let end = start + chunkSize;
    if (end > totalSize) {
        end = totalSize;
    }

    const length = end - start;
    const buffer = Buffer.alloc(length);

    fs.readSync(fd, buffer, 0, length, start);

    // 组装成: windows98_audio_vga_2d-0-1048576.bin.zst
    const outputName = `${baseName}-${start}-${end}${suffix}`;
    fs.writeFileSync(outputName, buffer);

    console.log(`生成分片: ${outputName}`);

    start += chunkSize;
    partCount++;
}

fs.closeSync(fd);
console.log(`\n分割完成！`);