"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'decl8-core-mut-'));
const sources={core:fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8'),parser:fs.readFileSync(root+'/runtime/d9wg_declaration8.h','utf8'),policy:fs.readFileSync(root+'/runtime/d9wg_declaration8_policy.h','utf8')};
const plan=[
 ['literal data boundary','parser','offset += words;','offset += 1;'],
 ['last constant register','parser','> max_const_vectors - vectors','>= max_const_vectors - vectors'],
 ['zero vectors','parser','if (!vectors || vectors > max_const_vectors ||','if (vectors > max_const_vectors ||'],
 ['constant offset','parser','memcpy((uint8_t *)const_data + start * 16u,','memcpy((uint8_t *)const_data,'],
 ['constant masks','parser','const_mask[reg] = 1;','const_mask[reg] = 0;'],
 ['constant uniqueness','parser','if (!const_mask[reg]) ++*const_vector_count;','++*const_vector_count;'],
 ['stream offset advance','parser','offsets[stream] += size;','offsets[stream] += 4;'],
 ['skip bytes','parser','skip * (uint32_t)sizeof(uint32_t)','skip'],
 ['skip location','parser','offsets[stream] += skip','offsets[0] += skip'],
 ['element capacity','parser','count >= max_elements','count > max_elements'],
 ['element semantics','parser','elements[count].usage_index = (uint8_t)usage_index;','elements[count].usage_index = 0;'],
 ['element stream','parser','elements[count].stream = (uint16_t)stream;','elements[count].stream = 0;'],
 ['tessellator input','parser','if (token & D3D8VSD_STREAM_TESSFLAG)','if (0)'],
 ['normal2 semantics','policy','*usage = D3D9DECLUSAGE_NORMAL; *usage_index = 1u; return;','*usage = D3D9DECLUSAGE_NORMAL; *usage_index = 0u; return;'],
 ['unknown register meaning','policy','*usage_index = 8u + (reg & 0x7u);','*usage_index = (reg & 0x7u);'],
 ['float3 size','policy','case D3D8VSDT_FLOAT3: return 12u;','case D3D8VSDT_FLOAT3: return 16u;'],
 ['ubyte4 mapping','policy','case D3D8VSDT_UBYTE4: return D3D9DECLTYPE_UBYTE4;','case D3D8VSDT_UBYTE4: return D3D9DECLTYPE_D3DCOLOR;'],
 ['profile','core','device_compatibility_api(device_from_iface(iface)) != 8u','device_compatibility_api(device_from_iface(iface)) != 9u'],
 ['max token bound','core','d9wg_declaration8_validate(tokens, D9WG_D3D8_SHADER_TOKENS,','d9wg_declaration8_validate(tokens, D9WG_D3D8_SHADER_TOKENS - 1,'],
 ['failed output','core','*out = NULL;','/* output left unchanged */'],
 ['normal-only rule','core','if (declaration_only && e->usage == D3D9DECLUSAGE_NORMAL','if (((void)declaration_only, 0) && e->usage == D3D9DECLUSAGE_NORMAL'],
 ['secondary normal exception','core','&& !e->usage_index && e->type','&& e->usage_index == 1 && e->type'],
 ['SDK9 offset','core','e->stream, e->offset, e->type','e->stream, 0, e->type'],
 ['SDK9 usage','core','e->method, e->usage, e->usage_index','e->method, e->usage_index, e->usage'],
 ['SDK9 terminator','core','elements[count] = (D3DVERTEXELEMENT9)D3DDECL_END();','elements[count] = (D3DVERTEXELEMENT9){0xff,0,0,0,0,0};'],
];
function span(s,n){const m=new RegExp('^[A-Za-z_][^;{}\\n]*?\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(s);assert(m,n);let e=s.indexOf('{',m.index)+1,d=1;for(;d;e++){if(s[e]==='{')d++;if(s[e]==='}')d--;}return[m.index,e];}
try{let killed=0;for(const[name,kind,from,to]of plan){let original=sources[kind],candidate;if(kind==='core'){const[a,b]=span(original,'d9wg_d3d9_core_vertex_declaration8'),body=original.slice(a,b);assert(body.includes(from),name);candidate=original.slice(0,a)+body.replace(from,to)+original.slice(b);}else{assert(original.includes(from),name);candidate=original.replace(from,to);}const file=dir+'/candidate.h';fs.writeFileSync(file,candidate.replace(/#include "([^"]+)"/g,(_,p)=>'#include "'+path.resolve(root,kind==='core'?'d3d9proxy':'runtime',p)+'"'));let envName=kind==='core'?'D8WG_DECL_CORE_SOURCE':'D8WG_DECLARATION8_HEADER';if(kind==='policy'){const wrapper=dir+'/wrapper.h';fs.writeFileSync(wrapper,sources.parser.replace('#include "d9wg_declaration8_policy.h"','#include "'+file+'"').replace('#include "../d3d9proxy/d3d9_protocol.h"','#include "'+root+'/d3d9proxy/d3d9_protocol.h"'));candidate=wrapper;}else candidate=file;
 const r=spawnSync(process.execPath,[__dirname+'/d8wg_declaration_core_test.js'],{encoding:'utf8',timeout:90000,env:{...process.env,[envName]:candidate}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const output=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+' compiler failure: '+output);assert(/Declaration8 core case \d+ line \d+:/.test(output),name+' must fail actual C assertion: '+output);killed++;}console.log('PASS declaration8 core mutations: '+killed+'/'+plan.length+' strictly compiled then failed actual numeric C assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
