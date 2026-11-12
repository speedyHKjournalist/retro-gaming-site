"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../ddrawproxy/ddraw_proxy.c"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-query-mutants-"));
const cases=[
    ["cube root reports every face","root_seed.ddsCaps.dwCaps2 = (root_seed.ddsCaps.dwCaps2 &\n                ~DDSCAPS2_CUBEMAP_ALLFACES) | DDSCAPS2_CUBEMAP |\n                DDSCAPS2_CUBEMAP_POSITIVEX;","root_seed.ddsCaps.dwCaps2 |= DDSCAPS2_CUBEMAP_ALLFACES;"],
    ["same-face query returns current node","DDSurface *candidate = surface->mip_next;","DDSurface *candidate = surface;"],
    ["same-face mip search disabled","if (candidate && surface->cube_face < 6u &&","if (FALSE && candidate && surface->cube_face < 6u &&"],
    ["cube root returns itself as attachment","candidate && candidate != surface &&","candidate && TRUE &&"],
    ["cube mip selector ignores requested face","(caps->dwCaps2 & face_caps[surface->cube_face]) &&","TRUE &&"],
    ["cube-face query ignores caps","\n                    (candidate->desc.ddsCaps.dwCaps & caps->dwCaps) == caps->dwCaps)","\n                    TRUE)"],
    ["cube mip query ignores caps","\n                (candidate->desc.ddsCaps.dwCaps & caps->dwCaps) == caps->dwCaps)","\n                TRUE)"],
    ["leaf loses complex membership","child_caps |= DDSCAPS_COMPLEX;","if (level + 1u < level_count) child_caps |= DDSCAPS_COMPLEX; else child_caps &= ~DDSCAPS_COMPLEX;"],
    ["query requires mipmap selector","if (surface->mip_next\n            && (surface->mip_next->desc.ddsCaps.dwCaps", "if (surface->mip_next && (caps->dwCaps & DDSCAPS_MIPMAP)\n            && (surface->mip_next->desc.ddsCaps.dwCaps"],
    ["unrelated caps return a mip","(surface->mip_next->desc.ddsCaps.dwCaps & caps->dwCaps) == caps->dwCaps", "(caps->dwCaps || TRUE)"],
    ["query returns current level","*out = (IDirectDrawSurface7 *)surface->mip_next;", "*out = (IDirectDrawSurface7 *)surface;"],
];
try{for(const [name,before,after]of cases){assert.equal(source.split(before).length,2,name);const file=dir+"/mutant.c";fs.writeFileSync(file,source.replace(before,after));const run=spawnSync(process.execPath,[path.join(__dirname,"ddraw_attachment_query_test.js")],{encoding:"utf8",env:{...process.env,DDRAW_ATTACHMENT_SOURCE:file}});assert.notEqual(run.status,0,"survived "+name);assert(/attachment query line \d+:/.test(run.stderr),"compiled runtime assertion required: "+name+"\n"+run.stderr);assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(run.stderr),run.stderr);console.log("KILL "+name);}console.log("PASS actual mip attachment faults: "+cases.length+"/"+cases.length+", including the original leaf-cap and selector failures");}finally{fs.rmSync(dir,{recursive:true,force:true});}
