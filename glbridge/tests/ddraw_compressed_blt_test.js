"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),{createHash}=require("node:crypto");
const root=path.resolve(__dirname,".."),core=path.resolve(root,"../../v86"),source=fs.readFileSync(process.env.DDRAW_BC_BLT_SOURCE||root+"/ddrawproxy/ddraw_proxy.c","utf8"),header=process.env.DDRAW_BC_CODEC_HEADER||root+"/ddrawproxy/ddraw_bc_codec.h",dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-bc-blt-"));
const reference=JSON.parse(fs.readFileSync(core+"/tests/glbridge/fixtures/ddraw_native_blits.json"));
assert.equal(createHash("sha256").update(fs.readFileSync(path.resolve(root,"../",reference.source))).digest("hex"),reference.sourceSha256);
function extract(name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let at=source.indexOf("{",m.index)+1,depth=1;for(;depth;at++){if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return source.slice(m.index,at);}
const native=reference.blocks.filter(r=>r.pattern<2||[1,3].includes(r.format));
const nativeCases=native.map(row=>{const pixels=[];for(let i=0;i<16;i++){let p=row.pattern===0?0xff0000ff:[0,3,7,10,11,14].includes(i)?0xff00ff00:0xff0000ff;if(row.pattern===2)p=(p&0xffffff)|((i*17)<<24);if(row.pattern===3)p=(p&0xffffff)|((i&1)?0xff000000:0);pixels.push(p>>>0);}const bytes=[];row.words.forEach(w=>bytes.push(w&255,w>>>8&255,w>>>16&255,w>>>24));return `{${(0x30545844+(row.format<<24))>>>0}u,{${pixels.map(p=>p+"u")}},{${bytes}}}`;});
const code=`/* Existing fixtures model undetached subresources. Detached mappings use
 * actual helpers in ddraw_memory_graph_test and real XP/GPU graph probes. */
#define surface_wire_mip_level(s) ((s)->mip_level)
#define surface_wire_cube_face(s) ((s)->cube_face)
#include "${root}/ddrawproxy/ddraw_protocol.h"
#include "${root}/runtime/d9wg_runtime.h"
#include "${root}/runtime/d9wg_format_layouts.h"
#include "${header}"
${fs.readFileSync(__dirname+"/native/ddraw_bc_blt_prefix.h","utf8")}
/* Non-depth fixture; actual masked depth transfers are covered by depth_transfer. */
static BOOL surface_depth_transfer_masks(DDSurface*s,DWORD*wz,DWORD*ws,DWORD*cz,DWORD*cs){const D9WGFormatLayout*l=d9wg_find_format(s->format);if(l&&l->depth){MUST(0);}*wz=*ws=*cz=*cs=0;return FALSE;}
static void surface_repack_depth_rows(DDSurface*s,BYTE*bytes,UINT pitch,UINT width,UINT rows,BOOL to_wire){(void)s;(void)bytes;(void)pitch;(void)width;(void)rows;(void)to_wire;MUST(0);}
${["format_bytes_per_pixel","format_compressed_block_bytes","format_is_compressed","format_is_yuv","surface_memory_changed","surface_memory_upload_aliases","emit_surface_upload","prepare_surface_for_host","surface_memory_rect","surface_can_bc_blt","surface_bc_rect","surface_blt_sample_map","surface_bc_pixel","surface_bc_blt","emit_blt"].map(extract).join("\n")}
static const struct{UINT format;DWORD pixels[16];BYTE bytes[16];}native[]={${nativeCases.join(",\n")}};
${fs.readFileSync(__dirname+"/native/ddraw_bc_blt_cases.h","utf8")}
`;
try{fs.writeFileSync(dir+"/blit.c",code);const built=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer",dir+"/blit.c","-o",dir+"/blit"],{encoding:"utf8"});assert.equal(built.status,0,built.stderr);const run=spawnSync(dir+"/blit",[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
