#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Run the production pending-query loop without a game or GPU."""
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp


def method(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


replay = fp.src('replay_thread.h').read_text()
proxies = fp.src('stream_proxies.h').read_text()
poll = method(replay, 'void pollQueries()')
terminal = method(proxies, 'void completeEvent(')
write = method(proxies, 'void writeResult(')
issue_start = replay.index('if((a->dwIssueFlags&D3::kIssueEnd)&&!q->dead.load())')
issue = method(replay[issue_start:], 'if((a->dwIssueFlags&D3::kIssueEnd)&&!q->dead.load())')
report = method(proxies, 'void reportQueryFrame(')
reset = fp.src('stream_device.h').read_text()
assert 'c.queryEpoch.fetch_add(1); // invalidate EVENT cache across Reset' in reset
# Frozen original loop is a negative control, not a second implementation to ship.
baseline = '''void pollQueries(){
    for(std::size_t i=0;i<pending_.size();){
        StreamQuery* q=pending_[i];bool done=false;
        if(q->dead.load()||!q->inner)done=true;
        else{
            unsigned char buf[64]={};add(core.q.stats.queryPolls);
            const HRESULT hr=static_cast<IDirect3DQuery9*>(q->inner)->GetData(buf,q->dataSize,0);
            if(q->type==8)q->completeEvent(hr,buf);
            if(hr==S_OK){q->writeResult(buf,q->dataSize);q->readyGen.store(q->issueSeen,std::memory_order_release);done=true;}
            else if(q->type==8&&FAILED(hr))done=true;
        }
        if(done){pending_[i]=pending_.back();pending_.pop_back();}else ++i;
    }
}'''
prefix = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
using HRESULT=std::int32_t;using BOOL=std::int32_t;using DWORD=std::uint32_t;
constexpr HRESULT S_OK=0,S_FALSE=1,E_FAIL=-1;constexpr BOOL FALSE=0;
namespace D3 {constexpr unsigned kIssueEnd=1;}
#define FAILED(value) ((value)<0)
#include "query_poll.h"
struct IDirect3DQuery9 {
    HRESULT status=S_FALSE;BOOL value=0;unsigned calls=0;
    HRESULT GetData(void* data,DWORD size,DWORD flags){
        assert(flags==0&&size==sizeof value);++calls;
        if(status==S_OK)std::memcpy(data,&value,sizeof value);
        return status;
    }
};
void add(std::atomic<std::uint64_t>& count){count.fetch_add(1);}
struct StreamQuery {
    unsigned type=8,dataSize=4;std::uint32_t issueSeen=1,issueSeenEpoch=0;
    std::atomic<bool> dead{false};IDirect3DQuery9 driver;IDirect3DQuery9* inner=&driver;
    std::atomic<std::uint32_t> eventDoneGen{0},readyGen{0},resultSeq{0};
    std::atomic<HRESULT> eventStatus{S_FALSE};std::atomic<BOOL> eventValue{FALSE};
    unsigned char result[64]{};DWORD resultSize=0;
'''
middle = r'''
};
struct Replayer {
    struct {struct {struct {std::atomic<std::uint64_t> queryPolls{0};} stats;} q;std::atomic<std::uint32_t> queryEpoch{0};} core;
    std::vector<StreamQuery*> pending_;
'''
issue_wrapper = '''
    void issue(StreamQuery* q,unsigned flags){
        struct Args {unsigned dwIssueFlags;};Args args{flags};const Args* a=&args;
''' + issue + '''
    }
'''
reporter = '''
};
struct QueryReporter {
    std::uint64_t frameNo=0;std::atomic<std::uint64_t> framesReplayed{0};
    NorthlightStream::QueryWaitProfile queryFrame[21]{};
''' + report
checks = r'''
};
static void publish(StreamQuery& q,HRESULT status,BOOL value){
    unsigned char result[64]={};std::memcpy(result,&value,sizeof value);q.completeEvent(status,result);
}
static void valueIs(const StreamQuery& q,BOOL expected){
    BOOL value=0;std::memcpy(&value,q.result,sizeof value);assert(value==expected);
    assert(q.resultSize==4&&q.resultSeq.load()==2);
    for(unsigned i=4;i<sizeof q.result;++i)assert(q.result[i]==0);
}
int main(){
    // Execute the actual reporting gate: the existing cadence is preserved,
    // while one slow trip is reported even outside a sampled frame.
    {QueryReporter r;r.frameNo=128;std::vector<std::string> lines;
     auto sink=[&](const char* line){lines.emplace_back(line);};
     r.queryFrame[8].calls=1;r.queryFrame[8].maxTripNs=19999999;r.reportQueryFrame(sink);
     assert(lines.empty()&&r.queryFrame[8].calls==0);
     r.queryFrame[8].calls=1;r.queryFrame[8].maxTripNs=20000000;r.queryFrame[8].tripNs=20000000;
     r.queryFrame[8].beforeNs=13000000;r.queryFrame[8].replayNs=6000000;r.queryFrame[8].afterNs=1000000;
     r.reportQueryFrame(sink);assert(lines.size()==1&&r.queryFrame[8].calls==0);
     assert(lines[0].find("CSTREAM QUERYFRAME gameFrame=128")!=std::string::npos);
     assert(lines[0].find("beforeReplayMs=13.0000 replayMs=6.0000 afterReplayMs=1.0000 tripMs=20.0000")!=std::string::npos);
     r.reportQueryFrame(sink);assert(lines.size()==1);
     r.frameNo=127;r.queryFrame[8].calls=r.queryFrame[9].calls=1;
     r.reportQueryFrame(sink);assert(lines.size()==3&&r.queryFrame[8].calls==0&&r.queryFrame[9].calls==0);
     r.queryFrame[8].calls=1;r.queryFrame[8].maxTripNs=100000000;r.reportQueryFrame({});assert(r.queryFrame[8].calls==0);}
    // Same-generation terminal successes retain both TRUE/FALSE payloads. The
    // fake driver is ready too: baseline remains semantically valid but wasteful.
    for(BOOL value:{BOOL(0),BOOL(1),BOOL(0x12345678)}){
        Replayer r;StreamQuery q;q.issueSeen=73;q.driver.status=S_OK;q.driver.value=value;
        publish(q,S_OK,value);r.pending_={&q};r.pollQueries();
        assert(r.pending_.empty()&&q.readyGen.load()==73);valueIs(q,value);
        if(q.driver.calls!=0||r.core.q.stats.queryPolls.load()!=0){
            std::puts("DUPLICATE_EVENT_DRIVER_READ");return 77;
        }
    }
    // Pending generations still poll once; an ordinary driver completion and
    // error keep their original publication/removal semantics.
    for(HRESULT status:{S_FALSE,S_OK,E_FAIL}){
        Replayer r;StreamQuery q;q.driver.status=status;q.driver.value=9;
        r.pending_={&q};r.pollQueries();assert(q.driver.calls==1&&r.core.q.stats.queryPolls.load()==1);
        assert(r.pending_.empty()==(status!=S_FALSE));
        assert(q.eventDoneGen.load()==(status==S_FALSE?0u:1u));
        assert(q.readyGen.load()==(status==S_OK?1u:0u));
        if(status==S_OK)valueIs(q,9);
        else assert(q.resultSize==0);
    }
    // A published error is terminal even if the driver later reports success.
    {Replayer r;StreamQuery q;q.driver.status=S_OK;publish(q,E_FAIL,0);
     r.pending_={&q};r.pollQueries();assert(r.pending_.empty()&&q.driver.calls==0);
     assert(q.eventStatus.load()==E_FAIL&&q.readyGen.load()==0&&q.resultSize==0);}
    // The old generation cannot complete a reissued END. A later publication
    // completes only that new generation, without another driver read.
    {Replayer r;StreamQuery q;publish(q,S_OK,11);++q.issueSeen;
     r.pending_={&q};r.pollQueries();assert(r.pending_.size()==1&&q.driver.calls==1&&q.readyGen.load()==0);
     publish(q,S_OK,22);r.pollQueries();assert(r.pending_.empty()&&q.driver.calls==1&&q.readyGen.load()==2);valueIs(q,22);}
    // Actual replay END stamps the current reset epoch; repeated END entries
    // stay unique. A Reset invalidates the old completion even when it was S_OK.
    {Replayer r;StreamQuery q;q.issueSeen=0;r.core.queryEpoch.store(3);
     r.issue(&q,2);assert(q.issueSeen==0&&r.pending_.empty());
     r.issue(&q,1);assert(q.issueSeen==1&&q.issueSeenEpoch==3&&r.pending_.size()==1);
     publish(q,S_OK,17);r.core.queryEpoch.fetch_add(1);r.pollQueries();
     assert(q.driver.calls==1&&q.readyGen.load()==0&&r.pending_.size()==1);
     r.issue(&q,1);assert(q.issueSeen==2&&q.issueSeenEpoch==4&&r.pending_.size()==1);
     publish(q,S_OK,18);r.pollQueries();assert(r.pending_.empty()&&q.readyGen.load()==2&&q.driver.calls==1);valueIs(q,18);}
    {Replayer r;StreamQuery q;q.dead.store(true);r.issue(&q,1);assert(q.issueSeen==1&&r.pending_.empty());}
    // Reset/dead objects leave the pending list without dereferencing a driver.
    for(bool dead:{false,true}){Replayer r;StreamQuery q;q.dead.store(dead);
        if(!dead)q.inner=nullptr;r.pending_={&q};r.pollQueries();assert(r.pending_.empty()&&q.driver.calls==0);}
    // Zero is not a completed-generation sentinel, including generation wrap.
    {Replayer r;StreamQuery q;q.issueSeen=0;r.pending_={&q};r.pollQueries();assert(q.driver.calls==1&&r.pending_.size()==1);}
    // Occlusion always reads the driver and preserves nonterminal errors.
    for(HRESULT status:{S_FALSE,S_OK,E_FAIL}){Replayer r;StreamQuery q;q.type=9;q.driver.status=status;q.driver.value=3;
        publish(q,S_OK,7);r.pending_={&q};r.pollQueries();assert(q.driver.calls==1);
        assert(r.pending_.empty()==(status==S_OK));if(status==S_OK)valueIs(q,3);}
    // Swap/pop must not skip an adjacent pending, null or cached completion.
    {Replayer r;StreamQuery a,b,c,d;publish(a,S_OK,1);c.inner=nullptr;publish(d,E_FAIL,0);
     r.pending_={&a,&b,&c,&d};r.pollQueries();assert(r.pending_.size()==1&&r.pending_[0]==&b);
     assert(a.driver.calls+c.driver.calls+d.driver.calls==0&&b.driver.calls==1);}
    // Deterministic workload: each verified completion saves exactly one
    // driver call; this is not an elapsed-time or FPS benchmark.
    std::uint64_t calls=0,polls=0;
    for(unsigned i=1;i<=8192;++i){Replayer r;StreamQuery q;q.issueSeen=i;publish(q,S_OK,BOOL(i&1));
        r.pending_={&q};r.pollQueries();calls+=q.driver.calls;polls+=r.core.q.stats.queryPolls.load();
        assert(r.pending_.empty()&&q.readyGen.load()==i);valueIs(q,BOOL(i&1));}
    assert(calls==0&&polls==0);
    std::puts("PASS production pending-query loop: 8192 duplicate driver reads removed; pending, payload, errors, END reissue, reset, occlusion and swap/pop preserved; no game/GPU");
}
'''

with tempfile.TemporaryDirectory(prefix='event-terminal-') as directory:
    root = Path(directory)
    for label, flags in [('O2', ['-O2']), ('debug', ['-O0', '-g'])]:
        for negative, function in [(False, poll), (True, baseline)]:
            cpp = root / 'actual-loop.cpp'
            cpp.write_text(prefix + terminal + '\n' + write + middle + issue_wrapper + function + reporter + checks, encoding='utf-8')
            exe = root / 'actual-loop.exe'
            subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17',
                            '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *flags, *fp.test_include_flags(),
                            str(cpp), '-o', str(exe)], env=fp.zig_env(), check=True)
            result = subprocess.run([str(exe)], text=True, capture_output=True)
            print(label, 'original negative control' if negative else 'current production', result.stdout.strip())
            assert result.returncode == (77 if negative else 0), (result.returncode, result.stdout, result.stderr)
            assert ('DUPLICATE_EVENT_DRIVER_READ' in result.stdout) == negative
