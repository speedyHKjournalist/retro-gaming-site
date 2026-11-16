"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const source=fs.readFileSync(path.join(__dirname,'../ddrawproxy/d3d7_proxy.inc'),'utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-empty-mutants-'));
const cases=[
 ['TnL transition ignored','d3d7_empty_vb_input_result','deferred_tnl_binding ? E_FAIL : D3D_OK','(deferred_tnl_binding && FALSE) ? E_FAIL : D3D_OK'],
 ['HAL/RGB receive TnL error','d3d7_empty_vb_input_result','guid_equal(&device->device_guid, &IID_IDirect3DTnLHalDevice) &&','(TRUE || guid_equal(&device->device_guid, &IID_IDirect3DTnLHalDevice)) &&'],
 ['legacy device receives TnL error','d3d7_empty_vb_input_result','device->creation_version >= 7u &&','TRUE &&'],
 ['cold VB rejected like a first-call counter','d3d7_empty_vb_input_result','device->draw_input_kind == D3D7_INPUT_EMPTY_INDEXED_STRIDED &&','TRUE &&'],
 ['successful VB bind fails to consume transition','d3d7_empty_vb_input_result','d3d7_commit_draw_input(device, D3D7_INPUT_VERTEX_BUFFER, fvf);','(void)device;'],
 ['packed zero input keeps stale strided binding','d3d7_empty_stream_input_result',': D3D7_INPUT_PACKED, fvf);',': D3D7_INPUT_EMPTY_INDEXED_STRIDED, fvf);'],
 ['invalid empty input commits binding','d3d7_empty_stream_input_result','if (SUCCEEDED(result))','if (TRUE || SUCCEEDED(result))'],
 ...['DrawPrimitive','DrawIndexedPrimitive','DrawPrimitiveStrided','DrawIndexedPrimitiveStrided'].map(name=>{const condition=name.includes('Indexed')?'!vertex_count || !index_count':'!vertex_count';return [name+' empty stream still dispatched','d3d7_device_'+name,'if ('+condition+')','if (FALSE && ('+condition+'))'];}),
 ['vertex-buffer empty stream rejected','d3d7_device_DrawPrimitiveVB','if (!vertex_count) return d3d7_empty_vb_input_result','if (FALSE && !vertex_count) return d3d7_empty_vb_input_result'],
 ['indexed vertex-buffer empty stream rejected','d3d7_device_DrawIndexedPrimitiveVB','if (!vertex_count || !index_count) return d3d7_empty_vb_input_result','if (FALSE && (!vertex_count || !index_count)) return d3d7_empty_vb_input_result'],
 ['zero index count requires index pointer','d3d7_device_DrawIndexedPrimitiveVB','(index_count && !indices)','!indices'],
 ['unused texture-size field accepted','d3d7_empty_draw_result','if (texture_count > 8u || ((fvf >> 16u) & ~texture_sizes))','if (FALSE && (texture_count > 8u || ((fvf >> 16u) & ~texture_sizes)))'],
 ['strided RHW accepted','d3d7_empty_draw_result','if (strided && (fvf & 0x000eu) == 0x0004u)','if (FALSE && strided && (fvf & 0x000eu) == 0x0004u)'],
 ['empty draw dirties target','d3d7_device_DrawPrimitive','return d3d7_empty_stream_input_result(d3d7_device_from_iface(iface), fvf, FALSE, FALSE, vertex_count, 0u);','return (d3d7_mark_draw_surfaces_dirty(d3d7_device_from_iface(iface)), d3d7_empty_stream_input_result(d3d7_device_from_iface(iface), fvf, FALSE, FALSE, vertex_count, 0u));'],
 ['nonempty draw bypassed','d3d7_device_DrawPrimitive','if (!vertex_count)','if (vertex_count)'],
];
try{for(const [label,name,from,to]of cases){const match=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(match,name);let end=source.indexOf('{',match.index)+1,depth=1;for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}const body=source.slice(match.index,end);assert.equal(body.split(from).length,2,label);let changed=body.replace(from,to);assert.notEqual(changed,body,label);const file=dir+'/proxy.inc';fs.writeFileSync(file,source.slice(0,match.index)+changed+source.slice(end));const r=spawnSync(process.execPath,[__dirname+'/ddraw_empty_draw_test.js'],{encoding:'utf8',env:{...process.env,DDRAW_EMPTY_SOURCE:file}});assert.notEqual(r.status,0,'survived '+label);assert.match(r.stderr,/empty draw line/,label+': compiled runtime assertion required\n'+r.stderr);assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(r.stderr),r.stderr);console.log('KILL '+label);}console.log('PASS empty draw mutations: '+cases.length+'/'+cases.length+' compiled runtime assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
