#include "backend_policy.h"
#include <cassert>
#include <cstring>
int main(){using namespace NorthlightBackend;
assert(parse(L"",false)==Kind::Legacy);
assert(parse(L"",true)==Kind::Invalid);
assert(parse(L" NATIVE ",true)==Kind::Native);
assert(parse(L"dxvk",true)==Kind::Dxvk);
assert(parse(L" DXVK2 ",true)==Kind::Dxvk2&&!strcmp(name(Kind::Dxvk2),"dxvk2")&&!strcmp(name(Kind::Dxvk),"dxvk"));
assert(isPackagedDxvk(Kind::Dxvk)&&isPackagedDxvk(Kind::Dxvk2)&&!isPackagedDxvk(Kind::Legacy)&&!isPackagedDxvk(Kind::Native));
assert(parse(L"legacy",true)==Kind::Legacy);
assert(parse(L"../d3d9.dll",true)==Kind::Invalid);
assert(parse(L"dxvkxxxxxxxxxxxxxxxx",true)==Kind::Invalid);
assert(parse(nullptr,true)==Kind::Invalid);
const std::wstring root=L"C:\\Games\\WoW\\",sys=L"C:\\windows\\system32";
// No kind resolves to the game-folder d3d9.dll (the proxy itself).
assert(defaultPath(Kind::Dxvk,root,sys)==L"C:\\Games\\WoW\\renderer-backends\\dxvk\\dxvk_d3d9.dll");
assert(defaultPath(Kind::Dxvk2,root,sys)==L"C:\\Games\\WoW\\renderer-backends\\dxvk2\\dxvk2_d3d9.dll");
assert(baseName(defaultPath(Kind::Dxvk2,root,sys))!=baseName(defaultPath(Kind::Dxvk,root,sys)));
assert(defaultPath(Kind::Legacy,root,sys)==L"C:\\Games\\WoW\\renderer-backends\\legacy\\legacy_d3d9.dll");
assert(defaultPath(Kind::Native,root,sys)==L"C:\\windows\\system32\\d3d9.dll");
assert(defaultPath(Kind::Native,root,L"").empty()&&defaultPath(Kind::Invalid,root,sys).empty());
// Never the proxy itself, and no second module named d3d9.dll except the system runtime.
for(Kind k:{Kind::Dxvk,Kind::Dxvk2,Kind::Legacy,Kind::Native})
    for(const auto& c:candidates(k,L"",root,sys)){assert(!samePath(c,root+L"d3d9.dll"));assert(!d3d9Name(c)||k==Kind::Native);}
assert(d3d9Name(L"x\\D3D9.DLL")&&!d3d9Name(L"x\\dxvk_d3d9.dll")&&!d3d9Name(L"d3d9.dll.bak"));
// Game folder = host exe directory, not the proxy's (macOS: <game>\\mods\\d3d9.dll).
assert(gameRoot(L"C:\\Games\\WoW\\Wow.exe",L"C:\\Games\\WoW\\mods\\d3d9.dll")==root);
assert(gameRoot(L"",L"C:\\Games\\WoW\\MODS\\d3d9.dll")==root);
assert(gameRoot(L"",L"C:\\Games\\WoW\\d3d9.dll")==root&&gameRoot(L"",L"d3d9.dll").empty());
assert(location(L"C:\\Games\\WoW\\d3d9.dll",root)==Location::Root);
assert(location(L"c:/games/wow/Mods/D3D9.dll",root)==Location::Mods);
assert(location(L"C:\\Games\\WoW\\renderer-backends\\d3d9.dll",root)==Location::Other);
assert(directory(L"C:\\a\\b.dll")==L"C:\\a\\"&&directory(L"b.dll").empty());
// dlls.txt entry as WoWSilicon's libDllLdr and Mod Manager write it (any case, separators, CRLF).
assert(dllsListed("mods/winerosetta.dll\nmods/libSiliconPatch.dll\nmods/d3d9.dll\n"));
assert(dllsListed("  MODS\\D3D9.DLL\r\nmods/winerosetta.dll")&&dllsListed("mods/d3d9.dll"));
assert(!dllsListed("mods/winerosetta.dll\nmods/libSiliconPatch.dll\n")&&!dllsListed("")&&!dllsListed("mods/d3d9.dll.disabled\nd3d9.dll"));
assert(candidates(Kind::Legacy,L"",root,sys)==std::vector<std::wstring>{defaultPath(Kind::Legacy,root,sys)});
assert(candidates(Kind::Dxvk,L"",root,sys).size()==1);
assert(candidates(Kind::Dxvk2,L"",root,sys)==std::vector<std::wstring>{defaultPath(Kind::Dxvk2,root,sys)});
assert(candidates(Kind::Native,L"",root,L"").empty());
assert(candidates(Kind::Invalid,L"x.dll",root,sys).empty());
// BackendPath overrides the kind's default path.
assert(overridePath(L"",root).empty()&&overridePath(nullptr,root).empty()&&overridePath(L"   ",root).empty());
assert(overridePath(L" reshade/d3d9.dll ",root)==L"C:\\Games\\WoW\\reshade\\d3d9.dll");
assert(overridePath(L"\"D:\\dxvk x\\d3d9.dll\"",root)==L"D:\\dxvk x\\d3d9.dll");
assert(overridePath(L"d:/x/d3d9.dll",root)==L"d:\\x\\d3d9.dll");
assert(overridePath(L"\\\\server\\share\\d3d9.dll",root)==L"\\\\server\\share\\d3d9.dll");
auto custom=candidates(Kind::Legacy,overridePath(L"mine\\d3d9.dll",root),root,sys);
assert(custom.size()==1&&custom[0]==L"C:\\Games\\WoW\\mine\\d3d9.dll");
// DXVK 3 init marker: beside the dxvk backend, never named d3d9.dll.
// Identity helpers.
assert(samePath(L"C:\\GAMES\\wow\\D3D9.DLL",L"c:/games/WoW/d3d9.dll")&&!samePath(L"",L""));
assert(baseName(L"a/b\\c.dll")==L"c.dll"&&baseName(L"c.dll")==L"c.dll");
}
