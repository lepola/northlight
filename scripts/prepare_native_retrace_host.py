#!/usr/bin/env python3
"""Prepare a distinctly named private apitrace host; never build or run it.

The original vendor tree/binaries remain unchanged. Exact pinned source bytes
and the Northlight helper inputs are recorded before copying. The parser and
generated D3D9 handlers receive no replay-behavior patches.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM_TREE = '18cfd388ac47a9e00978b34d6c20341f3e2cdb56'
EXCEPTION_OPTIONS = ('    if(MSVC)\n'
                     '        set_source_files_properties(northlight/native_retrace_bridge.cpp PROPERTIES COMPILE_OPTIONS /EHsc)\n'
                     '    else()\n'
                     '        set_source_files_properties(northlight/native_retrace_bridge.cpp PROPERTIES COMPILE_OPTIONS -fexceptions)\n'
                     '    endif()\n')
PINS = {
    'retrace/d3dretrace_main.cpp': 'bdbbfd149815c686fb09fe1e465e6a2c834e39d5ce2a15fa9056b9789534acc0',
    'retrace/CMakeLists.txt': '2fb980ce34df926dfcaef5fb7c07246c118f17b5a9a1c9c4ebd71b6e408c5a93',
    'retrace/retrace_main.cpp': '1fa455124180f60655ac90b3651d66d9a2b7c08fb18c736f31fb36ba9d2531e2',
    'retrace/retrace.cpp': '86ca0213673b72b9327e8e11f66e0504cc645d44c69c617c741901f8ab82eb1a',
    'retrace/retrace_swizzle.cpp': '1d934eef634284e5699e2417e0a90a236e9214e86206bbdf0f8f3879eee7edd0',
    'retrace/retrace.hpp': 'dd61f54b18345397c041f1d0ae66339dc1aa252bd1314e642ee857b738b170d9',
    'lib/trace/trace_parser.cpp': '0464d9cb784e86aa0a7c46058950e58ab2eb9d9f9fe11c35e789d16f10e8d44d',
    'lib/trace/trace_model.hpp': '318de8a0afccbd4114427430680bf86fbd4fe3233616a7b1e300f14c9255cef7',
    'wrappers/trace.py': '5ba796b93c86a9ce35944e4a8fd27b85ff5b1ef1bb1512678687e70fba349a45',
    'specs/d3d9.py': '5377d7aa156b93fe1958bfdeb9e1bba0fda9c229a31187c20d0215b27c10afc6',
}
HELPERS = ('native_retrace_bridge.h', 'native_retrace_bridge.cpp', 'client_image.h',
           'snapshot_archive.h', 'snapshot_store.h', 'client_code_observation.h')


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f'Upstream hook no longer unique: {old!r}')
    return text.replace(old, new)


def identity_header(disc, glare):
    """Reuse runtime reader math; observe terminals and rejected pointer shapes."""
    start = disc.index('inline bool clientPointer(')
    end = disc.index('class IdentityFrame {', start)
    body = disc[start:end]
    body = replace_once(body, '    if(result&&!clientPointer(result,4))return false;',
                        '    read.nativeIdentity(result);\n    if(result&&!clientPointer(result,4))return false;')
    body = replace_once(body, '    if(!clientPointer(handle,0x60))return false;',
                        '    if(!clientPointer(handle,0x60)){read.invalidPointer();return false;}')
    body = replace_once(body, '        if(!clientPointer(variant,0x1c)||!read(std::uintptr_t(variant)+0x18,&gx,4))return false;',
                        '        if(!clientPointer(variant,0x1c)){read.invalidPointer();return false;}\n'
                        '        if(!read(std::uintptr_t(variant)+0x18,&gx,4))return false;')
    body = replace_once(body, '    if(!clientPointer(gx,0x3c)||!read(std::uintptr_t(gx)+0x38,&result,4))return false;',
                        '    if(!clientPointer(gx,0x3c)){read.invalidPointer();return false;}\n'
                        '    if(!read(std::uintptr_t(gx)+0x38,&result,4))return false;')
    start = glare.index('template<class Read>inline NorthlightCelestialDisc::Identities readIdentities(')
    end = glare.index('// Only own a native glare', start)
    return ('#pragma once\n#include <cstddef>\n#include <cstdint>\n#include <cstring>\n'
            '// Source-derived reader math; terminal and invalid-pointer observations only.\n'
            'namespace NorthlightCelestialDisc {\n' + body + '\n}\n'
            'namespace NorthlightCelestialGlare {\n' + glare[start:end] + '\n}\n')


def prepare(args):
    source = args.upstream.resolve()
    target = args.output_source.resolve()
    if target.exists() or target == source or target.is_relative_to(source) or source.is_relative_to(target):
        raise ValueError('Private output must be new and outside the unchanged vendor tree')
    inputs = {source / name: expected for name, expected in PINS.items()}
    for path, expected in inputs.items():
        if digest(path) != expected:
            raise ValueError(f'Pinned upstream source differs: {path}')
    helpers = {ROOT / 'src/stream' / name: digest(ROOT / 'src/stream' / name) for name in HELPERS}
    inputs.update(helpers)
    fixture = ROOT / 'tests/test_native_retrace_aliases.cpp'
    inputs[fixture] = digest(fixture)
    sky = [ROOT / 'src/sky' / name for name in ('celestial_disc_native.h', 'celestial_glare_native.h')]
    inputs.update({p: digest(p) for p in sky})
    identities = identity_header(*(p.read_text(encoding='utf-8') for p in sky))
    inputs[Path(__file__).resolve()] = digest(Path(__file__).resolve())
    host = (source / 'retrace/d3dretrace_main.cpp').read_text(encoding='utf-8')
    common = (source / 'retrace/retrace_main.cpp').read_text(encoding='utf-8')
    cmake = (source / 'retrace/CMakeLists.txt').read_text(encoding='utf-8')
    host = replace_once(host, '#include "d3dretrace.hpp"',
                        '#include "d3dretrace.hpp"\n#include "northlight/native_retrace_bridge.h"')
    host = replace_once(host, '    retracer.addCallbacks(d3dretrace::dxgi_callbacks);\n#endif\n}',
                        '    retracer.addCallbacks(d3dretrace::dxgi_callbacks);\n#endif\n'
                        '    NorthlightNativeRetraceBridge::install(retracer);\n}')
    host = replace_once(host, 'retrace::cleanUp(void) {\n}',
                        'retrace::cleanUp(void) {\n    NorthlightNativeRetraceBridge::finish();\n}')
    common = replace_once(common, '#include "retrace.hpp"',
                          '#include "retrace.hpp"\n#ifdef NORTHLIGHT_NATIVE_RETRACE_BRIDGE\n'
                          '#include "northlight/native_retrace_bridge.h"\n#endif')
    common = replace_once(common, '            if (!parser->open(argv[i])) {',
                          '#ifdef NORTHLIGHT_NATIVE_RETRACE_BRIDGE\n'
                          '            if (!NorthlightNativeRetraceBridge::sourceTrace(argv[i],\n'
                          '                    loopCount != 0 || retrace::ignoreCalls || retrace::numPasses != 1)) return 1;\n'
                          '#endif\n            if (!parser->open(argv[i])) {')
    cmake = replace_once(cmake, '    install (TARGETS d3dretrace RUNTIME DESTINATION bin)',
                         '    target_sources(d3dretrace PRIVATE northlight/native_retrace_bridge.cpp)\n'
                         '    target_include_directories(d3dretrace PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/northlight")\n'
                         '    target_compile_features(d3dretrace PRIVATE cxx_std_17)\n'
                         + EXCEPTION_OPTIONS +
                         '    target_compile_definitions(retrace_common PRIVATE NORTHLIGHT_NATIVE_RETRACE_BRIDGE=1)\n'
                         '    set_target_properties(d3dretrace PROPERTIES OUTPUT_NAME northlight-native-retrace)\n'
                         '    add_executable(northlight-native-alias-fixture northlight/test_native_retrace_aliases.cpp retrace.cpp retrace_swizzle.cpp)\n'
                         '    target_include_directories(northlight-native-alias-fixture PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/northlight")\n'
                         '    target_compile_features(northlight-native-alias-fixture PRIVATE cxx_std_17)\n'
                         '    target_link_libraries(northlight-native-alias-fixture common image getopt dxerr winmm psapi advapi32)\n'
                         '    add_dependencies(northlight-native-alias-fixture version)\n'
                         + EXCEPTION_OPTIONS.replace('northlight/native_retrace_bridge.cpp', 'northlight/test_native_retrace_aliases.cpp') +
                         '    install (TARGETS d3dretrace RUNTIME DESTINATION bin)')
    shutil.copytree(source, target, ignore=shutil.ignore_patterns('.git', '__pycache__'))
    for name, value in (('retrace/d3dretrace_main.cpp', host), ('retrace/retrace_main.cpp', common),
                        ('retrace/CMakeLists.txt', cmake)):
        (target / name).write_text(value, encoding='utf-8', newline='\n')
    helper_target = target / 'retrace/northlight'
    helper_target.mkdir()
    for path in helpers:
        shutil.copyfile(path, helper_target / path.name)
        if digest(helper_target / path.name) != helpers[path]:
            raise ValueError('Copied helper differs from its frozen source')
    shutil.copyfile(fixture, helper_target / fixture.name)
    if digest(helper_target / fixture.name) != inputs[fixture]:
        raise ValueError('Copied alias fixture differs from its frozen source')
    (helper_target / 'native_retrace_identity.h').write_text(identities, encoding='utf-8', newline='\n')
    for path, expected in inputs.items():
        if digest(path) != expected:
            raise ValueError(f'Input changed during preparation: {path}')
    for name, expected in PINS.items():
        if name not in ('retrace/d3dretrace_main.cpp', 'retrace/retrace_main.cpp', 'retrace/CMakeLists.txt'):
            if digest(target / name) != expected:
                raise ValueError(f'Unchanged official parser/handler input differs: {name}')
    manifest = {
        'format': 1, 'upstream_ref': '14.0', 'expected_upstream_tree': UPSTREAM_TREE,
        'upstream_identity_scope': 'Pinned hook/parser/generator input blobs; not a whole-tree assertion',
        'vendor_source': str(source), 'private_source': str(target),
        'inputs': {str(path): sha for path, sha in inputs.items()}, 'inputs_unchanged': True,
        'patched_files': {name: digest(target / name) for name in
                          ('retrace/d3dretrace_main.cpp', 'retrace/retrace_main.cpp', 'retrace/CMakeLists.txt')},
        'output_name': 'northlight-native-retrace.exe', 'plan_environment': 'NORTHLIGHT_RETRACE_PLAN',
        'generated_identity_header_sha256': digest(helper_target / 'native_retrace_identity.h'),
        'alias_fixture_sha256': digest(helper_target / fixture.name),
        'alias_fixture_target': 'northlight-native-alias-fixture',
        'built': False, 'executed': False, 'game_launched': False, 'vendor_modified': False,
        'aliases_complete': False, 'full_scene_replay_ready': False, 'fps_claimed': False,
        'planned_build': ['cmake', '--build', '<new-private-x86-build>', '--target', 'd3dretrace'],
    }
    (target / 'northlight-host-preparation.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return manifest


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--upstream', required=True, type=Path)
    parser.add_argument('--output-source', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(prepare(args), indent=2))


if __name__ == '__main__':
    main()
