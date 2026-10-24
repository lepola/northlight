#!/usr/bin/env python3
# northlight-test: requires=cxx slow
"""Exact frozen terrain coverage oracle, borders/extremes and a wrong-cell control."""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

BASELINE = '9be300a0147c0b8fc8cc2eefbfb4598dc5d26bab'
# Verbatim frozen function, renamed and wrapped in its namespace. The automatic
# test needs only source files and Clang, including in shallow/archive checkouts.
REFERENCE_SHA256 = 'ac82466ea984618cda10c296d34e4b9beffb816fb049560031795d655b1bd68d'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def headers(out):
    tree = ast.parse((fp.TESTS / 'test_terrain_snapshot.py').read_text())
    fixture = {node.targets[0].id: ast.literal_eval(node.value) for node in tree.body
               if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name)
               and node.targets[0].id in ('stub', 'harness')}
    (out / 'd3d9.h').write_bytes(fixture['stub'].encode())
    (out / 'rotation_device.h').write_bytes(fixture['harness'].split('int main(){')[0].encode())
    reference = (fp.FIXTURES / 'terrain_classification_reference.h').read_bytes()
    assert hashlib.sha256(reference).hexdigest() == REFERENCE_SHA256, 'Frozen terrain oracle changed'
    (out / 'terrain_reference.h').write_bytes(reference)


