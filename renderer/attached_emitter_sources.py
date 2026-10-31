"""Offline proof of attached particle and rigid unlit additive mesh emission.

The native model owns every appended light. No creature-entry inference, frame
zero skeleton evaluation, replacement-texture guess or runtime model scan.
"""
import collections
import hashlib
import json
import math
import struct

from world_scene_builder import array, decode_blp, srgb_to_linear, string, unpack
from world_lights_builder import constant_track
from equipment_emitter_patch import MAX_ATTACHED_SOURCES


class EmissionError(ValueError):
    def __init__(self, category, message):
        self.category = category
        super().__init__(message)


def category(error):
    if isinstance(error, EmissionError):
        return error.category
    return 'native' if 'native lights' in str(error).lower() else 'unsupported'


def consolidate(sources):
    """Merge only nearby same-bone, same-clock emitters; preserve RGB energy."""
    result = []
    for source in sources:
        # Rate values have already been normalized to the source's peak.
        schedule = lambda s: (s.get('rate_track'), s.get('enable_track'), s.get('emission', 'energy'), s.get('pulse'))
        neighbour = next((s for s in result if s['bone'] == source['bone'] and schedule(s) == schedule(source)
            and sum((x-y)**2 for x,y in zip(s['position'],source['position'])) <= .25**2
            and s['intensity']+source['intensity'] <= 4), None)
        if neighbour is None:
            result.append(dict(source))
            continue
        a, b = neighbour['intensity'], source['intensity']
        rgb = [neighbour['rgb'][k]*a + source['rgb'][k]*b for k in range(3)]
        power = max(rgb)
        neighbour['position'] = tuple((x*a+y*b)/(a+b) for x,y in zip(neighbour['position'],source['position']))
        neighbour['rgb'], neighbour['intensity'] = tuple(v/power for v in rgb), power
        for key in ('particles', 'particle_blends', 'particle_flags', 'mesh_batches'):
            neighbour[key] = neighbour.get(key, []) + source.get(key, [])
        neighbour['textures'] = sorted(set(neighbour['textures'] + source['textures']))
    if len(result) > MAX_ATTACHED_SOURCES:
        raise EmissionError('unsupported', 'Source budget exceeded: %d independent sources (limit %d); none silently omitted' % (len(result), MAX_ATTACHED_SOURCES))
    return result


def material_track(blob, offset, fmt):
    """Constant values are evidence only when their keys belong to this M2."""
    interpolation,sequence,nt,ot,nk,ok = unpack('<hh4I',blob,offset)
    animations,animation_offset = unpack('<II',blob,28)
    globals,global_offset = unpack('<II',blob,20)
    if nt != nk or nt > 128 or not -1 <= sequence < globals:
        raise ValueError('Unsupported mesh material track')
    if sequence >= 0 and (nt != 1 or not unpack('<I',blob,global_offset+sequence*4)[0]):
        raise ValueError('Invalid mesh material clock')
    if sequence == -1 and animations:
        if nt != animations:
            raise ValueError('Mesh material does not cover every original animation')
        for slot,(count,_) in enumerate(array(blob,ot,nt,'<II')):
            if count and not unpack('<I',blob,animation_offset+slot*64+12)[0]&0x20:
                raise ValueError('External mesh material animation')
    return constant_track(blob,offset,fmt)


