#!/usr/bin/env python3
"""Independent analytic geometry and ray truth for actor AO GPU regressions.

Stdlib only. This module deliberately contains no screen-space AO algorithm.
Exported R32F depth/owner textures and sampled reference receivers can be fed
to the production AO bytecode plus AOBlur on an independent hidden device.
"""
from __future__ import annotations

from array import array
from dataclasses import dataclass
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

NEAR, FAR, PART, STRENGTH = .2, 1000., .94, .6
SX, SY = 1.2686, 1.9626


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x-y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def unit(a):
    scale = math.sqrt(dot(a, a))
    return tuple(x/scale for x in a)


@dataclass(frozen=True)
class Box:
    lo: tuple
    hi: tuple
    actor: bool = False
    label: str = 'box'


@dataclass(frozen=True)
class Scene:
    name: str
    eye: tuple
    target: tuple
    boxes: tuple
    floor: float | None = 0.


@dataclass(frozen=True)
class Hit:
    t: float
    point: tuple
    normal: tuple
    actor: bool
    label: str


def trace(scene, origin, direction, limit=math.inf):
    """Nearest positive analytic plane/closed-AABB intersection."""
    nearest, result = limit, None
    if scene.floor is not None and direction[1] < -1e-12:
        t = (scene.floor-origin[1])/direction[1]
        if 1e-6 < t < nearest:
            nearest = t
            result = Hit(t, tuple(a+t*b for a, b in zip(origin, direction)), (0., 1., 0.), False, 'ground')
    for box in scene.boxes:
        enter, leave, entry_axis, exit_axis = -math.inf, math.inf, 0, 0
        for axis in range(3):
            if abs(direction[axis]) < 1e-12:
                if not box.lo[axis] <= origin[axis] <= box.hi[axis]:
                    enter, leave = math.inf, -math.inf
                    break
                continue
            a, b = (box.lo[axis]-origin[axis])/direction[axis], (box.hi[axis]-origin[axis])/direction[axis]
            a, b = min(a, b), max(a, b)
            if a > enter:
                enter, entry_axis = a, axis
            if b < leave:
                leave, exit_axis = b, axis
        t, axis = (enter, entry_axis) if enter > 1e-6 else (leave, exit_axis)
        if leave >= max(enter, 1e-6) and 1e-6 < t < nearest:
            normal = [0., 0., 0.]
            normal[axis] = (-1. if direction[axis] > 0 else 1.) * (1. if enter > 1e-6 else -1.)
            nearest = t
            result = Hit(t, tuple(a+t*b for a, b in zip(origin, direction)), tuple(normal), box.actor, box.label)
    return result


def actor_boxes():
    return tuple([Box((x-.12, .12, -.12), (x+.12, 1.12, .12), True, 'shin') for x in (-.23, .23)] +
                 [Box((x-.14, 0., -.27), (x+.14, .17, .18), True, 'foot') for x in (-.23, .23)] +
                 [Box((-.48, 1.08, -.22), (.48, 1.94, .22), True, 'torso'),
                  Box((-.22, 1.94, -.22), (.22, 2.36, .22), True, 'head')])


def scenes():
    poses = {'close': ((0., 1.65, -3.), (0., .7, 0.)), 'mid': ((0., 3., -7.), (0., .7, 0.)),
             'far': ((0., 6., -18.), (0., .7, 0.)), 'down': ((0., 4., -3.), (0., .5, 0.)),
             'top': ((0., 7., -.8), (0., .4, 0.)), 'up': ((0., .35, -3.), (0., 1.4, 0.))}
    result = {name: Scene(name, *pose, actor_boxes()) for name, pose in poses.items()}
    result['sky'] = Scene('sky', *poses['up'], actor_boxes(), None)
    result['wall'] = Scene('wall', (0., 2., -7.), (0., .6, .5),
                           (Box((-3., 0., .8), (3., 4., 1.4), label='wall'),))
    result['cube'] = Scene('cube', (0., 2., -5.), (0., .5, 0.),
                           (Box((-.7, 0., -.7), (.7, 2.5, .7), True, 'cube'),))
    return result


