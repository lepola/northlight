#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Exercise the actual renderer logger with native synchronization/I/O stubs."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parent
OUT = fp.output_dir()
SOURCE = fp.src('renderer.cpp')
text = SOURCE.read_text()
start = text.index("static SRWLOCK logLock")
end = text.index("// CPU wall-time samples", start)
logger = text[start:end]
assert "static void logf(" in logger and "static void reportLogCost()" in logger

PREFIX = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using LONGLONG=long long;
struct LARGE_INTEGER { LONGLONG QuadPart=0; };
struct SRWLOCK { std::mutex value; };
#define SRWLOCK_INIT {}
#define MAX_PATH 260
static std::atomic<bool> failOpen{false},failPrint{false},failNewline{false},failFlush{false},failClock{false};
static std::atomic<unsigned> openCalls{0};
static void AcquireSRWLockExclusive(SRWLOCK* p){p->value.lock();}
static void ReleaseSRWLockExclusive(SRWLOCK* p){p->value.unlock();}
static int QueryPerformanceCounter(LARGE_INTEGER* p){
    if(failClock)return 0;
    p->QuadPart=1+std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();return 1;
}
static int QueryPerformanceFrequency(LARGE_INTEGER* p){p->QuadPart=1000000000;return 1;}
static FILE* _wfopen(const wchar_t*,const wchar_t*){++openCalls;if(failOpen.exchange(false))return nullptr;return std::tmpfile();}
static int fake_vfprintf(FILE* f,const char* format,va_list args){if(failPrint.exchange(false))return -1;return std::vfprintf(f,format,args);}
static int fake_fputc(int c,FILE* f){if(failNewline.exchange(false))return EOF;return std::fputc(c,f);}
static int fake_fflush(FILE* f){if(failFlush.exchange(false))return EOF;return std::fflush(f);}
static wchar_t rootPath[MAX_PATH]={};
static FILE* logFile=nullptr;
static std::atomic<unsigned> rotations{0},openCallsAtRotation{0};
static void rotatePreviousLog(){++rotations;openCallsAtRotation=openCalls.load();}
#define vfprintf fake_vfprintf
#define fputc fake_fputc
#define fflush fake_fflush
'''

SUFFIX = r'''
#undef vfprintf
#undef fputc
#undef fflush
static void reset(){
    if(logFile)std::fclose(logFile);
    logFile=nullptr;logCost={};logFrequency=logIntervalStart=0;openCalls=0;logRotated=false;rotations=0;openCallsAtRotation=0;
    failOpen=failPrint=failNewline=failFlush=failClock=false;
}
static std::string contents(){
    assert(logFile);assert(std::fflush(logFile)==0);assert(std::fseek(logFile,0,SEEK_SET)==0);
    std::string result;char data[2048];size_t n;
    while((n=std::fread(data,1,sizeof data,logFile)))result.append(data,n);
    assert(!std::ferror(logFile));assert(std::fseek(logFile,0,SEEK_END)==0);return result;
}
static void failures(){
    reset();reportLogCost();assert(!logFile&&openCalls==0&&logCost.calls==0);
    failOpen=true;logf("first");assert(!logFile&&logCost.calls==1&&logCost.ioErrors==1&&logCost.formattedBytes==0);
    assert(rotations==1&&openCallsAtRotation==0); // previous log kept before the first open
    logf("recovery %d",7);assert(logFile&&openCalls==2&&logCost.calls==2&&logCost.ioErrors==1);
    assert(rotations==1); // a failed first open never rotates twice
    assert(logCost.formattedBytes==11&&contents()=="recovery 7\n");
    reset();failPrint=failNewline=failFlush=true;logf("discarded");
    assert(logCost.calls==1&&logCost.ioErrors==3&&logCost.formattedBytes==0);
    logf("ok");assert(logCost.calls==2&&logCost.ioErrors==3&&logCost.formattedBytes==3&&contents()=="ok\n");
    reset();failClock=true;logf("clockless");assert(logCost.calls==1&&logCost.wallTicks==0&&logCost.maxTicks==0&&logIntervalStart==0);
    failClock=false;logf("clock recovery");assert(logIntervalStart>0&&logCost.wallTicks>=logCost.maxTicks&&logCost.maxTicks>=0);
    reset();
}
static void stress(){
    constexpr unsigned Threads=6,PerThread=400,Reports=40;
    std::atomic<unsigned> waiting{0};std::atomic<bool> go{false};
    auto barrier=[&](){++waiting;while(!go.load())std::this_thread::yield();};
    std::vector<std::thread> workers;
    logf("seed");
    for(unsigned id=0;id<Threads;++id)workers.emplace_back([&,id]{barrier();for(unsigned i=0;i<PerThread;++i)logf("WORK thread=%u item=%u payload=abcdefghij",id,i);});
    std::thread reporter([&]{barrier();for(unsigned i=0;i<Reports;++i){reportLogCost();std::this_thread::yield();}});
    while(waiting.load()!=Threads+1)std::this_thread::yield();go=true;
    for(auto& worker:workers)worker.join();reporter.join();
    assert(openCalls==1&&rotations==1);const auto pending=logCost;assert(pending.calls>0&&pending.ioErrors==0&&pending.wallTicks>=pending.maxTicks);
    reportLogCost();assert(logCost.calls==1&&logCost.ioErrors==0); // Report line belongs to the next window.
    const auto reportBytes=logCost.formattedBytes;
    reportLogCost();assert(logCost.calls==1&&logCost.ioErrors==0);
    const std::string output=contents();assert(!output.empty()&&output.back()=='\n');
    std::vector<bool> seen(Threads*PerThread);unsigned workLines=0,reportLines=0,seedLines=0;
    unsigned long long accountedCalls=logCost.calls,accountedBytes=logCost.formattedBytes,lastReportedCalls=0,lastReportedBytes=0;
    size_t offset=0;
    while(offset<output.size()){
        const auto newline=output.find('\n',offset);assert(newline!=std::string::npos);
        const std::string line=output.substr(offset,newline-offset);offset=newline+1;
        if(line=="seed"){++seedLines;continue;}
        if(line.rfind("WORK ",0)==0){unsigned id=0,item=0;int consumed=0;
            assert(std::sscanf(line.c_str(),"WORK thread=%u item=%u payload=abcdefghij%n",&id,&item,&consumed)==2);
            assert(size_t(consumed)==line.size()&&id<Threads&&item<PerThread&&!seen[id*PerThread+item]);
            seen[id*PerThread+item]=true;++workLines;continue;
        }
        double interval=0,wall=0,maximum=0;unsigned long long calls=0,bytes=0,errors=0;int consumed=0;
        assert(std::sscanf(line.c_str(),"LOGGER intervalMs=%lf calls=%llu formattedBytes=%llu callerWallMs=%lf maxCallMs=%lf ioErrors=%llu%n",&interval,&calls,&bytes,&wall,&maximum,&errors,&consumed)==6);
        assert(size_t(consumed)==line.size()&&calls>0&&errors==0);
        assert(std::isfinite(interval)&&std::isfinite(wall)&&std::isfinite(maximum)&&interval>=0&&wall>=maximum&&maximum>=0);
        accountedCalls+=calls;accountedBytes+=bytes;lastReportedCalls=calls;lastReportedBytes=bytes;++reportLines;
    }
    assert(seedLines==1&&workLines==Threads*PerThread&&reportLines==Reports+2);
    for(bool item:seen)assert(item);
    assert(accountedCalls==seedLines+workLines+reportLines&&accountedBytes==output.size());
    assert(lastReportedCalls==1&&lastReportedBytes==reportBytes);
    std::cout<<"logger extracted source: six writers, 2400 unique intact work lines, 42 concurrent/final report lines; exact calls/bytes, report self-accounting, lazy open, rotation once before the first open, open/write/newline/flush errors, clock failure and timing invariants passed; total_calls="<<accountedCalls<<" bytes="<<accountedBytes<<'\n';
    reset();
}
int main(){failures();stress();}
'''

OUT.mkdir(exist_ok=True)
fixture = OUT / "logger_fixture.cpp"
fixture.write_text(PREFIX + "\n// BEGIN EXTRACTED RENDERER LOGGER\n" + logger + "\n// END EXTRACTED RENDERER LOGGER\n" + SUFFIX)
report = {
    "scope": "Actual production logger extracted by markers; native mutex/QPC/tmpfile stubs only. No D3D, Wine, or game launch.",
    "source_sha256": {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in [SOURCE, Path(__file__), fixture]},
    "extracted_logger_sha256": hashlib.sha256(logger.encode()).hexdigest(),
    "runs": [],
    "limitations": ["Native mutex and steady-clock substitute for Windows SRWLOCK and QPC; Wine/Windows runtime behavior is not measured.", "I/O failures are injected; successful writes use a real temporary FILE stream.", "ASan/UBSan are memory/undefined-behavior checks, not a thread race detector.", "Timing results test nonnegative accounting, not performance on the game runtime."],
}
for suffix, flags in [("", ["-O2"]), ("_san", ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"])]:
    binary = OUT / ("test_logger_cost" + suffix)
    commands = [["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread", *flags, str(fixture), "-o", str(binary)], [str(binary)]]
    run = {}
    for label, command in zip(["compile", "run"], commands):
        result = subprocess.run(command, text=True, capture_output=True, timeout=60)
        run[label] = {"command": shlex.join(command), "exit_code": result.returncode, "stdout": result.stdout, "stderr": result.stderr}
        if result.returncode:
            report["runs"].append(run)
            (OUT / "logger-validation.json").write_text(json.dumps(report, indent=2) + "\n")
            raise RuntimeError(f"{label} failed: {result.stdout}{result.stderr}")
    report["runs"].append(run)
    print(run["run"]["stdout"], end="")
(OUT / "logger-validation.json").write_text(json.dumps(report, indent=2) + "\n")
print(OUT / "logger-validation.json")
