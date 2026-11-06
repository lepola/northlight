#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Exercise the actual logger's flush boundary, output, errors and thread isolation."""
from pathlib import Path
import subprocess, sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.out()/'frame-logging-batch';out.mkdir(exist_ok=True)
source=fp.src('renderer.cpp').read_text()
begin=source.index('static SRWLOCK logLock=')
end=source.index('static void reportLogCost()',begin)
(out/'logger.inc').write_text(source[begin:end])
cpp=r'''
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <thread>
#include <cassert>
#include <io.h>
#include <fcntl.h>
static FILE* logFile=nullptr;
static wchar_t rootPath[MAX_PATH]={};
static void rotatePreviousLog(){}
#include "logger.inc"
static std::string persisted(){
    const HANDLE h=reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(logFile)));
    LARGE_INTEGER size{},zero{};assert(GetFileSizeEx(h,&size)&&size.QuadPart<1000000);
    assert(SetFilePointerEx(h,zero,nullptr,FILE_BEGIN));std::string data(size_t(size.QuadPart),'\0');DWORD got=0;
    assert(ReadFile(h,data.data(),DWORD(data.size()),&got,nullptr)&&got==data.size());
    assert(std::fseek(logFile,0,SEEK_END)==0);return data;
}
int main(int argc,char** argv){assert(argc==2);
    for(bool text:{false,true}){
        logFile=std::tmpfile();assert(logFile);assert(_setmode(_fileno(logFile),text?_O_TEXT:_O_BINARY)!=-1);
        logCost={};const auto lines=threadLogLines;std::string expected;
        const std::string nl=text?"\r\n":"\n";
        {LogFlushBatch empty;}assert(!logFlushBatchDepth&&!logFlushPending&&!logCost.calls);
        try{LogFlushBatch batch;logf("value=%d rgba=%.2f",3,1.25);expected+="value=3 rgba=1.25"+nl;
            {LogFlushBatch nested;logf("%s","percent %n %p %s");expected+="percent %n %p %s"+nl;}
            logf("%s",static_cast<const char*>(nullptr));expected+="(null)"+nl;
            const std::string longLine(32768,'x');logf("%s",longLine.c_str());expected+=longLine+nl;
            throw 1;
        }catch(int){}
        assert(!logFlushBatchDepth&&!logFlushPending&&persisted()==expected&&logCost.calls==4&&!logCost.ioErrors&&threadLogLines==lines+4);
        const auto bytesBefore=logCost.formattedBytes;std::thread threads[3];
        for(unsigned t=0;t<3;++t)threads[t]=std::thread([t]{for(unsigned i=0;i<100;++i){LogFlushBatch batch;logf("thread-%u-%u",t,i);}assert(!logFlushBatchDepth&&!logFlushPending);});
        for(auto& t:threads)t.join();auto data=persisted();size_t expectedBytes=bytesBefore;
        for(unsigned t=0;t<3;++t)for(unsigned i=0;i<100;++i){auto line="thread-"+std::to_string(t)+"-"+std::to_string(i);assert(data.find(line+nl)!=std::string::npos);expectedBytes+=line.size()+1;}
        assert(logCost.calls==304&&logCost.formattedBytes==expectedBytes&&!logCost.ioErrors);std::fclose(logFile);
    }
    logFile=std::fopen(argv[1],"rb");assert(logFile);const auto errors=logCost.ioErrors;
    {LogFlushBatch batch;logf("%s","write refused");}assert(logCost.ioErrors>errors);std::fclose(logFile);logFile=nullptr;
    std::puts("FRAME_LOG_BATCH_PASS binary_text exact_output long_null_percent nested_exception concurrent_thread_flush io_errors");
}
'''
(out/'test.cpp').write_text(cpp)
sentinel=out/'read-only-input.txt';sentinel.write_bytes(b'unchanged')
for opt in ('-O0','-O2'):
    exe=out/(opt[1:]+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu',opt,'-UNDEBUG','-std=c++17',str(out/'test.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
    subprocess.run([str(exe),str(sentinel)],check=True)
assert sentinel.read_bytes()==b'unchanged'
