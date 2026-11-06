"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..');
const sources={legacy:fs.readFileSync(root+'/runtime/d9wg_texture_legacy_color.h','utf8'),guest:fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8'),dirty:fs.readFileSync(root+'/runtime/d9wg_texture_dirty2d.h','utf8'),copy:fs.readFileSync(root+'/runtime/d9wg_texture_dirty_copy2d.h','utf8')};
const mutations=[
 ['legacy red/blue swapped','legacy','to[0] = (BYTE)blue;','to[0] = (BYTE)red;'],
 ['legacy XRGB alpha treated as defined','legacy','alpha = source_format == 22u ? 255u : from[3];','alpha = from[3];'],
 ['legacy copies ignore dirty regions','legacy','if (cube || s->dirty_full) {','if (TRUE) {'],
 ['legacy conversion leaks into API9','legacy','return api == 8u &&','return api >= 8u &&'],
 ['legacy ignores upload failure','legacy','return emitted;','return TRUE;'],
 ['legacy skips largest dimension mip match','legacy','++plan.source_first;','plan.source_first += 0;'],
 ['legacy ignores destination width','legacy','if (area.right > (LONG)width)','if (FALSE && area.right > (LONG)width)'],
 ['legacy skips last face validation','legacy','for (UINT face = 0; face < faces; ++face) for (UINT i = 0; i < plan.levels; ++i)','for (UINT face = 0; face < 1u; ++face) for (UINT i = 0; i < plan.levels; ++i)'],
 ['explicit mip generation reads stale GPU base','guest','if (texture->pool == D3DPOOL_MANAGED && !texture_flush_dirty(texture))','if (FALSE && texture->pool == D3DPOOL_MANAGED && !texture_flush_dirty(texture))'],
 ['managed unlock uploads hidden CPU writes','guest','texture->pool != D3DPOOL_MANAGED && !(level_data->lock_flags & D3DLOCK_READONLY)','!(level_data->lock_flags & D3DLOCK_READONLY)'],
 ['NO_DIRTY_UPDATE ignored','guest','!(flags & (D3DLOCK_READONLY | D3DLOCK_NO_DIRTY_UPDATE))','!(flags & D3DLOCK_READONLY)'],
 ['readonly locks mark dirty','guest','!(flags & (D3DLOCK_READONLY | D3DLOCK_NO_DIRTY_UPDATE))','!(flags & D3DLOCK_NO_DIRTY_UPDATE)'],
 ['only initial base mip uploaded','dirty','i < texture->level_count; ++i) {','i < 1u; ++i) {'],
 ['base dirty does not propagate to mips','dirty','UINT end = level ? level + 1u : texture->level_count;','UINT end = level + 1u;'],
 ['partial dirty uploads unrelated pixels','dirty','full[i] = !area.left && !area.top && (UINT)area.right == data->width\n                && (UINT)area.bottom == data->height;','full[i] = TRUE;'],
 ['eviction fails to reload managed textures','guest','            texture_dirty_all(texture);','            texture->dirty_pending = FALSE;'],
 ['no dirty UpdateTexture copies everything','copy','if (data->dirty_full) {','if (TRUE) {'],
 ['UpdateTexture leaves dirty state unconsumed','copy','texture_dirty_clear(&source->levels[i]);','(void)source->levels[i];'],
 ['managed retry falsely reports success','dirty','if (!emit_texture_update(texture, i, &node->rect)) return FALSE;','if (!emit_texture_update(texture, i, &node->rect)) return TRUE;']
];
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-dirty-mutants-'));try{for(const[label,kind,before,after] of mutations){const src=sources[kind];assert(src.includes(before),label);const file=path.join(dir,kind+'.h');fs.writeFileSync(file,src.replace(before,after));const env={...process.env,[{legacy:'D9WG_LEGACY_COLOR_HEADER',guest:'D9WG_DIRTY_SOURCE',dirty:'D9WG_DIRTY_HEADER',copy:'D9WG_DIRTY_COPY_HEADER'}[kind]]:file};const r=spawnSync(process.execPath,[path.join(__dirname,'d9wg_texture_dirty_test.js')],{env,encoding:'utf8'});assert.notEqual(r.status,0,'survived: '+label);assert.match(r.stderr,/dirty texture line/,'runtime assertion required: '+label);assert(!r.stderr.includes('error:'),'compile error does not count: '+label);}console.log(`PASS texture dirty mutations: ${mutations.length}/${mutations.length}; original pixel/region/state assertions reject each fault`);}finally{fs.rmSync(dir,{recursive:true,force:true});}
