#!/usr/bin/env python3
# northlight-test:
"""Issue 106 independent ray-truth checks; optional real GPU-result acceptance.

No graphics device or game is opened. --fixture and --gpu-result validate an
external hidden-device production-bytecode run, not a CPU AO reimplementation.
"""
import argparse
import json
import math
from pathlib import Path
from dataclasses import asdict

from ao_actor_reference import Box, Scene, STRENGTH, reference, scenes, trace, validate_gpu_result
from ao_actor_reference import ray
from ao_actor_fixture_pack import load_fixtures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path)
    parser.add_argument('--gpu-result', type=Path)
    args = parser.parse_args()
    plane = Scene('plane', (0., 1., -3.), (0., 0., 0.), ())
    for radius in (.1, 2., 10.):
        assert reference(plane, (0., 0., 0.), (0., 1., 0.), radius, 512)=={'hard': 0., 'quadratic': 0.}
    wall = Scene('analytic_wall', plane.eye, plane.target,
                 (Box((0., 0., -100.), (100., 100., 100.), label='wall'),))
    a = .25  # receiver-to-wall distance divided by AO radius
    s = math.sqrt(1-a*a)
    expected_hard = STRENGTH*(math.acos(a)-a*s)/math.pi
    i0 = math.pi/4-(a*s+math.asin(a))/2
    i1 = -math.log(a/(1+s))-s
    i2 = s/a+math.asin(a)-math.pi/2
    expected_quadratic = STRENGTH*2/math.pi*(i0-2*a*i1+a*a*i2)
    actual = reference(wall, (-.5, 0., 0.), (0., 1., 0.), 2., 8192)
    assert abs(actual['hard']-expected_hard)<.0015, (actual, expected_hard)
    assert abs(actual['quadratic']-expected_quadratic)<.0015, (actual, expected_quadratic)
    actor = scenes()['close']
    near = reference(actor, (.23, 0., .32), (0., 1., 0.), 2., 4096)['quadratic']
    far = reference(actor, (.23, 0., 2.1), (0., 1., 0.), 2., 4096)['quadratic']
    assert near>.025 and far<.001, (near, far)
    hit = trace(actor, (0., 1.5, -3.), (0., 0., 1.))
    assert hit and hit.actor and hit.label=='torso' and hit.normal==(0., 0., -1.) and abs(hit.t-2.78)<1e-12
    assert set(('close', 'mid', 'far', 'down', 'top', 'up', 'wall', 'cube', 'sky'))<=scenes().keys()
    # The checked-in rays are independently recomputed here; changing textures,
    # geometry or reference values cannot silently bless a screen-space shader.
    for name, fixture in load_fixtures():
        scene = scenes()[fixture['scene']]
        geometry = {key: asdict(scene)[key] for key in ('eye', 'target', 'floor', 'boxes')}
        assert json.loads(json.dumps(geometry)) == fixture['geometry'], name
        w, h = fixture['size']
        aw, ah = fixture['ao_size']
        for roi in ('halo', 'contact', 'wall', 'cube'):
            if roi not in fixture['rois']:
                continue
            region = fixture['rois'][roi]
            assert region['pixels'] >= 100, (name, roi)
            sample = region['samples'][len(region['samples']) // 2]
            x = max(1, min(w - 2, math.floor((sample['x'] + .5) / aw * w + .25)))
            y = max(1, min(h - 2, math.floor((sample['y'] + .5) / ah * h + .25)))
            receiver = trace(scene, scene.eye, ray(scene, x, y, w, h))
            assert receiver is not None
            truth = reference(scene, receiver.point, receiver.normal, fixture['radius'], fixture['rays'])
            assert abs(truth['quadratic'] - sample['quadratic']) < 1e-12, (name, roi)
    # The acceptance layer must reject the original global-gate failure: a
    # wall/cube's real AO disappears even though all output values remain valid.
    fixture = {'scene': 'wall', 'size': [8, 8], 'radius': 2., 'depth_sha256': 'depth', 'actor_sha256': 'owner',
               'rois': {'wall': {'samples': [{'quadratic': actual['quadratic']}], 'pixels': 1}}}
    good = {'method': 'GTAO', **{k: fixture[k] for k in ('scene', 'size', 'radius', 'depth_sha256', 'actor_sha256')},
            'baseline': {'wall': [1-actual['quadratic']]}, 'candidate': {'wall': [1-actual['quadratic']]}}
    assert not validate_gpu_result(fixture, good)
    bad = {**good, 'candidate': {'wall': [1.]}}
    assert validate_gpu_result(fixture, bad), 'Erasing real crease AO must fail acceptance'
    if args.fixture or args.gpu_result:
        assert args.fixture and args.gpu_result, 'Supply both --fixture and --gpu-result'
        errors = validate_gpu_result(json.loads(args.fixture.read_text()), json.loads(args.gpu_result.read_text()))
        assert not errors, errors
        print('PASS production GPU-result reference gates')
    print('PASS independent actor AO ray truth: open plane, analytic wall integrals, finite legs, body hit, control rejection')


if __name__ == '__main__':
    main()