EDGE = r'''
#include "rotation_device.h"
#include "terrain_reference.h"
#include <cfloat>
#include <cstdio>
static unsigned checks=0,accepted=0,rejected=0;
static void equal(bool a,bool b,const Bounds& x,const Bounds& y){
    assert(a==b&&"TERRAIN_COVERAGE_MISMATCH");
    assert(!std::memcmp(x.low,y.low,sizeof x.low)&&!std::memcmp(x.high,y.high,sizeof x.high)&&"TERRAIN_COVERAGE_MISMATCH");
    assert(x.chunks.size()==y.chunks.size()&&"TERRAIN_COVERAGE_MISMATCH");
    for(size_t i=0;i<x.chunks.size();++i){const auto& p=x.chunks[i];const auto& q=y.chunks[i];
        assert(p.x==q.x&&p.y==q.y&&p.minX==q.minX&&p.maxX==q.maxX&&p.minY==q.minY&&p.maxY==q.maxY&&"TERRAIN_COVERAGE_MISMATCH");}
}
static void check(const std::vector<Position>& p,D3DPRIMITIVETYPE topology=D3DPT_TRIANGLELIST){
    Bounds reference,current;const auto at=[&](size_t i){return p[i];};
    const bool a=referenceTriangleSequence(at,p.size(),topology,reference),b=classifyTriangleSequence(at,p.size(),topology,current);
    equal(a,b,reference,current);++checks;a?++accepted:++rejected;
    bool finite=true;for(auto v:p)finite=finite&&std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
    if(finite){const bool c=referenceTriangleSequence<true>(at,p.size(),topology,reference),d=classifyTriangleSequence<true>(at,p.size(),topology,current);equal(c,d,reference,current);++checks;}
}
static void append(std::vector<Position>& p,float x,float y){p.push_back({x-.005f,y-.005f,1});p.push_back({x+.005f,y-.005f,2});p.push_back({x,y+.01f,3});}
int main(){
    check({});check({{0,0,0},{0,0,0}});check({{0,0,0},{0,0,0},{0,0,0}});
    std::vector<Position> changed;append(changed,-2,-2);append(changed,.01f,-2);check(changed); // The deliberately wrong-cell build must fail here.
    // Exercise both the inline set and its hash fallback, including revisits.
    for(unsigned cells:{1u,8u,9u,32u}){std::vector<Position> p;
        for(unsigned pass=0;pass<3;++pass)for(unsigned i=0;i<cells;++i){const unsigned cell=pass==1?cells-1-i:i;append(p,-2.f-float(cell)*100.f,-2.f);}check(p);}
    for(int gx:{0,1,511,512,513,1023})for(int gy:{0,1,511,512,513,1023}){
        const double origin=17066.666666666666,step=100.0/3.0;
        const float loX=float(origin-(gy+1)*step),hiX=float(origin-gy*step),loY=float(origin-(gx+1)*step),hiY=float(origin-gx*step);
        for(float x:{loX,hiX,loX+.03f,hiX-.03f})for(float direction:{-INFINITY,INFINITY}){
            for(float candidate:{x,std::nextafter(x,direction)}){std::vector<Position> p;append(p,(loX+hiX)*.5f,(loY+hiY)*.5f);append(p,candidate,(loY+hiY)*.5f);check(p);}}
        for(float y:{loY,hiY,loY+.03f,hiY-.03f})for(float direction:{-INFINITY,INFINITY}){
            for(float candidate:{y,std::nextafter(y,direction)}){std::vector<Position> p;append(p,(loX+hiX)*.5f,(loY+hiY)*.5f);append(p,(loX+hiX)*.5f,candidate);check(p);}}
    }
    for(float t:{.03f,std::nextafter(.03f,INFINITY),std::nextafter(.03f,-INFINITY),-.03f}){
        std::vector<Position> p;append(p,-2,-2);p.push_back({-.2f,-2,1});p.push_back({t,-2,2});p.push_back({-.2f,-1.9f,3});check(p);}
    for(float bad:{NAN,INFINITY,-INFINITY,FLT_MAX,-FLT_MAX})for(unsigned axis=0;axis<3;++axis){
        std::vector<Position> p;append(p,-2,-2);append(p,-3,-3);if(axis==0)p.back().x=bad;else if(axis==1)p.back().y=bad;else p.back().z=bad;check(p);}
    std::vector<Position> strip={{-2,-2,1},{-1,-2,2},{-2,-1,3},{-2,-1,3},{-1,-1,4},{-1,-2,5}};check(strip,D3DPT_TRIANGLESTRIP);
    for(unsigned i=0;i<1000;++i){std::vector<Position> p;append(p,-2,-2);append(p,-2-float(i%20)*33.333333f,-2-float(i%7)*33.333333f);append(p,-2,-2);check(p);}
    std::printf("PASS exact frozen coverage checks=%u accepted=%u rejected=%u, list/strip/degenerate/disjoint/grid/interior/tolerance/NaN/extreme/separate-call controls\n",checks,accepted,rejected);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='terrain-classify-') as tmp:
        out = args.output or Path(tmp)
        assert not out.exists() or not any(out.iterdir()), 'Use a new/empty evidence directory'
        out.mkdir(parents=True, exist_ok=True)
        watched = [Path(__file__), fp.REPO / 'northlight_paths.py', fp.TESTS / 'test_terrain_snapshot.py',
                   fp.FIXTURES / 'terrain_classification_reference.h', fp.src('terrain_capture_bounds.h')]
        inputs_before = {str(path.resolve()): sha(path) for path in watched}
        headers(out)
        (out / 'edge.cpp').write_bytes(EDGE.encode())
        before = sha(fp.src('terrain_capture_bounds.h'))
        commands, results = [], []
        for label, flags in [('O2', ['-O2']), ('O0', ['-O0']), ('asan', ['-O1', '-g', '-fsanitize=address,undefined'])]:
            exe = out / label
            command = ['clang++', '-std=c++17', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *flags,
                       '-I', str(out), *fp.test_include_flags(), str(out / 'edge.cpp'), '-o', str(exe)]
            commands.append(command);subprocess.run(command, check=True)
            text = subprocess.check_output([str(exe)], text=True);(out / (label + '.log')).write_text(text)
            print(label, text, end='', flush=True);results.append({'label': label, 'sha256': sha(exe), 'pass': True})
        mutant = out / 'negative';mutant.mkdir()
        source = fp.src('terrain_capture_bounds.h').read_text()
        begin = source.index('        const bool reusePrevious=')
        end = source.index('        if(reusePrevious)', begin)
        source = source[:begin] + '        const bool reusePrevious=previousChunk!=std::numeric_limits<unsigned>::max();\n' + source[end:]
        (mutant / 'terrain_capture_bounds.h').write_bytes(source.encode())
        command = ['clang++', '-std=c++17', '-O2', '-UNDEBUG', '-I', str(mutant), '-I', str(out),
                   *fp.test_include_flags(), str(out / 'edge.cpp'), '-o', str(mutant / 'wrong')]
        commands.append(command);subprocess.run(command, check=True)
        result = subprocess.run([str(mutant / 'wrong')], capture_output=True, text=True)
        (out / 'negative.log').write_text(result.stdout + result.stderr)
        assert result.returncode != 0 and 'TERRAIN_COVERAGE_MISMATCH' in result.stderr, result
        assert sha(fp.src('terrain_capture_bounds.h')) == before, 'Tested source changed'
        assert all(sha(Path(path)) == value for path, value in inputs_before.items()), 'Test helper input changed'
        (out / 'validation.json').write_text(json.dumps({'source_sha256': before, 'baseline': BASELINE, 'commands': commands,
            'compiler_version': subprocess.check_output(['clang++', '--version'], text=True), 'runs': results,
            'inputs_before': inputs_before, 'helper_inputs_unchanged': True, 'frozen_oracle_sha256': REFERENCE_SHA256,
            'wrong_cell_control_failed_as_expected': True, 'game_launched': False}, indent=2) + '\n')
        print('PASS wrong-cell negative control fails the independent coverage oracle')


if __name__ == '__main__':
    main()