def ray(scene, x, y, width, height):
    forward = unit(sub(scene.target, scene.eye))
    right = unit(cross((0., 1., 0.), forward))
    up = cross(forward, right)
    vx, vy = (2*(x+.5)/width-1)/SX, (1-2*(y+.5)/height)/SY
    return tuple(vx*right[k]+vy*up[k]+forward[k] for k in range(3))


def radical_inverse(bits):
    value, fraction = 0., .5
    while bits:
        value += (bits & 1)*fraction
        bits >>= 1
        fraction *= .5
    return value


def reference(scene, point, normal, radius, rays=2048):
    """Cosine hemisphere visibility; finite hard and quadratic distance kernels."""
    tangent = unit(cross(normal, (0., 1., 0.) if abs(normal[1]) < .9 else (1., 0., 0.)))
    bitangent = cross(normal, tangent)
    origin = tuple(p+n*1e-4 for p, n in zip(point, normal))
    hard = quadratic = 0.
    for k in range(rays):
        r, phi = math.sqrt((k+.5)/rays), 2*math.pi*radical_inverse(k)
        local = r*math.cos(phi), r*math.sin(phi), math.sqrt(1-r*r)
        direction = tuple(tangent[i]*local[0]+bitangent[i]*local[1]+normal[i]*local[2] for i in range(3))
        hit = trace(scene, origin, direction, radius)
        if hit:
            hard += 1.
            quadratic += (1-hit.t/radius)**2
    return {'hard': STRENGTH*hard/rays, 'quadratic': STRENGTH*quadratic/rays}


def roi_names(scene, hit):
    if hit is None:
        return ('sky',)
    if hit.actor:
        return ('body',)
    x, y, z = hit.point
    if scene.name == 'wall':
        return ('wall',) if abs(x)<2.2 and ((hit.label=='wall' and .03<y<.7) or (hit.label=='ground' and -.1<z<.75)) else ()
    if scene.name == 'cube':
        return ('cube',) if abs(x)<1.2 and .72<z<2. else ()
    if hit.label != 'ground':
        return ()
    result = []
    if abs(x)<.8 and .3<=z<=1.5:
        result.append('halo')
    distance = min(math.hypot(max(abs(x-fx)-.14, 0.), max(-.27-z, z-.18, 0.)) for fx in (-.23, .23))
    if .005<distance<=.18:
        result.append('contact')
    return tuple(result)


def export_fixture(scene, width, height, radius, output, rays=4096, samples=96):
    """Write full-res D24-in-R32F depth/owner textures and sampled ray truth."""
    if width<4 or height<4 or not .1<=radius<=10 or rays<64 or samples<1:
        raise ValueError('Invalid fixture dimensions, radius or reference sample count')
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    depth, owner = array('f'), array('f')
    span = (1 << 24)-1
    for y in range(height):
        for x in range(width):
            hit = trace(scene, scene.eye, ray(scene, x, y, width, height))
            z = hit.t if hit and NEAR<hit.t<FAR else FAR
            depth.append(round(FAR/(FAR-NEAR)*(1-NEAR/z)*PART*span)/span)
            owner.append(float(bool(hit and hit.actor and z<FAR)))
    groups = {}
    aw, ah = width//2, height//2
    for j in range(ah):
        for i in range(aw):
            x = max(1, min(width-2, math.floor((i+.5)/aw*width+.25)))
            y = max(1, min(height-2, math.floor((j+.5)/ah*height+.25)))
            hit = trace(scene, scene.eye, ray(scene, x, y, width, height))
            if hit and not NEAR<hit.t<FAR:
                hit = None
            for roi in roi_names(scene, hit):
                groups.setdefault(roi, []).append((i, j, hit))
    selected = {}
    for name, points in groups.items():
        count = min(samples, len(points))
        indices = [round(k*(len(points)-1)/max(count-1, 1)) for k in range(count)]
        selected[name] = {'pixels': len(points), 'samples': []}
        for index in indices:
            i, j, hit = points[index]
            truth = reference(scene, hit.point, hit.normal, radius, rays) if hit else {'hard': 0., 'quadratic': 0.}
            selected[name]['samples'].append({'x': i, 'y': j, **truth})
    if sys.byteorder != 'little':
        depth.byteswap()
        owner.byteswap()
    depth_bytes, owner_bytes = depth.tobytes(), owner.tobytes()
    dp, mp = output.with_suffix('.depth.f32'), output.with_suffix('.actor.f32')
    dp.write_bytes(depth_bytes)
    mp.write_bytes(owner_bytes)
    result = {'schema': 'ao_actor_reference_v1', 'scene': scene.name, 'size': [width, height], 'ao_size': [aw, ah],
              'geometry': {'eye': scene.eye, 'target': scene.target, 'floor': scene.floor,
                           'boxes': [{'lo': box.lo, 'hi': box.hi, 'actor': box.actor, 'label': box.label} for box in scene.boxes]},
              'near': NEAR, 'far': FAR, 'partition': PART, 'projection': [SX, SY], 'strength': STRENGTH,
              'radius': radius, 'rays': rays, 'depth_file': dp.name, 'actor_file': mp.name,
              'depth_sha256': hashlib.sha256(depth_bytes).hexdigest(), 'actor_sha256': hashlib.sha256(owner_bytes).hexdigest(),
              'rois': selected}
    output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    return result


