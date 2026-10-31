"""Offline, fail-closed WMO lamp discovery from material, texture and enclosure geometry."""
import math
import re

GLASS = re.compile(r'(?:lamp|lantern).*glass|glass.*(?:lamp|lantern)', re.I)
GLASS_EMITTER = 16


def candidates(root):
    from world_scene_builder import array, chunks, string
    data = dict(chunks(root)); names = data.get('MOTX', b'')
    if len(data.get('MOMT', b'')) % 64:
        raise ValueError('Invalid WMO material records')
    return {i: string(names, m[3]) for i, m in enumerate(array(data['MOMT'], 0, len(data['MOMT']) // 64, '<16I'))
            if m[0] & 0x10 and m[2] == 0 and GLASS.search(string(names, m[3]).replace('/', chr(92)).rsplit(chr(92), 1)[-1])}


def analyze(root, group, read_texture, group_index=0, material_names=None):
    """Return fixture-local lights and the index offsets of their own glass triangles.

    A four-panel, consistently oriented enclosure plus a bounded luminous texture
    is required. Unsupported or ambiguous geometry produces no source and no tag.
    """
    from world_scene_builder import array, chunks, decode_blp, srgb_to_linear, wmo_group
    from world_emitter_coverage import calibrated_intensity, triangle_area
    from point_transmission_builder import uv_texels
    material_names = candidates(root) if material_names is None else material_names
    if not material_names:
        return [], {}
    if not 0 <= group_index < 128:
        return [], {} # FGS2's bounded fixture tag cannot encode this group.
    sub = {}
    for tag,payload in chunks(wmo_group(group)[0]['MOGP'], 68):sub.setdefault(tag,payload)
    vertices = array(sub['MOVT'], 0, len(sub['MOVT']) // 12, '<3f')
    uv = array(sub['MOTV'], 0, len(sub['MOTV']) // 8, '<2f')
    if len(vertices) != len(uv):
        raise ValueError('Lamp vertex/UV mismatch')
    indices = [row[0] for row in array(sub['MOVI'], 0, len(sub['MOVI']) // 2, '<H')]
    batches = array(sub.get('MOBA', b''), 0, len(sub.get('MOBA', b'')) // 24, '<6hI3H2B')
    lights, accepted = [], {}
    for material, texture in sorted(material_names.items()):
        parent, triangles = {}, []
        def find(index):
            parent.setdefault(index, index)
            while parent[index] != index:
                parent[index] = parent[parent[index]]; index = parent[index]
            return index
        for batch in batches:
            if batch[11] != material:
                continue
            first, count = batch[6:8]
            if first + count > len(indices) or count % 3:
                raise ValueError('Invalid lamp batch')
            for offset in range(first, first + count, 3):
                tri = indices[offset:offset+3]
                if max(tri) >= len(vertices) or not all(math.isfinite(x) for i in tri for x in (*vertices[i], *uv[i])):
                    raise ValueError('Invalid lamp geometry')
                for vertex in tri[1:]:parent[find(vertex)] = find(tri[0])
                triangles.append((offset, tri))
                if len(triangles) > 8192:
                    raise ValueError('Lamp triangle limit')
        components, component_triangles = {}, {}
        for index in parent:components.setdefault(find(index), []).append(index)
        for offset, tri in triangles:component_triangles.setdefault(find(tri[0]), []).append((offset, tri))
        faces = []
        for key, ids in components.items():
            tris = component_triangles[key]
            if len(ids) != 4 or len(tris) != 2:
                continue
            center = tuple(sum(vertices[i][k] for i in ids)/4 for k in range(3))
            normals, area = [], 0.
            for _, tri in tris:
                p, q, r = (vertices[i] for i in tri); u = [q[k]-p[k] for k in range(3)]; v = [r[k]-p[k] for k in range(3)]
                n = (u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]); length = math.sqrt(sum(x*x for x in n))
                if length <= 1e-8:break
                normals.append(tuple(x/length for x in n));area += triangle_area(p,q,r)
            if len(normals)!=2 or sum(normals[0][k]*normals[1][k] for k in range(3)) < .95:
                continue
            faces.append((center, normals[0], ids, tris, area))
        groups = []
        for face in sorted(faces):
            cluster = next((g for g in groups if all(math.dist(face[0], other[0]) < 1.3 for other in g)), None)
            if cluster is None:groups.append([face])
            else:cluster.append(face)
        if not groups:
            continue
        width,height,rgba = decode_blp(read_texture(texture),128)
        uv_vertices = [(0.,)*14 + tuple(t) for t in uv]
        fixtures = []
        for cluster in groups:
            if len(cluster) != 4:
                continue
            position = tuple(sum(f[0][k] for f in cluster)/4 for k in range(3))
            bound = max(math.dist(vertices[i],position) for face in cluster for i in face[2])
            if not .1 <= bound <= 1.5:
                continue
            directions = [sum(face[1][k]*(face[0][k]-position[k]) for k in range(3)) for face in cluster]
            if not (all(v > .03 for v in directions) or all(v < -.03 for v in directions)):
                continue
            if any(min(sum(face[1][k]*other[1][k] for k in range(3)) for other in cluster if other is not face) > -.75 for face in cluster):
                continue
            used = uv_texels(uv_vertices, [tri for face in cluster for _,tri in face[3]], width, height)
            if not used:
                continue
            rgb = tuple(sum(srgb_to_linear(rgba[i*4+k]/255.)*(rgba[i*4+3]/255.) for i in used)/len(used) for k in range(3))
            peak = max(rgb);area = sum(face[4] for face in cluster)
            if peak < .02 or not math.isfinite(area) or area <= 0:
                continue
            power = calibrated_intensity(peak*area*.35)
            start = bound*1.05;end = min(24.,max(2.,10.*math.sqrt(power),start+.25))
            fixtures.append((position, tuple(c/peak*power for c in rgb), start, end, cluster))
        for fixture in fixtures:
            if any(other is not fixture and math.dist(fixture[0],other[0]) <= fixture[2]+other[2] for other in fixtures):
                continue
            position,color,start,end,cluster = fixture
            if len(lights) >= 65536:raise ValueError('Lamp source limit')
            identity=0x90000000+(group_index<<16)+len(lights)
            lights.append((position,color,start,end,identity,3,2|GLASS_EMITTER))
            for face in cluster:
                for offset,_ in face[3]:accepted[offset]=identity
    return lights,accepted


def merge_native(native, lamps):
    """Keep native energy/ranges; certify only an unambiguous central MOLT.

    Its fixture-local index ties the retained MOLT to the same eight glass faces.
    Ambiguous or off-center native matches suppress a duplicate fallback source
    but cannot exempt geometry from any shadow cube.
    """
    matches=[]
    for lamp in lamps:
        found=[]
        for i,source in enumerate(native):
            if source[5] != 1:continue
            a,b=lamp[1],source[1];denom=math.sqrt(sum(x*x for x in a)*sum(x*x for x in b))
            if denom and sum(x*y for x,y in zip(a,b))/denom >= .92 and math.dist(lamp[0],source[0]) <= lamp[2] and source[3] >= lamp[3]*.5:
                found.append(i)
        matches.append(found)
    out=list(native)
    for lamp,found in zip(lamps,matches):
        if not found:
            out.append(lamp)
        elif len(found)==1 and sum(found[0] in other for other in matches)==1:
            source=native[found[0]]
            if math.dist(lamp[0],source[0]) <= .12*lamp[2]:
                out[found[0]]=(*source[:4],lamp[4],source[5],source[6]|GLASS_EMITTER)
    return out
