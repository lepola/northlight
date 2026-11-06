#!/usr/bin/env python3
# northlight-test:
"""0.3.216 (issue #43): the world context accepted only [A-Za-z0-9_] as the Map.dbc directory, so Zul'gurub
(Zul'Gurub) and "Stratholme Raid" (Naxxramas) never got a world context. The filter now also accepts an
apostrophe and a space (still nothing else: no path separator, dot or control character), and every
key=value log line that carries a map name quotes it. Source wiring; writes nothing."""
import re, sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
ctx=fp.src('world_context.h').read_text();w=fp.src('world_renderer.h').read_text();lights=fp.src('world_local_lights.h').read_text()
read=ctx[ctx.index('inline bool readMapAndCamera('):];read=read[:read.index('\n}\n')]
allowed=re.search(r"if\(!\((.*)\)\)return false;",read).group(1)
def accepts(c):
    # evaluate the character filter on the C++ source expression
    e=allowed.replace("c=='\\''","c==\"'\"").replace('&&',' and ').replace('||',' or ')
    e=re.sub(r"\(c>='(.)'&&c<='(.)'\)",lambda m:f"('{m[1]}'<=c<='{m[2]}')",e.replace(' and ','&&').replace(' or ','||'))
    e=e.replace('&&',' and ').replace('||',' or ').replace("c=='_'","c=='_'").replace("c==' '","c==' '")
    return bool(eval(e,{'c':c}))
checks={
 'the filter accepts letters, digits, underscore, apostrophe and space':
  all(accepts(c) for c in "Zul'gurub Stratholme Raid_9"),
 'the filter still rejects separators, dots, quotes, percent and control characters':
  not any(accepts(c) for c in '/\\.:*?"<>|%;=[]\n\t\x7f'),
 'the length checks stay (empty and unterminated names are rejected, nothing is published on failure)':
  'if(count==0 || count==sizeof name)return false;' in read and 'for(;count<sizeof name && name[count];++count)' in read,
 'no log line prints a map name unquoted':
  not re.search(r'[mM]ap=%s',w) and w.count('ap=\\"%s\\"')>=16,
 'the tile path stays a plain concatenation (an apostrophe or space needs no escaping in a file path)':
  w.count('"%sworld-cache/%s/%d_%d.fg3"')==2 and 'FindFirstFileW(std::filesystem::u8path(root+"world-cache/"+map+"/*.fg3").c_str(),&data)' in w,
 'local lights accept the same names (<map>.fgl exists for instance maps) and still reject separators':
  "c=='_'||c=='\\''||c==' '))return false;" in lights and 'if(!safeMap(map)' in lights and 'map.empty()||map.size()>63' in lights,
}
for k,v in checks.items():print(('PASS ' if v else 'FAIL ')+k)
assert all(checks.values())
print('PASS instance map name filter: apostrophe and space reach the world context, map names are quoted in logs')