def mesh_sources(assets, path, blob):
    """Qualify single-texture, constant-opacity rigid luminous triangles in skin0.

    CM2Shared::SetVertices uses skin bones, then section boneComboIndex and the
    M2 bone-combo table. The original vertex's indices are not that palette.
    https://github.com/SatyPardus/wowdecomp/blob/b1c71b8bd108823bca952f16c6f83a7ad33b9648/Engine/Source/Model2/M2Shared.cpp#L1106
    """
    materials = array(blob, *reversed(unpack('<II',blob,112)), '<2H')
    if not any(flags & 1 and blend in (3,4) for flags,blend in materials):
        return [], {}, 0
    skin = assets.read(path[:-3]+'00.skin')
    if skin[:4] != b'SKIN':
        raise ValueError('Unsupported skin')
    def rows(data, offset, fmt):
        return array(data,*reversed(unpack('<II',data,offset)),fmt)
    lookup = [v for (v,) in rows(skin,4,'<H')]
    indices = [v for (v,) in rows(skin,12,'<H')]
    skin_bones = rows(skin,20,'<4B')
    subsets, batches = rows(skin,28,'<10H7f'), rows(skin,36,'<2B11H')
    vertices, textures = rows(blob,60,'<3f4B4B3f4f'), rows(blob,80,'<4I')
    bone_combos = [v for (v,) in rows(blob,120,'<H')]
    tex_combos = [v for (v,) in rows(blob,128,'<H')]
    weight_combos = [v for (v,) in rows(blob,144,'<h')]
    transform_combos = [v for (v,) in rows(blob,152,'<h')]
    colors, color_offset = unpack('<II',blob,72)
    weights, weight_offset = unpack('<II',blob,88)
    bones, bone_offset = unpack('<II',blob,44)
    if not bones or bone_offset < 304 or bone_offset+bones*88 > len(blob):
        raise ValueError('Invalid original mesh bones')
    result, rejected, candidates = [], {}, 0
    for index,batch in enumerate(batches):
        try:
            if batch[6] >= len(materials):
                raise ValueError('Invalid mesh material')
            flags, blend = materials[batch[6]]
            if not flags & 1 or blend not in (3,4):
                continue
            candidates += 1
            if batch[3] >= len(subsets) or batch[8] != 1 or batch[2] != 0:
                raise ValueError('Only simple single-texture unlit additive shader supported')
            subset = subsets[batch[3]]
            if subset[0] != 0:
                raise ValueError('Display-controlled mesh geoset')
            if batch[9] >= len(tex_combos) or tex_combos[batch[9]] >= len(textures):
                raise ValueError('Invalid mesh texture combo')
            texture = textures[tex_combos[batch[9]]]
            if texture[0] or not texture[2]:
                raise ValueError('Dynamic mesh texture replacement')
            # Environment coordinates and texture animation cannot use the
            # unmodified first UV coordinates as an emission proof.
            units = rows(blob,136,'<h')
            if batch[10] >= len(units) or units[batch[10]][0] != 0:
                raise ValueError('Non-UV0 mesh texture coordinates')
            if batch[12] >= len(transform_combos) or transform_combos[batch[12]] != -1:
                raise ValueError('Animated/transformed mesh texture')
            tint, opacity = (1.,1.,1.), 1.
            if batch[5] != 65535:
                if batch[5] >= colors:
                    raise ValueError('Invalid mesh color index')
                tint = material_track(blob,color_offset+batch[5]*40,'<3f')
                opacity *= material_track(blob,color_offset+batch[5]*40+20,'<h')[0]/32767
            if batch[11] != 65535:
                if batch[11] >= len(weight_combos):
                    raise ValueError('Invalid mesh opacity combo')
                weight = weight_combos[batch[11]]
                if weight >= 0:
                    if weight >= weights:
                        raise ValueError('Invalid mesh opacity index')
                    opacity *= material_track(blob,weight_offset+weight*20,'<h')[0]/32767
            if not all(math.isfinite(v) and 0 <= v <= 1 for v in (*tint,opacity)):
                raise ValueError('Invalid mesh tint/opacity')
            if not opacity:
                raise ValueError('Invisible mesh material')
            name = string(blob,texture[3])
            w,h,rgba = decode_blp(assets.read(name),64)
            first, count = subset[4]+(subset[1]<<16), subset[5]
            if not count or count%3 or first+count > len(indices):
                raise ValueError('Invalid mesh triangle range')
            grouped = collections.defaultdict(list)
            for at in range(first,first+count,3):
                ids = indices[at:at+3]
                if any(v >= len(lookup) or v >= len(skin_bones) or lookup[v] >= len(vertices) for v in ids):
                    raise ValueError('Invalid mesh vertex range')
                verts = [vertices[lookup[v]] for v in ids]
                anchors = set()
                for vertex, skin_index in zip(verts,ids):
                    if sum(vertex[3:7]) != 255:
                        raise ValueError('Non-normalized mesh bone weights')
                    for influence,weight in enumerate(vertex[3:7]):
                        if not weight:
                            continue
                        combo = subset[7]+skin_bones[skin_index][influence]
                        if combo >= len(bone_combos) or bone_combos[combo] >= bones:
                            raise ValueError('Invalid mesh skin bone palette')
                        anchors.add(bone_combos[combo])
                if len(anchors) != 1:
                    raise ValueError('Luminous mesh deforms across multiple bones')
                bone = anchors.pop()
                # The appended native light uses this same original bone matrix,
                # including camera-facing billboard rotation; no bind-pose bake.
                a,b,c = [v[:3] for v in verts]
                u,v = [tuple(y-x for x,y in zip(a,p)) for p in (b,c)]
                area = math.sqrt(sum(x*x for x in (u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])))/2
                rgb = [0.,0.,0.]
                # Four fixed barycentric samples retain actual UV-local color,
                # alpha and triangle area, instead of borrowing an atlas mean.
                for bary in ((1/3,1/3,1/3),(.6,.2,.2),(.2,.6,.2),(.2,.2,.6)):
                    uv = [sum(verts[j][14+k]*bary[j] for j in range(3)) for k in range(2)]
                    xy = [min(size-1,max(0,int((q%1 if texture[1]&(1<<k) else min(1,max(0,q)))*size))) for k,(q,size) in enumerate(zip(uv,(w,h)))]
                    pixel = (xy[1]*w+xy[0])*4
                    alpha = rgba[pixel+3]/255 if blend == 4 else 1.
                    for k in range(3):
                        rgb[k] += srgb_to_linear(rgba[pixel+k]/255)*tint[k]*opacity*alpha/4
                if area and max(rgb):
                    grouped[bone].append((tuple(sum(v[k] for v in verts)/3 for k in range(3)),area,rgb))
            for bone, triangles in grouped.items():
                # Separate spatial regions on one bone before consolidation.
                regions = []
                for position,area,rgb in triangles:
                    region = next((r for r in regions if sum((x-y)**2 for x,y in zip(r[0],position)) <= .35**2),None)
                    if region is None:
                        region = [position,0.,[0.,0.,0.],[0.,0.,0.]]
                        regions.append(region)
                    energy = area*max(rgb)
                    region[1] += energy
                    for k in range(3):
                        region[2][k] += rgb[k]*area*.35
                        region[3][k] += position[k]*energy
                for _, energy,rgb,weighted_position in regions:
                    peak = max(rgb)
                    if peak < .00005:
                        continue
                    if peak > 4:
                        raise ValueError('Mesh region exceeds bounded native energy')
                    result.append({'bone':bone,'position':tuple(v/energy for v in weighted_position),'rgb':tuple(v/peak for v in rgb),
                        'intensity':peak,'start':.5,'end':8.,'particles':[],'mesh_batches':[index],'textures':[name],
                        'emission':'fire' if any(s in name for s in ('flame','fire','candle')) else 'energy',
                        'basis':'rigid original skin bone; constant unlit additive mesh, triangle area x UV texture x tint x opacity'})
        except (ValueError,FileNotFoundError,struct.error,IndexError) as error:
            rejected[index] = str(error)
    return result,rejected,candidates


