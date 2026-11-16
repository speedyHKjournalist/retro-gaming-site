'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),sources={legacy:fs.readFileSync(root+'/ddrawproxy/d3d_legacy_proxy.inc','utf8'),seven:fs.readFileSync(root+'/ddrawproxy/d3d7_proxy.inc','utf8')},dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-sphere-mut-'));
const cases=[
 ['legacy','legacy intersection margin omitted','distance - radii[index] < 0.5f','distance - radii[index] < 0.0f'],
 ['legacy','legacy half-unit boundary inclusive','distance - radii[index] < 0.5f','distance - radii[index] <= 0.5f'],
 ['legacy','legacy outside test incorrectly padded','if (distance < -radii[index])','if (distance < 0.5f - radii[index])'],
 ['legacy','inverse clip cube lacks X bias','bias._41 = bias._42 = 0.5f;','bias._41 = 0.0f;bias._42 = 0.5f;'],
 ['legacy','perspective corner divide omitted','double reciprocal = 1.0 / v[3];','double reciprocal = 1.0;'],
 ['legacy','frustum left and right swapped','{{0,2,4},{1,5,3},{2,3,6}','{{1,5,3},{0,2,4},{2,3,6}'],
 ['legacy','plane X coefficient not normalized','out[0] = (float)(nx*scale);','out[0] = (float)nx;'],
 ['legacy','singular sphere transform reports success','if (!d3d3_sphere_planes(&matrix, planes))\n        return DDWG_TRACE_ERROR(D3DERR_INVALIDMATRIX);','if (!d3d3_sphere_planes(&matrix, planes))\n        return DDWG_TRACE_ERROR(D3D_OK);'],
 ['seven','Device7 uses legacy half-unit margin','if (distance > -radii[index] && distance < radii[index])','if (distance > -radii[index] && distance - radii[index] < 0.5f)'],
 ['seven','Device7 extracts a matrix row','world_view_projection._3##column','world_view_projection._##column##3'],
 ['seven','enabled user clip plane ignored','(device->render_states[D3DRENDERSTATE_CLIPPLANEENABLE] & 0x3fu)','(device->render_states[D3DRENDERSTATE_CLIPPLANEENABLE] & 0x0u)'],
];
try{for(const[kind,name,before,after]of cases){const source=sources[kind];assert.equal(source.split(before).length,2,name+' unique mutation');const file=dir+'/'+kind+'.inc';fs.writeFileSync(file,source.replace(before,after));const env={...process.env,[kind==='legacy'?'DDRAW_SPHERE_LEGACY_SOURCE':'DDRAW_SPHERE_SEVEN_SOURCE']:file};const r=spawnSync(process.execPath,[__dirname+'/ddraw_sphere_visibility_test.js'],{encoding:'utf8',env,timeout:30000});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const output=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+' compile failure '+output);assert(/sphere visibility line/.test(output),name+' no runtime assertion '+output);}console.log('PASS actual sphere visibility runtime mutations: '+cases.length+'/'+cases.length);}finally{fs.rmSync(dir,{recursive:true,force:true});}