def validate_gpu_result(fixture, report):
    """Validate AOBlur output samples from real baseline/candidate GPU bytecode.

    Report: method, scene, size, radius, depth_sha256, actor_sha256, plus baseline
    and candidate dictionaries of ROI -> alpha arrays in fixture sample order.
    These are fixed quality limits, not promises that a proposed shader passes.
    """
    errors = []
    for key in ('scene', 'size', 'radius', 'depth_sha256', 'actor_sha256'):
        if report.get(key) != fixture[key]:
            errors.append('fixture mismatch: '+key)
    if report.get('method') not in ('GTAO', 'HBAO+'):
        errors.append('unsupported method')
    required = ('wall',) if fixture['scene']=='wall' else ('cube', 'body') if fixture['scene']=='cube' else ('sky', 'body') if fixture['scene']=='sky' else ('halo', 'contact', 'body')
    for name in required:
        if name not in fixture['rois']:
            errors.append(name+': missing critical ROI')
    for name, roi in fixture['rois'].items():
        if name in ('halo', 'contact') and roi['pixels']<100:
            errors.append(name+': fewer than 100 visible pixels; export at a higher resolution')
        baseline, candidate = report.get('baseline', {}).get(name, []), report.get('candidate', {}).get(name, [])
        if len(baseline)!=len(roi['samples']) or len(candidate)!=len(roi['samples']):
            errors.append(name+': missing samples')
            continue
        if not all(math.isfinite(v) and 0<=v<=1 for v in baseline+candidate):
            errors.append(name+': invalid alpha')
            continue
        if not candidate:
            continue
        truth = [p['quadratic'] for p in roi['samples']]
        base_error = sum(abs(1-a-t) for a, t in zip(baseline, truth))/len(truth)
        new_error = sum(abs(1-a-t) for a, t in zip(candidate, truth))/len(truth)
        mean_delta = sum(abs(a-b) for a, b in zip(candidate, baseline))/len(truth)
        if name=='sky' and any(abs(a-1)>1e-6 for a in candidate):
            errors.append('sky: AO identity lost')
        if name in ('body', 'wall', 'cube') and mean_delta>.002:
            errors.append(name+': baseline control changed')
        if name!='sky' and new_error>base_error+.005:
            errors.append(name+': ray MAE regressed')
        if name=='halo' and fixture.get('expect_halo_fix', True) and fixture['radius']>=1 and base_error>.015:
            if new_error>base_error*.4+.005:
                errors.append('halo: insufficient reference-error reduction')
            bias = abs(sum(1-a-t for a, t in zip(candidate, truth))/len(truth))
            if fixture['radius']==2 and bias>.015:
                errors.append('halo: radius-2 reference bias exceeds .015')
    return errors


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene', choices=scenes(), required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--width', type=int, default=640)
    parser.add_argument('--height', type=int, default=400)
    parser.add_argument('--radius', type=float, default=2.)
    parser.add_argument('--rays', type=int, default=4096)
    parser.add_argument('--samples', type=int, default=96)
    args = parser.parse_args()
    result = export_fixture(scenes()[args.scene], args.width, args.height, args.radius, args.output, args.rays, args.samples)
    print(json.dumps({'output': str(args.output), 'receivers': {name: item['pixels'] for name, item in result['rois'].items()}}))