def attached_sources(assets, path, blob):
    from build_spell_emitters import spell_sources
    if len(blob) < 304 or blob[:4] != b'MD20' or unpack('<I',blob,4)[0] != 264:
        raise EmissionError('unsupported','Not WotLK v264')
    if unpack('<I',blob,264)[0]:
        raise EmissionError('native','Existing native lights preserved')
    count, offset = unpack('<II',blob,296)
    stride = 492 if unpack('<I',blob,16)[0]&512 else 476
    if count > 4096 or offset+count*stride > len(blob):
        raise EmissionError('unsupported','Invalid particle array')
    particle_candidates = sum(unpack('<B',blob,offset+i*stride+40)[0] in (3,4) for i in range(count))
    rejected = {}
    try:
        particles,rejected['particles'] = spell_sources(assets,blob,max_sources=None)
    except ValueError as error:
        particles = []
        rejected['particles'] = str(error)
    try:
        meshes,rejected['mesh_batches'],mesh_candidates = mesh_sources(assets,path,blob)
    except (ValueError,FileNotFoundError,struct.error,IndexError) as error:
        meshes,mesh_candidates = [],1
        rejected['mesh_batches'] = str(error)
    result = consolidate(particles+meshes)
    if not result:
        raise EmissionError('unsupported' if particle_candidates or mesh_candidates or unpack('<I',blob,288)[0] else 'not_emissive',
                            'No proven attached source: '+json.dumps(rejected,sort_keys=True))
    return result,rejected


