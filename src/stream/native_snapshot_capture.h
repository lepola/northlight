#pragma once
// Explicit outer-trace capture only. The owner must disconnect BOTH callbacks
// before finish/destruction. Ordinary rendering constructs none of this state.
#include "snapshot_capture_sink.h"
#include <cstring>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace NorthlightStream {
class NativeSnapshotCapture {
public:
    using MarkerFn=bool(*)(void*,const wchar_t*,unsigned) noexcept;
    static constexpr unsigned MarkerChars=320,StatusBytes=768;
    enum Failure:unsigned {None=0,Start=1,Sink=2,ObserverOrder=4,Provenance=8,Marker=16,Concurrent=32,StatusIo=64};
    struct Status {
        SnapshotCaptureSink::Status sink;
        unsigned marked=0,observerFailures=0;bool pending=false,started=false,finished=false,complete=false;
        bool diagnostic=true,fpsEligible=false,traceJoinValidated=false;
    };
    NativeSnapshotCapture()=default;
    ~NativeSnapshotCapture(){finish();}
    NativeSnapshotCapture(const NativeSnapshotCapture&)=delete;
    NativeSnapshotCapture& operator=(const NativeSnapshotCapture&)=delete;
    bool open(const std::filesystem::path& path,const std::string& session,MarkerFn marker,void* context=nullptr){
        return begin(path,session,marker,context,SnapshotCaptureSink::Limits{},true);
    }
    bool open(const std::filesystem::path& path,const std::string& session,MarkerFn marker,void* context,SnapshotCaptureSink::Limits limits){
        return begin(path,session,marker,context,limits,true);
    }
    // This callback remains bounded and allocation/IO/wait free. The receipt
    // describes the bytes the sink encoded, not a second encoding of the packet.
    static void completed(void* opaque,const GameSnapshot& snapshot,std::uintptr_t device,std::uint64_t frame,std::uint32_t epoch,std::uint32_t thread) noexcept {
        if(!opaque)return;auto& self=*static_cast<NativeSnapshotCapture*>(opaque);
        if(!self.enabled_.load(std::memory_order_acquire))return;
        Guard guard(self);if(!guard.entered||!self.enabled_.load(std::memory_order_acquire))return;
        if(self.pending_.accepted){self.reject(ObserverOrder);return;}
        if(snapshot.triggerKind<1||snapshot.triggerKind>3){self.reject(Provenance);return;}
        if(!self.sink_.submit(snapshot,device,frame,epoch,thread,&self.pending_))self.reject(Sink);
    }
    // A separate opt-in trace observer. The injected marker implementation may
    // serialize externally; it is never invoked by SnapshotCompleted.
    static void observed(void* opaque,const GameSnapshot& snapshot,std::uintptr_t device,std::uint64_t frame,std::uint32_t epoch,std::uint32_t thread) noexcept {
        if(!opaque)return;auto& self=*static_cast<NativeSnapshotCapture*>(opaque);
        if(!self.enabled_.load(std::memory_order_acquire))return;
        Guard guard(self);if(!guard.entered||!self.enabled_.load(std::memory_order_acquire))return;
        const auto& receipt=self.pending_;
        if(!receipt.accepted){self.reject(ObserverOrder);return;}
        if(receipt.device!=device||receipt.frame!=frame||receipt.epoch!=epoch||receipt.thread!=thread||
           receipt.trigger!=snapshot.triggerKind||receipt.draw!=snapshot.triggerDraw){self.reject(Provenance);return;}
        wchar_t text[MarkerChars];const unsigned length=formatMarker(self.session_,receipt,text);
        if(!length||!self.marker_(self.markerContext_,text,length)){self.reject(Marker);return;}
        ++self.marked_;self.pending_={};
    }
    // Canonical ASCII grammar, converted without locale-dependent wide printf:
    // lower-case unprefixed hex device/8-digit CRC; all other numbers decimal.
    // The synchronous callback must copy the text if it retains it after return.
    static unsigned formatMarker(const char* session,const SnapshotCaptureSink::Receipt& receipt,wchar_t (&output)[MarkerChars]) noexcept {
        output[0]=0;if(!session||!receipt.accepted||!receipt.device||!receipt.thread||receipt.trigger<1||receipt.trigger>3||
            receipt.record>=SnapshotCaptureSink::MaxRecords||receipt.bytes<SnapshotArchive::HeaderBytes+SnapshotArchive::RecordBytes+SnapshotArchive::ChecksumBytes||receipt.bytes>SnapshotArchive::MaxBytes)return 0;
        unsigned size=0;for(;size<=64&&session[size];++size){const char c=session[size];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'))return 0;}
        if(!size||size>64)return 0;
        char ascii[MarkerChars];const int length=std::snprintf(ascii,sizeof ascii,
            "NLSNAP1;session=%s;record=%u;device=%llx;frame=%llu;epoch=%u;thread=%u;trigger=%u;draw=%llu;bytes=%u;crc=%08x",
            session,receipt.record,(unsigned long long)receipt.device,(unsigned long long)receipt.frame,receipt.epoch,receipt.thread,
            receipt.trigger,(unsigned long long)receipt.draw,receipt.bytes,receipt.crc32);
        if(length<=0||unsigned(length)>=MarkerChars)return 0;
        for(unsigned i=0;i<=unsigned(length);++i)output[i]=wchar_t(static_cast<unsigned char>(ascii[i]));return unsigned(length);
    }
    // Owner only after both callbacks are disconnected. This may wait and write;
    // a clean archive footer alone never establishes a valid trace association.
    bool finish()noexcept {
        if(finished_)return complete_&&!failures_.load();
        enabled_.store(false,std::memory_order_release);finished_=true;
        try{
            if(!sink_.finish()&&started_)reject(Sink);
            const auto archive=sink_.status();if(pending_.accepted||marked_!=archive.accepted)reject(ObserverOrder);
            complete_=started_&&!failures_.load()&&!archive.failures&&archive.joined&&archive.accepted==archive.retired;
            if(!owned_)return false;
            char text[StatusBytes];const int length=std::snprintf(text,sizeof text,
                "{\"format\":1,\"kind\":\"native-snapshot-capture-status\",\"session\":\"%s\",\"capture_layer\":\"above-northlight\",\"capture_layer_validated\":false,\"diagnostic\":true,\"fps_eligible\":false,\"trace_join_validated\":false,\"started\":%s,\"finished\":true,\"accepted\":%u,\"retired\":%u,\"marked\":%u,\"sink_failures\":%u,\"failures\":%u,\"pending\":%s,\"complete\":%s,\"status_allowance_bytes\":%u,\"budget_bytes\":%llu}\n",
                session_,started_?"true":"false",archive.accepted,archive.retired,marked_,archive.failures,failures_.load(),pending_.accepted?"true":"false",complete_?"true":"false",StatusBytes,(unsigned long long)(archive.budgetBytes+StatusBytes));
            const auto pending=path_/"native-capture-status.pending",final=path_/"native-capture-status.json";
            SnapshotCaptureSink::File file;bool saved=length>0&&unsigned(length)<sizeof text&&file.create(pending)&&file.write(text,std::size_t(length));
            saved=file.close()&&saved;
            if(saved){
#ifdef _WIN32
                saved=MoveFileW(pending.c_str(),final.c_str())!=FALSE;
#else
                saved=::link(pending.c_str(),final.c_str())==0;
                // The link publishes the closed file. A leftover pending alias
                // after cleanup failure is never completion authority.
                if(saved)(void)::unlink(pending.c_str());
#endif
            }
            if(!saved){reject(StatusIo);complete_=false;}
        }catch(...){reject(StatusIo);complete_=false;}
        return complete_;
    }
    // Owner-only, after callbacks are quiescent (as for finish).
    Status status()const noexcept {return {sink_.status(),marked_,failures_.load(),pending_.accepted,started_,finished_,complete_&&!failures_.load()};}
private:
    friend struct NativeSnapshotCaptureTest;
    SnapshotCaptureSink sink_;SnapshotCaptureSink::Receipt pending_;
    char session_[65]{};std::filesystem::path path_;MarkerFn marker_=nullptr;void* markerContext_=nullptr;
    unsigned marked_=0;bool owned_=false,started_=false,finished_=false,complete_=false;
    std::atomic<unsigned> failures_{None};std::atomic<bool> enabled_{false};std::atomic_flag busy_=ATOMIC_FLAG_INIT;
    struct Guard {
        NativeSnapshotCapture& owner;bool entered;
        explicit Guard(NativeSnapshotCapture& value)noexcept:owner(value),entered(!value.busy_.test_and_set(std::memory_order_acquire)){if(!entered)owner.reject(Concurrent);}
        ~Guard(){if(entered)owner.busy_.clear(std::memory_order_release);}
    };
    void reject(unsigned failure)noexcept {failures_.fetch_or(failure,std::memory_order_relaxed);enabled_.store(false,std::memory_order_release);}
    bool begin(const std::filesystem::path& path,const std::string& session,MarkerFn marker,void* context,SnapshotCaptureSink::Limits limits,bool launch){
        if(started_||finished_||failures_.load()||!marker||session.empty()||session.size()>64||
           limits.bytes<StatusBytes+SnapshotCaptureSink::RecordLineBytes*2||limits.bytes>SnapshotCaptureSink::MaxSessionBytes){reject(Start);return false;}
        for(char c:session)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-')){reject(Start);return false;}
        std::memcpy(session_,session.c_str(),session.size()+1);
        try{
            path_=path;
            // Keep the total session cap unchanged, including the owner report.
            limits.bytes-=StatusBytes;
            const bool opened=sink_.open(path,session,limits,launch);owned_=!sink_.destination_.empty();
            if(!opened){reject(Start|Sink);return false;}
            marker_=marker;markerContext_=context;started_=true;
            enabled_.store(true,std::memory_order_release);return true;
        }catch(...){owned_=!sink_.destination_.empty();reject(Start);return false;}
    }
};
} // namespace NorthlightStream
