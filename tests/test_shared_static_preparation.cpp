#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main unused_grouping_main
#include "test_static_shadow_grouping.cpp"
#undef main
#pragma clang diagnostic pop
int main(int argc,char**argv){assert(argc>=2);std::string error;unsigned checks=0;
 for(int input=1;input<argc;++input){NorthlightGI::WorldScene source;assert(NorthlightGI::loadScene(argv[input],source,error));Model frozen,shared;assert(loadCompact(argv[input],"shared",64u<<20,frozen,error));const auto before=source.retainedBytes();assert(packSharedModel(source,"shared",shared,error));assert(before==source.retainedBytes());assert(digest(frozen)==digest(shared));++checks;
  // Metadata and upload-ready bytes must survive release of the source decode.
  const auto value=digest(shared);source={};assert(value==digest(shared));++checks;
  std::printf("SHARED_STATIC input=%s vertices=%zu indices=%zu batches=%zu bytes=%zu digest=%016llx\n",argv[input],shared.vertices.size(),shared.indexCount(),shared.batches.size(),shared.bytes,(unsigned long long)value);
 }
 std::printf("PASS shared static checks=%u\n",checks);
}