def write_stage(assets, output, candidates, report, stem, empty):
    """Shared offline M2-only archive, byte readback and auditable exclusions."""
    from build_spell_emitters import append_spell_lights, validate_spell_lights
    from mpq import Archive
    output.mkdir(parents=True,exist_ok=True)
    payload = {}
    totals = collections.Counter(scanned=len(candidates),native=0,unsupported=0,not_emissive=0,patched=0)
    report.update(models={},skipped={},source_archives=assets.fingerprint())
    report['coverage_categories'] = {
        'native':'Existing native light array preserved without layering new lights.',
        'not_emissive':'No additive particle, unlit additive mesh or ribbon evidence in the enumerated model.',
        'unsupported':'Potential emission could not be proven safe, or the model was unavailable/over budget.',
        'patched':'At least one proven source; rejected regions are listed separately, so this is not a claim of complete luminous coverage.'}
    for path,info in sorted(candidates.items()):
        try:
            original = assets.read(path)
            emitters,refused = attached_sources(assets,path,original)
            # Display replacement palettes are server-selected. Preserve only
            # sources whose actual authored particle RGB is not replaceable.
            if info.get('replacement_colors'):
                particle_offset = unpack('<I',original,300)[0]
                stride = 492 if unpack('<I',original,16)[0]&512 else 476
                if any(unpack('<h',original,particle_offset+i*stride+42)[0] not in (-1,0) for s in emitters for i in s['particles']):
                    raise ValueError('Shared display variants replace particle colour; no universal M2 light colour')
            patched = append_spell_lights(original,emitters)
            validate_spell_lights(original,patched,emitters)
        except (ValueError,FileNotFoundError,struct.error) as error:
            reason = category(error)
            totals[reason] += 1
            report['skipped'][path] = {'category':reason,'reason':str(error)}
            continue
        payload[path] = patched
        report['models'][path] = {**info,'emitters':emitters,'qualified_sources':len(emitters),'omitted_sources':0,
            'rejected_regions':refused,'before_sha256':hashlib.sha256(original).hexdigest(),
            'after_sha256':hashlib.sha256(patched).hexdigest(),'source_archive':assets.origin(path)}
    if not payload:
        raise ValueError(empty)
    target = output/(stem+'-lights-staged.mpq')
    with Archive(target,create=True,capacity=max(4096,len(payload)*2)) as archive:
        temporary = output/'model.tmp'
        for path,data in payload.items():
            temporary.write_bytes(data)
            archive.add(temporary,path)
        temporary.unlink()
    with Archive(target) as archive:
        names = {n.lower() for n in archive.names() if not n.startswith('(')}
        if names != set(payload) or any(archive.read(p) != b for p,b in payload.items()):
            raise ValueError('Model-only MPQ byte roundtrip failed')
    totals.update(patched=len(payload),native_lights=sum(len(v['emitters']) for v in report['models'].values()),
                  validated_native_models=len(payload),mpq_byte_roundtrips=len(payload),dbc_files=0,wdb_files=0)
    report['counts'] = dict(totals)
    report['mpq_sha256'] = hashlib.sha256(target.read_bytes()).hexdigest()
    report['archive_warnings'] = assets.warnings() if hasattr(assets,'warnings') else []
    (output/(stem+'-lights-report.json')).write_text(json.dumps(report,indent=2)+'\n')
    return report
