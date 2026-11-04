"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),source=fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8"),eight=fs.readFileSync(root+"/d3d8proxy/d3d8_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-pp-core-mut-"));
function mutate(before,after,name,side){const original=side===8?eight:source;const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(original);assert(m);let end=original.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(original[end]==="{")depth++;if(original[end]==="}")depth--;}const body=original.slice(m.index,end);assert(body.includes(before),before);return original.slice(0,m.index)+body.replace(before,after)+original.slice(end);}
const cases=[
 ["CND1.4 refused",9,"shader8_opcode_supported","if (!is_pixel_shader || minor > 4u) return FALSE;","if (!is_pixel_shader || minor > 3u) return FALSE;","policy"],
 ["SDK9 enum normalization omitted",9,"shader8_regtype_valid","switch (register_type >> D3DSP_REGTYPE_SHIFT)","switch (register_type)","policy"],
 ["unknown opcode accepted",9,"shader8_opcode_supported","default:\n        return FALSE;","default:*operand_words=0;return TRUE;","policy"],
 ["pixel RCP accepted",9,"shader8_opcode_supported","if (is_pixel_shader) return FALSE;","if (0 && is_pixel_shader) return FALSE;","policy"],
 ["comment length over-skipped",9,"shader8_validate_body","offset += 1u + comment_words;","offset += 2u + comment_words;","policy"],
 ["END included in body count",9,"shader8_validate_body","*body_token_count = offset;","*body_token_count = offset+1;","policy"],
 ["destination register ignored",9,"shader8_validate_body","tokens[offset + 1u] & D3DSP_REGTYPE_MASK","tokens[offset + 1u] & 0u","policy"],
 ["source register ignored",9,"shader8_validate_body","src < src_count;","src < 0u*src_count;","policy"],
 ["frontend loses minor",8,"validate_shader_body","is_pixel_shader, minor, body_token_count);","is_pixel_shader, minor & 0u, body_token_count);","policy"],
 ["frontend loses kind",8,"validate_shader_body","is_pixel_shader, minor, body_token_count);","is_pixel_shader && 0, minor, body_token_count);","policy"],
 ["frontend bypasses provider",8,"validate_shader_body","return d9wg_d3d9_core_validate_shader_body8(tokens, max_tokens,\n            is_pixel_shader, minor, body_token_count);","(void)tokens;(void)max_tokens;(void)is_pixel_shader;(void)minor;(void)body_token_count;return TRUE;","policy"],
 ["API8 uses permissive9 scanner",9,"create_shader","device_compatibility_api(device) == 8","device_compatibility_api(device) == 0","create"],
 ["API8 accepts shader model2",9,"create_shader","((version >> 8) & 0xffu) == 1u","((version >> 8) & 0xffu) <= 2u","create"],
 ["API8 rejects pixel1.4",9,"create_shader","minor <= (want_pixel ? 4u : 1u)","minor <= (want_pixel ? 3u : 1u)","create"],
 ["API8 loses8192 token bound",9,"create_shader","shader8_validate_body(bytecode + 1, D9WG_D3D8_SHADER_TOKENS,","shader8_validate_body(bytecode + 1, D9_MAX_SHADER_TOKENS,","create"],
 ["stored bytecode omits END",9,"create_shader","token_count = body_count + 2u;","token_count = body_count + 1u;","create"],
 ["shader bytes not copied",9,"create_shader","CopyMemory(shader->code, bytecode, code_bytes);","ZeroMemory(shader->code,code_bytes);","create"],
];
try{for(const[label,side,name,before,after,test]of cases){const file=dir+"/proxy.c";fs.writeFileSync(file,mutate(before,after,name,side));const r=spawnSync(process.execPath,[__dirname+(test==="policy"?"/d8wg_shader_validation_core_test.js":"/d8wg_shader_creation_core_test.js")],{env:{...process.env,[side===8?"D8WG_SHADER_VALIDATION_SOURCE":"D8WG_SHADER_VALIDATION_BACKEND_SOURCE"]:file},encoding:"utf8",timeout:30000}),out=r.stdout+r.stderr;assert(!r.error,label);assert.notEqual(r.status,0,"survived "+label);assert(!/error:|undefined reference|Undefined symbols/.test(out),label+" compile failure: "+out);assert.match(out,/Shader8 (policy|creation) line|Assertion.*failed|Assertion failed/,label+" must fail runtime assertion: "+out);console.log("KILL "+label);}console.log(`PASS shader8 core mutations:${cases.length}/${cases.length}, strict compilation then runtime assertions`);}finally{fs.rmSync(dir,{recursive:true,force:true});}
