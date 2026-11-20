// Disassemble one D3D9 shader bytecode file to text via d3dcompiler_47.
// Usage: disassemble_shader.exe input.bin output.asm. No device is created.
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <vector>
int main(int argc,char** argv){
    if(argc!=3&&argc!=4){std::fprintf(stderr,"usage: %s input.bin output.asm [compiler.dll]\n",argv[0]);return 1;}
    FILE* in=std::fopen(argv[1],"rb");if(!in){std::perror(argv[1]);return 2;}
    std::vector<char> data;char buffer[4096];size_t n;while((n=std::fread(buffer,1,sizeof buffer,in))>0)data.insert(data.end(),buffer,buffer+n);std::fclose(in);
    HMODULE module=LoadLibraryA(argc==4?argv[3]:"d3dcompiler_47.dll");
    if(!module){std::fprintf(stderr,"d3dcompiler_47 load failed: %lu\n",(unsigned long)GetLastError());return 3;}
    using Function=HRESULT(WINAPI*)(const void*,SIZE_T,UINT,const char*,ID3DBlob**);
    auto disassemble=reinterpret_cast<Function>(GetProcAddress(module,"D3DDisassemble"));if(!disassemble)return 4;
    ID3DBlob* blob=nullptr;HRESULT hr=disassemble(data.data(),data.size(),0,nullptr,&blob);
    if(FAILED(hr)||!blob){std::fprintf(stderr,"D3DDisassemble failed: 0x%08lx\n",(unsigned long)hr);return 5;}
    FILE* out=std::fopen(argv[2],"wb");if(!out){std::perror(argv[2]);return 6;}
    std::fwrite(blob->GetBufferPointer(),1,blob->GetBufferSize(),out);std::fclose(out);blob->Release();FreeLibrary(module);return 0;
}
