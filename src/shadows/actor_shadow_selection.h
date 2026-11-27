#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <iterator>
#include <vector>

// Stable quality policy for the optional actor shadow quota (budget>0 only).
// Draws of one constant group are one actor: they cast or drop together.
// Rigid actors (every draw bound to the same single palette bone) are either
// free-standing (doodads, fences: cast unconditionally and never consume the
// quota, exactly as with budget 0) or attachments (weapons, shoulders, held
// items: follow their body's decision, so they never cast alone). Rigid bytes
// of either class stay outside the quota; charging attachments to their body
// would make every capture gap of a weapon move the cut.
// The class is sticky per rigid identity. During the first FreeFrames frames
// (probation) a rigid actor within AttachRadius of a non-rigid sampled vertex
// follows that body, and becomes a confirmed attachment once it moves more than
// CoMoveTolerance from where it was first seen (it moves with the body); one
// that stays still settles as free-standing, so a fence beside a guard keeps
// casting when the guard walks away or is dropped. A confirmed attachment keeps
// last frame's body (else the nearest) within KeepReach*AttachRadius, holds its
// last decision while no body is in reach and frees only after FreeFrames such
// frames. A settled free-standing actor re-attaches only if it moves more than
// MoveTolerance while a body is within AttachRadius, so a patrol passing a fence
// never captures it. An attachment casts only if its body and every other body
// within AttachRadius cast. A rigid actor without a sampled position is ranked
// like any other actor.
// Remaining actors are ranked by nearest sampled vertex; actors kept last frame
// rank with distance scaled by (1-margin). Walking that ranking, the first actor
// that does not fit closes the cut: everything after it drops (monotonic
// prefix). An actor not kept last frame must fit on AdmitFrames consecutive
// frames before it is admitted, unless it is unlocated (first claim, as in
// 0.3.137) or within InnerCut of last frame's cut distance (no identity needed
// there, so fast or erratic near actors are never starved). While waiting it is
// dropped and reserves nothing, so a one-frame capture gap elsewhere cannot
// flash it on. Actors kept recently but missing from this frame's capture keep
// their bytes reserved for up to MaxAge frames; an unmatched actor releases the
// nearest such reservation of its shape. Identity: first draw's shader/
// declaration/mesh shape plus a sampled world position within a tolerance.
namespace NorthlightActorShadowSelection {
constexpr bool Enabled=true;
constexpr float InnerCut=.8f,ClaimRadius=8.f,CoMoveTolerance=.05f,HysteresisMargin=.10f,IdentityTolerance=1.5f,AttachRadius=4.f,KeepReach=1.5f,MoveTolerance=.25f;
constexpr unsigned AdmitFrames=3,MaxAge=8,FreeFrames=8,ProbationStill=3,StationaryFrames=120,ExemptMoveFrames=2,Samples=12;
constexpr float StationaryTolerance=.02f,RootReach=30.f,LooseTolerance=2.f;
// Tunables that changed after 0.3.138; Legacy reproduces 0.3.138 decisions.
// Rigid classes are position-keyed and stationary, so they are kept while the
// camera looks away (frustum exits otherwise restarted probation, darkening a
// fence beside a dropped NPC for FreeFrames on every return).
// Margin .20 and a 10-frame admission beyond the inner cut halve reversals in
// the orbit/walk simulation; a new rigid actor follows a body only until it has
// been still for ProbationStill frames (a fence entering view near a dropped
// NPC no longer goes dark for the whole probation).
// stationary: a non-rigid actor (multi-bone doodad, e.g. an event GameObject)
// whose world samples stay within StationaryTolerance of their anchors for
// StationaryFrames is exempt like a free-standing rigid doodad, up to
// exemptFraction*budget of stationary exempt bytes in their own pool (rigid
// bytes are not charged unless sharedWithRigid; nearest first); it leaves the state
// only after ExemptMoveFrames consecutive moving frames. stayFrames/stay: keep
// ranking (and identities) until actor bytes stay below stay*budget that long.
// A still actor that is not (yet) exempt is frozen: kept ones rank with the
// margin applied twice, dropped ones need room for three admission periods
// (or the inner cut), so a moving cut rarely flips them before they are exempt.
struct Tuning {float margin=.20f;unsigned rigidMaxAge=1800;std::size_t rigidCapacity=8192;unsigned reserveAge=30,admitFrames=10;bool probationFollow=false;
    bool stationary=true;float exemptFraction=1.f;unsigned stayFrames=30;float stay=.9f;
    float settledMove=CoMoveTolerance; /* a weapon that settled while its body was off-screen re-attaches on the body's first steps */
    bool sharedWithRigid=false; /* v3 variant: rigid bytes charged to the stationary cap */
    // stableIdentity: an actor is identified by the smallest stable per-draw key
    // of its draws (snapshot-cache entry = VB/IB identity + range, shader, decl),
    // not by whichever draw the game happened to issue first; stationary samples
    // are ordered by that key too. rateFrames: an actor's kept/dropped state
    // changes at most once per rateFrames unless the budget forces a drop.
    bool stableIdentity=true;unsigned rateFrames=60;
    // cadence: rendered frames per selection (NearShadowInterval when model capture is
    // skipped between captures). Every frame count above is in RENDERED frames: atCadence()
    // divides the Tuning counts, frames() the constants below, rounding up. 1 = unchanged.
    unsigned cadence=1;
    constexpr unsigned frames(unsigned n)const{return cadence>1?(n+cadence-1)/cadence:n;}};
// FlickerFixes=false keeps only the (decision-identical) cost work of 0.3.139.
constexpr bool FlickerFixes=true;
constexpr Tuning Legacy{.10f,MaxAge,SIZE_MAX,MaxAge,AdmitFrames,true,false,0,0,1,MoveTolerance,false,false,0};
inline Tuning active(){return FlickerFixes?Tuning{}:Legacy;}
inline Tuning atCadence(Tuning t,unsigned cadence){if(cadence<=1)return t;t.cadence=cadence;
    t.rigidMaxAge=t.frames(t.rigidMaxAge);t.reserveAge=t.frames(t.reserveAge);t.admitFrames=t.frames(t.admitFrames);t.stayFrames=t.frames(t.stayFrames);t.rateFrames=t.frames(t.rateFrames);return t;}
// ActorShadowRadius (northlight-quality.ini, yards). choose() without a Radius, or
// with yards 0, is exactly the 0.3.144 selection. Otherwise a body actor (not
// rigid free-standing, not attached) farther than the radius from the player
// casts no shadow: it is dropped BEFORE the quota (never ranked, charged or
// reserved; with decide the quota ranks only on the bytes inside the radius)
// and its attachments follow it. Free-standing rigid actors (fences, lanterns)
// are unaffected, as is GI (its actor
// packets are copied at capture, before selection). Every uncertainty fails
// toward extra shadows, never toward losing the player's own.
// No player position is read from the game: the player ("self") is found in
// the draws. The camera orbits the player, so the player is on the eye->pivot
// ray while the pivot estimate itself may be stale (pivotDistance only updates
// while orbiting: after a mouse-wheel zoom it is off by the zoom, 3..35 yd).
// Candidates: bodies with a sampled vertex or a point of their vertical axis
// (palette root..root+SelfAxis: the camera aims at the player's axis) within
// SelfRay of the ray at SelfMinT..SelfMaxT from the eye. Score: squared miss +
// ((along - |pivot-eye|)/SelfPivotYards)^2, so a stale pivot only weakly pulls.
// The camera follows the player: while the eye moves by d (>= SelfMove), a
// candidate whose root moved by d (within SelfFollow*|d|) "follows"; if any
// candidate follows, the others (a still NPC in front while walking) cannot be
// the best. The current self is the best while it is a candidate (and follows
// when anyone does). Exactly one self, with hysteresis per identity (a body
// that moved with the camera is matched where it was, shifted by the camera's
// move, so fast flight keeps it): it stays self SelfHold selections after it
// stops being the best candidate (zoom, turn, capture gap: its position then
// follows the camera), and another body becomes self only after being the best
// candidate for admitFrames consecutive selections. A crossing NPC therefore
// never casts through the self rule. Until a self is established (after a
// reset, map change, teleport, or once the hold expires): no filter at all.
// The self's root is the centre of the circle.
// Distance: centre to the actor's bounds (box of its sampled vertices and root),
// so a big model is not judged by one arbitrary vertex. The radius is at least
// MinYards (1..3 = "your character and whatever stands on or right next to it").
// Hysteresis per identity (the quota's key + palette root/vertex, assigned
// nearest pair first; a body whose key changed takes the nearest unclaimed entry
// of any key; unmatched entries survive reserveAge selections): an actor inside
// last frame leaves only beyond radius+band, band=max(BandMin, BandFraction*radius),
// and within RadiusDwell selections of its last change only one band further.
// Companions of the self (mount, passenger, anyone standing on or right next
// to it): bounds overlapping its bounds, or within MinYards of its root
// (+CompanionBand to stay); they always cast. An attachment whose body is in this group, or that is nearer one
// of its bodies than its own body, always casts; an attachment that lost its
// body (a capture gap) and lands across the group boundary holds its last
// decision.
// Unknown distance (no sampled vertex): kept (fail-open; 0 in all logs so far).
// decide: the quota ranks (History::shouldRank) on the bytes inside the radius;
// otherwise ranked says whether it does. Unranked frames (budget 0 or within
// it): bodies inside the radius all cast; only rigid classes and radius
// identities are kept.
struct Radius {float yards=0;const float* eye=nullptr;bool ranked=true,decide=false;bool active()const{return yards>0;}};
constexpr unsigned SelfHold=30;
// An identity not found where it was is looked for shifted by the camera's move,
// within IdentityTolerance + SelfGap*|move| (it may have missed a capture or two).
constexpr float SelfGap=2.f,MaxShift=8.f; /* the widening stops at 8 yd of camera move (a teleport shifts, it does not widen) */
// A body's in/out state changes at most once per RadiusDwell selections unless it
// is another band beyond the edge (identity or centre noise cannot flicker it).
constexpr unsigned RadiusDwell=30;
constexpr float ClusterGap=1.f,ClusterExtent=4.f; /* sections of one character (see radiusStage) */
constexpr float SelfRay=2.f,SelfAxis=3.f,SelfPivotYards=40.f,SelfMove=.05f,SelfFollow=.3f,SelfMinT=.5f,SelfMaxT=80.f,MinYards=3.f,CompanionBand=1.f,BandMin=2.f,BandFraction=.1f;
inline float band(float yards){return std::max(BandMin,BandFraction*yards);}
struct Draw {
    std::size_t index=0,bytes=0;unsigned group=0;
    float distanceSquared=0,at[3]={};bool known=false,rigid=false;float bone=0;
    std::uint64_t key=0;bool keep=true;
    float extra[2][3]={};unsigned char extras=0; /* extra world samples, only for stationary candidates */
    float root[3]={};bool hasRoot=false; /* palette bone 0 origin in world space (per-instance anchor) */
    /* 0.3.211 (task 31): the draw's capture identity for the capture admission (capture_admission.h): never read by the selection's decisions */
    std::uintptr_t shader=0;unsigned count=0;float captureRoot[3]={};bool captureRootValid=false;
};
struct RigidEntry {std::uint64_t key=0,bodyKey=0;float at[3]={},anchor[3]={},bodyAt[3]={};unsigned free=0,age=0,still=0;bool attachment=false,settled=false,kept=false,used=false,selfBody=false,everAttached=false;};
struct RadiusPair {float d=0;std::uint32_t actor=0,entry=0;};
struct RadiusEntry {std::uint64_t key=0;float at[3]={};unsigned age=0,streak=0,since=~0u;bool inside=false,companion=false,self=false,used=false,filtered=false;};
struct Actor {
    std::size_t first=0,count=0,bytes=0,body=SIZE_MAX;float distanceSquared=0,rank=0,at[3]={},anchor[3]={},bodyAt[3]={};
    std::uint64_t key=0,bodyKey=0;unsigned pending=0,free=0,still=0;
    bool known=false,rigid=false,located=false,previous=false,matched=false,waiting=false,keep=true;
    bool rigidTracked=false,attachment=false,settled=false,orphan=false,search=false;
    RigidEntry* entry=nullptr;
    float samples[Samples][3]={};unsigned mask=0,stillFrames=0,stillExtras=0,moving=0;bool exempt=false,exemptCandidate=false,wasExempt=false,frozen=false;
    unsigned sinceChange=~0u;bool locked=false,rooted=false;float vertexAt[3]={};
    bool everAttached=false,toFree=false,toAttach=false,rekeyed=false,outside=false,self=false,companion=false,rebound=false,selfBody=false,nearSelf=false,candidate=false,follows=false,unfiltered=false;unsigned streak=0,radiusSince=~0u;std::size_t head=SIZE_MAX;float radiusAt[3]={};RadiusEntry* radiusEntry=nullptr; /* ActorShadowRadius; head: the character (see radiusStage) */
};
struct Point {std::uint64_t cell=0;float x=0,y=0,z=0;std::size_t actor=0;};
struct Scratch {std::vector<Actor> actors;std::vector<std::size_t> order,candidates;std::vector<Point> points;std::vector<std::pair<std::size_t,std::size_t>> links;};
struct Result {
    std::size_t kept=0,dropped=0,keptBytes=0,droppedBytes=0,unknown=0;
    std::size_t actors=0,actorsKept=0,rigidActors=0,rigidDraws=0,rigidBytes=0,toggles=0,matched=0,retained=0,admitted=0,waiting=0,reserved=0;
    std::size_t attached=0,attachedBytes=0,orphans=0,rigidStuck=0,rigidNew=0;
    std::size_t exemptActors=0,exemptDraws=0,exemptBytes=0,cappedExempt=0,stillRankedSmall=0,exemptCapBinds=0,locked=0,exemptNpcLike=0,rooted=0,rootFallback=0,rootRejected=0;
    std::size_t radiusDropped=0,radiusDroppedDraws=0,radiusDroppedBytes=0,radiusToggles=0,radiusFlicker=0,radiusRekeyed=0,radiusCharacters=0,radiusSelf=0,radiusCompanions=0,radiusUnreferenced=0,radiusInsideBytes=0,radiusSelfMissing=0;
    // Diagnostics (crowd shadow blink): rigid actors changing between attached and free this selection (any state of the class
    // chain: probation, settled), radius rekeys whose adopted state is contradicted by the decision, and (diagnostics frames
    // only) kept free rigid actors beyond radius+band of the centre, with their bytes and the farthest one in yards.
    // Beyond = farther than radius+band from the centre. freeRigid*: former attachments (ever had a body) that are free now; propRigid*:
    // rigid actors that never had one (lanterns, signs); stuckBeyond: former attachments beyond that are settled and not re-attaching.
    // beyondKnown=false (no centre this selection, or not a diagnostics frame): the beyond gauges were not measured.
    std::size_t attachToFree=0,freeToAttach=0,attachToFreeBeyond=0,freeToAttachBeyond=0,radiusRekeyFlips=0,freeRigidBeyond=0,freeRigidBeyondBytes=0,propRigidBeyond=0,propRigidBeyondBytes=0,stuckBeyond=0;float freeRigidMaxYards=0;bool beyondKnown=false;
    bool ranked=true; /* the quota ranked this frame (Radius decide) */
    // Diagnostics frames only: the radius centre, and the first characters judged outside although their bounds are within
    // the set radius of it (the hysteresis limits, a body without a usable sample, a centre that just moved).
    struct NearOutside {float at[3]={},yards=0,limit=0;bool was=false,hold=false,sections=false;};
    static constexpr unsigned NearOutsideMax=6;
    NearOutside nearOutside[NearOutsideMax];unsigned nearOutsideCount=0,nearOutsideTotal=0;float radiusCentre[3]={},radiusYards=0;bool radiusCentreKnown=false;
};
namespace detail {
struct Character {unsigned in=0,out=0,since=~0u,streak=0;bool stay=false;}; /* ActorShadowRadius: per head, from its sections' entries */
struct Box { /* ActorShadowRadius bounds */float low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};
    void add(const float* p){for(unsigned k=0;k<3;++k){low[k]=std::min(low[k],p[k]);high[k]=std::max(high[k],p[k]);}}
    bool valid()const{return low[0]<=high[0];}
    float squared(const float* p)const{float q=0;for(unsigned k=0;k<3;++k){const float x=std::max({low[k]-p[k],p[k]-high[k],0.f});q+=x*x;}return q;}
    float squared(const Box& b)const{float q=0;for(unsigned k=0;k<3;++k){const float x=std::max({low[k]-b.high[k],b.low[k]-high[k],0.f});q+=x*x;}return q;}};
// Entries are sorted by (key, x): same-model crowds are searched only inside
// the x window of the radius instead of across every entry of the model.
template<class Entry> bool before(const Entry& a,const Entry& b){return a.key<b.key||(a.key==b.key&&a.at[0]<b.at[0]);}
template<class Entry> Entry* nearest(std::vector<Entry>& entries,std::uint64_t key,const float* at,float radius){
    auto it=std::lower_bound(entries.begin(),entries.end(),std::make_pair(key,at[0]-radius),[](const Entry& e,const std::pair<std::uint64_t,float>& k){return e.key<k.first||(e.key==k.first&&e.at[0]<k.second);});
    Entry* best=nullptr;float bestSquared=radius*radius;
    for(;it!=entries.end()&&it->key==key&&it->at[0]<=at[0]+radius;++it){if(it->used)continue;float d=0;
        for(unsigned a=0;a<3;++a){const float x=it->at[a]-at[a];d+=x*x;}
        if(d<=bestSquared){bestSquared=d;best=&*it;}}
    return best;
}
}
class History {
    struct Entry {std::uint64_t key=0;std::size_t bytes=0;float at[3]={};unsigned pending=0,age=0;bool kept=false,used=false;
        float samples[Samples][3]={};unsigned mask=0,stillFrames=0,stillExtras=0,moving=0,sinceChange=~0u;bool exempt=false;};
    using Rigid=RigidEntry;
    std::vector<Entry> entries_,next_;std::vector<Rigid> rigid_,nextRigid_,mergeRigid_;bool allKept_=false,ranking_=false;unsigned under_=0;float lastCut_=INFINITY,steadyCut_=INFINITY;
    std::vector<RadiusEntry> radius_,nextRadius_;std::vector<std::pair<std::size_t,float>> candidates_;std::vector<RadiusPair> pairs_;std::vector<std::uint32_t> spare_,hits_;std::vector<detail::Box> boxes_;std::vector<detail::Character> characters_;std::vector<std::pair<float,float>> misses_;float eye_[3]={},selfAt_[3]={};bool eyeValid_=false;unsigned selfHold_=0;std::uint64_t selfKey_=0;
public:
    // Frames within budget keep everything; no ranking identities are needed
    // then. Rigid classes and radius identities survive them: they do not
    // depend on the budget.
    void keptAll(){entries_.clear();allKept_=true;lastCut_=steadyCut_=INFINITY;ranking_=false;under_=0;}
    void clear(){entries_.clear();rigid_.clear();radius_.clear();eyeValid_=false;selfHold_=0;allKept_=false;lastCut_=steadyCut_=INFINITY;ranking_=false;under_=0;}
    // Budget-state hysteresis: once over budget, keep ranking (identities,
    // pending admissions, stationary state) until actor bytes stay below
    // stay*budget for stayFrames frames. Within-budget frames then keep all
    // except delayed re-admissions instead of resetting to "everything".
    bool shouldRank(std::size_t actorBytes,std::size_t budget,const Tuning& tuning){
        if(!budget)return false;
        if(actorBytes>budget){ranking_=true;under_=0;return true;}
        if(!ranking_||!tuning.stayFrames)return false;
        under_=double(actorBytes)<double(budget)*tuning.stay?under_+1:0;
        if(under_>=tuning.stayFrames){ranking_=false;under_=0;return false;}
        return true;
    }
    // Read-only: does the renderer need extra world samples for this actor?
    bool stationaryHint(std::uint64_t key,const float* at)const{
        auto it=std::lower_bound(entries_.begin(),entries_.end(),std::make_pair(key,at[0]-IdentityTolerance),[](const Entry& e,const std::pair<std::uint64_t,float>& k){return e.key<k.first||(e.key==k.first&&e.at[0]<k.second);});
        for(;it!=entries_.end()&&it->key==key&&it->at[0]<=at[0]+IdentityTolerance;++it){float d=0;for(unsigned a=0;a<3;++a){const float x=it->at[a]-at[a];d+=x*x;}
            if(d<=IdentityTolerance*IdentityTolerance&&(it->stillFrames||it->exempt))return true;}
        return false;
    }
    // Distance squared of the farthest kept actor when the last ranked frame dropped any.
    float lastCut()const{return lastCut_;}
    // The cut the inner-cut test uses with the rate limit: it follows a shrinking
    // cut at once but grows at most 5% per frame, so a frame-to-frame swing of
    // the cut cannot pull actors in past the lock and drop them the next frame.
    float steadyCut()const{return steadyCut_;}
    // An unmatched actor releases the nearest stale reservation of its shape
    // within ClaimRadius (a fast mover would otherwise reserve its bytes again
    // every frame); same-model actors farther away keep theirs.
    void claim(std::uint64_t key,const float* at){if(auto* e=detail::nearest(entries_,key,at,ClaimRadius))e->used=true;}
    // No usable history (first ranked frame, after reset or a within-budget
    // frame): every actor counts as kept last frame.
    bool allKept()const{return allKept_||entries_.empty();}
    std::size_t size()const{return entries_.size()+rigid_.size();}
    Entry* find(std::uint64_t key,const float* at){return detail::nearest(entries_,key,at,IdentityTolerance);}
    Rigid* findRigid(std::uint64_t key,const float* at,float reach=IdentityTolerance){return detail::nearest(rigid_,key,at,reach);}
    std::size_t radiusSize()const{return radius_.size();}
    void clearRadius(){radius_.clear();} /* task 45: the radius path was left (ActorShadowCpuMs level back to an unlimited setting) */
    std::vector<RadiusEntry>& radiusEntries(){return radius_;}
    std::vector<RadiusPair>& radiusPairs(){return pairs_;} /* scratch: identity candidates */
    std::vector<std::uint32_t>& radiusSpare(){return spare_;} /* scratch: unclaimed entries by x */
    std::vector<std::uint32_t>& radiusHits(){return hits_;} /* scratch: nearest candidate per entry */
    std::vector<detail::Box>& radiusBoxes(){return boxes_;} /* scratch: per actor, a character's bounds at its head */
    std::vector<detail::Character>& radiusCharacters(){return characters_;} /* scratch: per head */
    std::vector<std::pair<float,float>>& radiusMisses(){return misses_;} /* scratch: per head, squared miss of the centre ray and its depth */
    // Radius self state: last selection's camera eye, the self's position and
    // the selections it stays self without being the best candidate.
    const float* lastEye()const{return eyeValid_?eye_:nullptr;}
    void eye(const float* e){std::memcpy(eye_,e,sizeof eye_);eyeValid_=true;}
    const float* selfAt()const{return selfAt_;}
    void selfAt(const float* at){std::memcpy(selfAt_,at,sizeof selfAt_);}
    unsigned selfHold()const{return selfHold_;}
    std::uint64_t selfKey()const{return selfKey_;}
    void selfKey(std::uint64_t k){selfKey_=k;}
    void selfHold(unsigned n){selfHold_=n;}
    std::vector<std::pair<std::size_t,float>>& candidates(){return candidates_;}
    // Bytes of actors kept recently but absent from this frame's capture.
    std::size_t reserved(unsigned age=MaxAge)const{std::size_t n=0;for(const auto& e:entries_)if(!e.used&&e.kept&&!e.exempt&&e.age<age)n+=e.bytes;return n;}
    // Exempt actors out of view keep their room under the exempt cap.
    std::size_t exemptReserved()const{std::size_t n=0;for(const auto& e:entries_)if(!e.used&&e.exempt)n+=e.bytes;return n;}
    // Rigid classes persist for long (rigidMaxAge): matched entries are updated
    // in place, unmatched ones age, new ones are merged in; the (key, x) order is
    // restored by an insertion pass (positions move little), so no full sort of
    // thousands of stationary fences per frame.
    // ranked=false (a radius-only frame): budget identities are reset as by
    // keptAll(). radius: republish the radius identities.
    void publish(const std::vector<Actor>& actors,float cut,const Tuning& tuning,bool ranked=true,bool radius=false){
        next_.clear();nextRigid_.clear();lastCut_=cut;
        float grown=steadyCut_;for(unsigned n=0;n<tuning.cadence||!n;++n)grown=grown*1.05f*1.05f; /* 5% per rendered frame */
        steadyCut_=!(cut<steadyCut_)&&std::isfinite(steadyCut_)?std::min(cut,grown):cut;
        for(const auto& a:actors){
            if(a.rigidTracked){Rigid r;r.key=a.key;std::memcpy(r.at,a.at,sizeof r.at);std::memcpy(r.anchor,a.anchor,sizeof r.anchor);r.free=a.free;r.still=a.still;r.attachment=a.attachment;r.settled=a.settled;r.kept=a.keep;r.bodyKey=a.bodyKey;std::memcpy(r.bodyAt,a.bodyAt,sizeof r.bodyAt);r.selfBody=a.selfBody;r.everAttached=a.everAttached;
                if(a.entry){r.used=true;*a.entry=r;}else nextRigid_.push_back(r);}
            else if(ranked&&!a.rigid&&a.located){Entry e;e.key=a.key;e.bytes=a.bytes;std::memcpy(e.at,a.at,sizeof e.at);e.kept=a.keep;e.pending=a.pending;
                std::memcpy(e.samples,a.samples,sizeof e.samples);e.mask=a.mask;e.stillFrames=a.stillFrames;e.stillExtras=a.stillExtras;e.moving=a.moving;e.exempt=a.exempt;
                e.sinceChange=!a.matched?tuning.rateFrames:a.keep!=a.previous?0:std::min(a.sinceChange+1,tuning.rateFrames);next_.push_back(e);}}
        // Stationary state (exempt or accumulating) outlives a look away like a
        // rigid class does; other identities keep the short reservation age.
        if(ranked&&!allKept_)for(const auto& e:entries_)if(!e.used&&e.age<((e.exempt||e.stillFrames>=tuning.frames(ProbationStill))?tuning.rigidMaxAge:tuning.reserveAge)){next_.push_back(e);++next_.back().age;}
        std::size_t write=0;
        for(std::size_t i=0;i<rigid_.size();++i){auto& r=rigid_[i];
            if(r.used){r.used=false;r.age=0;}else if(r.age<tuning.rigidMaxAge)++r.age;else continue;
            if(write!=i)rigid_[write]=r;++write;}
        rigid_.resize(write);
        for(std::size_t i=1;i<rigid_.size();++i)if(detail::before(rigid_[i],rigid_[i-1])){
            Rigid r=rigid_[i];std::size_t j=i;for(;j&&detail::before(r,rigid_[j-1]);--j)rigid_[j]=rigid_[j-1];rigid_[j]=r;}
        std::sort(nextRigid_.begin(),nextRigid_.end(),detail::before<Rigid>);
        if(!nextRigid_.empty()){mergeRigid_.clear();mergeRigid_.reserve(rigid_.size()+nextRigid_.size());
            std::merge(rigid_.begin(),rigid_.end(),nextRigid_.begin(),nextRigid_.end(),std::back_inserter(mergeRigid_),detail::before<Rigid>);rigid_.swap(mergeRigid_);}
        if(rigid_.size()>tuning.rigidCapacity){ /* keep the most recently seen */
            std::nth_element(rigid_.begin(),rigid_.begin()+std::ptrdiff_t(tuning.rigidCapacity),rigid_.end(),[](const Rigid& a,const Rigid& b){return a.age<b.age;});
            rigid_.resize(tuning.rigidCapacity);std::sort(rigid_.begin(),rigid_.end(),detail::before<Rigid>);}
        if(radius){ /* this frame's located bodies; unmatched ones survive reserveAge frames (capture gaps) */
            nextRadius_.clear();bool self=false;for(const auto& a:actors)self=self||a.self;
            // Keyed by the smallest key seen: a partial capture only raises an actor's key.
            for(const auto& a:actors){if(a.rigid||a.body!=SIZE_MAX||a.orphan||!a.located)continue;RadiusEntry r;r.key=a.radiusEntry?std::min(a.key,a.radiusEntry->key):a.key;std::memcpy(r.at,a.at,sizeof r.at);
                r.inside=!a.outside&&!a.unfiltered;r.filtered=!a.unfiltered;r.since=a.radiusSince;const auto& h=a.head!=SIZE_MAX?actors[a.head]:a;r.companion=h.companion;r.streak=h.streak;r.self=a.self;nextRadius_.push_back(r);} /* sections carry the character's companion flag and streak; the self is its head */
            for(const auto& r:radius_)if(!r.used&&r.age<tuning.reserveAge){nextRadius_.push_back(r);auto& e=nextRadius_.back();++e.age;e.since+=e.since<~0u;e.streak=0;e.self=e.self&&!self&&selfHold_>0;} /* one self: an absent one keeps the flag only while none is present */
            std::sort(nextRadius_.begin(),nextRadius_.end(),detail::before<RadiusEntry>);radius_.swap(nextRadius_);}
        if(!ranked){keptAll();return;}
        std::sort(next_.begin(),next_.end(),detail::before<Entry>);
        entries_.swap(next_);allKept_=false;
    }
};
namespace detail {
// ActorShadowRadius decisions for body actors (see Radius). origin: the pivot.
// move: the camera's translation since the last selection (moved = its length).
// Characters: the game draws one character as several constant groups (bone
// palette sections: ~6 per player, companions=5 around a lone player in every
// 0.3.145 log), each an actor of its own. Identities and hysteresis stay per
// section, the decision is per character: body actors consecutive in capture
// order (attachments between them skipped) whose bounds come within ClusterGap
// of the character so far, up to ClusterExtent across, are one character; a key
// the character already has starts the next one (a stack of one model is
// several characters). Its head is the section with the smallest key; its place
// (centre ray axis, the circle's centre when it is the player) is the bottom
// middle of its bounds (a single section: its root, as before). Every section
// is judged on the character's bounds with its own last state and dwell; the
// character casts if any section does, and every section records that, so a
// character never casts in part, a split or merge can only keep it casting, and
// it counts one change.
inline void radiusStage(const std::vector<Draw>& draws,std::vector<Actor>& actors,History& history,const Radius& radius,const float* origin,const float* move,float moved,const Tuning& tuning,Result& result,bool diagnostics=false){
    auto body=[](const Actor& a){return !a.rigid&&a.body==SIZE_MAX&&!a.orphan;};
    auto usable=[](const Draw& d){return d.known&&std::isfinite(d.distanceSquared)&&d.distanceSquared>=0;};
    auto& boxes=history.radiusBoxes();boxes.assign(actors.size(),Box{});
    {std::size_t head=SIZE_MAX,start=0;
        for(std::size_t n=0;n<actors.size();++n){auto& a=actors[n];if(!body(a))continue;Box& b=boxes[n];
            for(std::size_t i=a.first;i<a.first+a.count;++i)if(usable(draws[i]))b.add(draws[i].at);if(a.located)b.add(a.at);
            bool join=false;
            if(head!=SIZE_MAX&&b.valid()){const Box& c=boxes[head];Box u=c;for(unsigned k=0;k<3;++k){u.low[k]=std::min(u.low[k],b.low[k]);u.high[k]=std::max(u.high[k],b.high[k]);}
                join=c.squared(b)<=ClusterGap*ClusterGap&&u.high[0]-u.low[0]<=ClusterExtent&&u.high[1]-u.low[1]<=ClusterExtent;
                for(std::size_t m=start;join&&m<n;++m)join=!(actors[m].head==head&&actors[m].key==a.key); /* a second section of the same model: the next character */
                if(join){auto& h=actors[head];if(a.located&&(!h.located||a.key<h.key)){boxes[n]=u;a.head=n;for(std::size_t m=start;m<n;++m)if(actors[m].head==head)actors[m].head=n;head=n;}else{boxes[head]=u;a.head=head;}}}
            if(!join){a.head=n;head=b.valid()?n:SIZE_MAX;start=n;}}
        std::vector<std::uint32_t>& sections=history.radiusHits();sections.assign(actors.size(),0);for(const auto& a:actors)if(body(a))++sections[a.head];
        for(std::size_t n=0;n<actors.size();++n){auto& a=actors[n];if(!body(a)||a.head!=n)continue;const Box& b=boxes[n];
            if(b.valid()&&sections[n]>1){a.radiusAt[0]=.5f*(b.low[0]+b.high[0]);a.radiusAt[1]=.5f*(b.low[1]+b.high[1]);a.radiusAt[2]=b.low[2];}else std::memcpy(a.radiusAt,a.at,sizeof a.at);}}
    auto isHead=[&](std::size_t n){return body(actors[n])&&actors[n].head==n;};
    // Identities (sections), nearest pairs first (a crowd of one model cannot
    // trade entries by capture order): the same key where it was; then the same
    // key shifted by the camera's move (a body that moved with the camera: the
    // player at any speed); then any key within IdentityTolerance (its smallest
    // captured draw key changed: a draw missing from this capture), never the
    // self's entry.
    auto& entries=history.radiusEntries();auto& pairs=history.radiusPairs();auto& spare=history.radiusSpare();auto& hits=history.radiusHits();
    bool sorted=false;
    auto assign=[&](int pass,float reach){pairs.clear();
        for(std::size_t n=0;n<actors.size();++n){const auto& a=actors[n];if(!body(a)||!a.located||a.radiusEntry)continue;
            float at[3];for(unsigned k=0;k<3;++k)at[k]=a.at[k]-(pass==1?move[k]:0.f);
            auto visit=[&](std::size_t i){const auto& e=entries[i];if(e.used)return;float d=0;for(unsigned k=0;k<3;++k){const float x=e.at[k]-at[k];d+=x*x;}if(d<=reach*reach)pairs.push_back({d,std::uint32_t(n),std::uint32_t(i)});};
            if(pass<2){auto it=std::lower_bound(entries.begin(),entries.end(),std::make_pair(a.key,at[0]-reach),[](const RadiusEntry& e,const std::pair<std::uint64_t,float>& k){return e.key<k.first||(e.key==k.first&&e.at[0]<k.second);});
                for(;it!=entries.end()&&it->key==a.key&&it->at[0]<=at[0]+reach;++it)visit(std::size_t(it-entries.begin()));}
            else if(!sorted)for(auto i:spare)visit(i);
            else for(auto it=std::lower_bound(spare.begin(),spare.end(),at[0]-reach,[&](std::uint32_t i,float x){return entries[i].at[0]<x;});it!=spare.end()&&entries[*it].at[0]<=at[0]+reach;++it)visit(*it);}
        auto take=[&](const RadiusPair& p){auto& a=actors[p.actor];auto& e=entries[p.entry];if(a.radiusEntry||e.used)return;a.radiusEntry=&e;e.used=true;result.radiusRekeyed+=pass==2;a.rekeyed=diagnostics&&pass==2;};
        auto less=[](const RadiusPair& x,const RadiusPair& y){return x.d<y.d||(x.d==y.d&&(x.actor<y.actor||(x.actor==y.actor&&x.entry<y.entry)));};
        // Greedy nearest-first, without sorting the common case: a pair that is the
        // nearest of both its actor and its entry is in the greedy result anyway.
        hits.assign(entries.size(),~0u);for(std::uint32_t i=0;i<pairs.size();++i){auto& h=hits[pairs[i].entry];if(h==~0u||less(pairs[i],pairs[h]))h=i;}
        for(std::size_t i=0,j;i<pairs.size();i=j){std::size_t k=i;for(j=i+1;j<pairs.size()&&pairs[j].actor==pairs[i].actor;++j)if(less(pairs[j],pairs[k]))k=j;
            if(hits[pairs[k].entry]==k)take(pairs[k]);}
        std::size_t kept=0;for(const auto& p:pairs)if(!actors[p.actor].radiusEntry&&!entries[p.entry].used)pairs[kept++]=p;
        pairs.resize(kept);std::sort(pairs.begin(),pairs.end(),less);for(const auto& p:pairs)take(p);};
    assign(0,IdentityTolerance);if(moved>0)assign(1,IdentityTolerance+SelfGap*std::min(moved,MaxShift));
    std::size_t open=0;for(const auto& a:actors)open+=body(a)&&a.located&&!a.radiusEntry;
    if(open){spare.clear();for(std::size_t i=0;i<entries.size();++i)if(!entries[i].used&&!entries[i].self)spare.push_back(std::uint32_t(i));
        if((sorted=open*spare.size()>4096))std::sort(spare.begin(),spare.end(),[&](std::uint32_t x,std::uint32_t y){return entries[x].at[0]<entries[y].at[0]||(entries[x].at[0]==entries[y].at[0]&&x<y);}); /* few: every pair */
        if(!spare.empty())assign(2,IdentityTolerance);}
    auto& characters=history.radiusCharacters();characters.assign(actors.size(),Character{});
    for(const auto& a:actors)if(body(a)&&a.head!=SIZE_MAX&&a.radiusEntry){auto& c=characters[a.head];const auto& e=*a.radiusEntry;
        c.stay=c.stay||e.companion;c.streak=std::max(c.streak,e.streak);if(e.filtered){(e.inside?c.in:c.out)+=1;c.since=std::min(c.since,e.since);}}
    float f[3]={},length=0;if(radius.eye&&origin){for(unsigned k=0;k<3;++k){f[k]=origin[k]-radius.eye[k];length+=f[k]*f[k];}length=std::sqrt(length);}
    // Candidates (characters) on the centre ray: miss distance to a sampled vertex
    // of any section or to the character's vertical axis (place..place+SelfAxis,
    // where the camera aims), plus a weak pull toward the (possibly stale) pivot.
    // While the camera translates and some candidate moves with it, a candidate
    // that does not is not the player. A character is the current self if any of
    // its sections was; it follows if its head section does.
    std::size_t current=SIZE_MAX,best=SIZE_MAX;float bestScore=INFINITY;
    if(length>1e-3f){for(auto& x:f)x/=length;
        auto& misses=history.radiusMisses();misses.assign(actors.size(),{INFINITY,0.f}); /* squared */
        auto test=[&](std::size_t h,const float* p){float t=0,q=0;for(unsigned k=0;k<3;++k){const float v=p[k]-radius.eye[k];t+=v*f[k];q+=v*v;}
            if(!(t>=SelfMinT&&t<=SelfMaxT))return;const float m=std::max(0.f,q-t*t);if(m<misses[h].first)misses[h]={m,t};};
        // A character whose bounds and axis are everywhere farther than SelfRay from
        // the ray cannot be a candidate: its draws are not tested.
        auto& distant=history.radiusHits();distant.assign(actors.size(),0);
        for(std::size_t n=0;n<actors.size();++n){if(!isHead(n)||!boxes[n].valid())continue;const Box& b=boxes[n];float r=0,t=0,q=0;
            for(unsigned k=0;k<3;++k){const float high=k==2?std::max(b.high[2],actors[n].radiusAt[2]+SelfAxis):b.high[k],c=.5f*(b.low[k]+high),v=c-radius.eye[k];r+=.25f*(high-b.low[k])*(high-b.low[k]);t+=v*f[k];q+=v*v;}
            distant[n]=std::sqrt(std::max(0.f,q-t*t))-std::sqrt(r)>SelfRay*1.01f+.01f;} /* a margin for rounding */
        for(std::size_t n=0;n<actors.size();++n){const auto& a=actors[n];if(!body(a))continue;if(a.radiusEntry&&a.radiusEntry->self&&current==SIZE_MAX)current=a.head;if(!a.known||distant[a.head])continue;
            for(std::size_t i=a.first;i<a.first+a.count;++i)if(usable(draws[i]))test(a.head,draws[i].at);
            if(a.located&&a.head==n)for(unsigned k=0;k<=6;++k){const float p[3]={a.radiusAt[0],a.radiusAt[1],a.radiusAt[2]+SelfAxis*float(k)/6};test(n,p);}}
        std::vector<std::pair<std::size_t,float>>& found=history.candidates();found.clear();bool anyFollows=false;
        for(std::size_t n=0;n<actors.size();++n){auto& a=actors[n];if(!isHead(n)||!(misses[n].first<=SelfRay*SelfRay))continue;
            if(moved>=SelfMove&&a.radiusEntry){float d=0;for(unsigned k=0;k<3;++k){const float x=a.at[k]-a.radiusEntry->at[k]-move[k];d+=x*x;}a.follows=std::sqrt(d)<=SelfFollow*moved;}
            anyFollows=anyFollows||a.follows;a.candidate=true;
            const float stale=(misses[n].second-length)/SelfPivotYards;found.push_back({n,misses[n].first+stale*stale});}
        // An unmatched self (a sudden camera jump: zoom, teleport): the on-ray body with its model key.
        if(current==SIZE_MAX&&history.selfHold()){float key=INFINITY;for(const auto& c:found)if(actors[c.first].key==history.selfKey()&&c.second<key){key=c.second;current=c.first;}}
        for(const auto& c:found){const auto& a=actors[c.first];if(anyFollows&&!a.follows)continue;
            if(best!=SIZE_MAX&&best==current)break; /* the current self wins while eligible */
            if(c.first==current||c.second<bestScore){best=c.first;bestScore=c.second;}}}
    // One self at a time, with hysteresis: the current self stays while it is the
    // best candidate and SelfHold selections after; another body takes over only
    // after being the best candidate for admitFrames consecutive selections.
    // No self (after a reset, map change or teleport, until one is established): no filter.
    const unsigned admit=std::max(1u,tuning.admitFrames),hold=std::max(1u,tuning.frames(SelfHold));
    if(best!=SIZE_MAX)actors[best].streak=characters[best].streak+1;
    std::size_t self=SIZE_MAX;unsigned left=history.selfHold();
    // A companion of the self (its rider or mount) takes over at once while the self is not captured.
    const bool partner=best!=SIZE_MAX&&current==SIZE_MAX&&left&&actors[best].radiusEntry&&actors[best].radiusEntry->companion;
    if(best!=SIZE_MAX&&(best==current||actors[best].streak>=admit||partner)){self=best;left=hold;}
    else if(left){--left;self=current;}
    history.selfHold(left);
    const bool established=left>0;
    float centre[3];
    if(self!=SIZE_MAX){auto& s=actors[self];s.self=true;result.radiusSelf=1;std::memcpy(centre,s.radiusAt,sizeof centre);history.selfAt(centre);history.selfKey(s.key);}
    else if(established){const float* last=history.selfAt();for(unsigned k=0;k<3;++k)centre[k]=last[k]+move[k];history.selfAt(centre);result.radiusSelfMissing=1;} /* capture gap: follows the camera */
    if(!established){result.radiusUnreferenced=1;for(auto& a:actors)if(body(a))a.unfiltered=true;return;} /* nothing is dropped (and nobody counts as inside for the hysteresis) */
    const Box selfBox=self!=SIZE_MAX?boxes[self]:Box{};
    const float yards=std::max(radius.yards,MinYards),margin=band(yards),wide=yards+margin;const unsigned dwell=tuning.frames(RadiusDwell);
    // Heads: the self's mount, passenger, anyone standing on or right next to it (companions).
    for(std::size_t n=0;n<actors.size();++n){if(!isHead(n))continue;auto& a=actors[n];if(a.self||!a.known)continue;const Box& b=boxes[n];
        const bool stay=characters[n].stay;const float root=MinYards+(stay?CompanionBand:0.f),touch=stay?CompanionBand:0.f;
        if(b.valid()&&((selfBox.valid()&&b.squared(selfBox)<=touch*touch)||b.squared(centre)<=root*root)){a.companion=true;++result.radiusCompanions;}}
    // Sections: each by its own last state and dwell on the character's bounds; the
    // head collects whether any section is inside (outside=false) and changed.
    for(auto& a:actors)if(body(a)&&a.head!=SIZE_MAX)actors[a.head].outside=true;
    for(auto& a:actors){if(!body(a)||a.head==SIZE_MAX)continue;auto& h=actors[a.head];const RadiusEntry* e=a.radiusEntry;const bool was=e&&e->inside,hold=e&&e->filtered&&e->since<dwell;
        bool in=h.self||h.companion||!h.known;
        if(!in){const Box& b=boxes[a.head];const float limit=was?(hold?wide+margin:wide):(hold?std::max(0.f,yards-margin):yards);in=b.valid()&&b.squared(centre)<=limit*limit;}
        if(in)h.outside=false;}
    if(diagnostics){std::memcpy(result.radiusCentre,centre,sizeof centre);result.radiusYards=yards;result.radiusCentreKnown=true;
        for(std::size_t n=0;n<actors.size();++n){if(!isHead(n)||!actors[n].outside)continue;const Box& b=boxes[n];const float q=b.valid()?b.squared(centre):-1.f;
            if(b.valid()&&q>yards*yards)continue;++result.nearOutsideTotal;if(result.nearOutsideCount>=Result::NearOutsideMax)continue;
            const RadiusEntry* e=actors[n].radiusEntry;const bool was=e&&e->inside,hold=e&&e->filtered&&e->since<dwell;
            auto& o=result.nearOutside[result.nearOutsideCount++];std::memcpy(o.at,actors[n].radiusAt,sizeof o.at);o.yards=q<0?-1.f:std::sqrt(q);
            o.limit=was?(hold?wide+margin:wide):(hold?std::max(0.f,yards-margin):yards);o.was=was;o.hold=hold;o.sections=characters[n].in+characters[n].out>1;}}
    for(auto& a:actors){if(!body(a)||a.head==SIZE_MAX)continue;const auto& h=actors[a.head];const RadiusEntry* e=a.radiusEntry;
        a.outside=h.outside;if(&a!=&h)a.companion=h.companion||h.self;
        const bool changed=e&&e->filtered&&e->inside==a.outside;a.radiusSince=changed?0:e?e->since+(e->since<~0u):~0u;result.radiusRekeyFlips+=a.rekeyed&&changed;} /* a.rekeyed is set on diagnostics frames only */
    // One change per character: its sections were mostly one way last selection and are the other now.
    for(std::size_t n=0;n<actors.size();++n){if(!isHead(n))continue;const auto& c=characters[n];
        const bool changed=actors[n].outside?c.in>c.out:c.out>c.in;result.radiusToggles+=changed;result.radiusFlicker+=changed&&c.since<dwell;++result.radiusCharacters;}
    for(auto& a:actors)if(body(a)&&a.outside){++result.radiusDropped;result.radiusDroppedDraws+=a.count;result.radiusDroppedBytes+=a.bytes;}
    // Free rigid actors (still rigid after the classification: probation, settled) are never judged by the radius and always cast:
    // those beyond radius+band of the centre cast a shadow their body (if captured at all) does not.
    if(diagnostics)result.beyondKnown=true;
    if(diagnostics)for(const auto& a:actors){if(!a.located)continue;
        const bool change=a.toFree||a.toAttach,gauge=a.rigid&&a.keep;if(!change&&!gauge)continue;
        float d=0;for(unsigned k=0;k<3;++k){const float x=a.at[k]-centre[k];d+=x*x;}if(!(d>wide*wide))continue;
        result.attachToFreeBeyond+=a.toFree;result.freeToAttachBeyond+=a.toAttach;if(!gauge)continue;
        if(a.everAttached){++result.freeRigidBeyond;result.freeRigidBeyondBytes+=a.bytes;result.freeRigidMaxYards=std::max(result.freeRigidMaxYards,std::sqrt(d));result.stuckBeyond+=a.settled;}
        else{++result.propRigidBeyond;result.propRigidBeyondBytes+=a.bytes;}}
}
}
// draws: capture order. Writes draws[i].keep.
// origin: rank by distance from this point (the player/shadow pivot) instead
// of the camera, so orbiting the camera does not reorder the cut.
// radius: optional ActorShadowRadius (null or yards 0: the 0.3.144 selection).
inline Result choose(std::vector<Draw>& draws,Scratch& scratch,std::size_t budget,History& history,bool diagnostics=false,
    const float* origin=nullptr,const Tuning& tuning=Tuning{},const Radius* radius=nullptr){
    auto& actors=scratch.actors;auto& order=scratch.order;auto& points=scratch.points;auto& links=scratch.links;
    Result result;actors.clear();order.clear();points.clear();links.clear();
    for(std::size_t i=0;i<draws.size();){
        Actor a;a.first=i;a.key=draws[i].key;a.rigid=true;const float bone=draws[i].bone;
        std::size_t j=i;
        for(;j<draws.size()&&draws[j].group==draws[i].group;++j){const auto& d=draws[j];a.bytes+=d.bytes;
            a.rigid=a.rigid&&d.rigid&&d.bone==bone;
            const bool known=d.known&&std::isfinite(d.distanceSquared)&&d.distanceSquared>=0;
            float q=d.distanceSquared;if(known&&origin){q=0;for(unsigned k=0;k<3;++k){const float t=d.at[k]-origin[k];q+=t*t;}}
            if(known&&(!a.known||q<a.distanceSquared))a.distanceSquared=q;
            if(known&&!a.located){a.located=true;std::memcpy(a.at,d.at,sizeof a.at);}
            if(tuning.stableIdentity&&(d.key<a.key||j==i)){a.key=d.key;if(known){a.located=true;std::memcpy(a.at,d.at,sizeof a.at);}}
            a.known=a.known||known;}
        // Stable identity: the smallest draw key; its position when it has one.
        if(tuning.stableIdentity&&a.located)for(std::size_t k=i;k<j;++k){const auto& d=draws[k];
            if(d.key==a.key&&d.known&&std::isfinite(d.distanceSquared)&&d.distanceSquared>=0){std::memcpy(a.at,d.at,sizeof a.at);break;}}
        std::memcpy(a.vertexAt,a.at,sizeof a.at);
        // Per-instance anchor: the palette root. Identical instances of one model
        // share every draw key; their roots differ, and waving parts do not move it.
        // A root far from the actor's own vertices means the palette layout is not
        // the proven one: keep the vertex sample then.
        if(tuning.stableIdentity&&!a.rigid&&a.located)for(std::size_t k=i;k<j;++k)if(draws[k].hasRoot){ /* rigid actors keep vertex samples: co-movement of attachments */
            float q=0;for(unsigned c=0;c<3;++c){const float t=draws[k].root[c]-a.vertexAt[c];q+=t*t;}
            if(q<=RootReach*RootReach){std::memcpy(a.at,draws[k].root,sizeof a.at);a.rooted=true;}else ++result.rootRejected;break;}
        a.count=j-i;actors.push_back(a);i=j;
    }
    // Unlocated rigid actors cannot be classified: rank them.
    for(auto& a:actors)if(a.rigid&&!a.located)a.rigid=false;
    // ActorShadowRadius: the camera's translation since the last selection (the
    // camera follows the player). An identity not found where it was is looked
    // for shifted by that move (within IdentityTolerance+SelfGap*|move|), so what
    // moves with the player (its weapons, a mount, companions in fast flight,
    // across a capture gap) keeps its identity and decisions.
    const bool radiusOn=radius&&radius->active();float move[3]={},moved=0;
    if(radiusOn&&radius->eye){if(const float* last=history.lastEye())for(unsigned k=0;k<3;++k){move[k]=radius->eye[k]-last[k];moved+=move[k]*move[k];}history.eye(radius->eye);moved=std::sqrt(moved);}
    // Pass 1, in actor order: identities (marking entries used exactly as a
    // single pass would). A settled free-standing actor that has not moved needs
    // no body search (the common fence); only diagnostics count bodies near it.
    bool anySearch=false;
    for(auto& a:actors){if(!a.rigid)continue;auto* e=history.findRigid(a.key,a.at);
        if(!e&&moved>0){float at[3];for(unsigned k=0;k<3;++k)at[k]=a.at[k]-move[k];e=history.findRigid(a.key,at,IdentityTolerance+SelfGap*std::min(moved,MaxShift));
            if(e&&e->settled&&!e->attachment)e=nullptr;} /* a settled prop never moves with the camera: a same-model neighbour's entry */if(e)e->used=true;a.entry=e;result.rigidNew+=!e;
        float m=0;if(e&&e->settled)for(unsigned k=0;k<3;++k){const float x=a.at[k]-e->anchor[k];m+=x*x;}
        a.search=!(e&&e->settled&&!(m>tuning.settledMove*tuning.settledMove))||diagnostics;anySearch=anySearch||a.search;}
    // Every sampled vertex of every non-rigid actor in a reach-sized xy grid.
    const float reach=AttachRadius*KeepReach;
    auto cellOf=[&](float x,float y,int dx,int dy){
        const auto cx=std::uint32_t(std::int32_t(std::floor(x/reach))+dx),cy=std::uint32_t(std::int32_t(std::floor(y/reach))+dy);
        return std::uint64_t(cx)<<32|cy;};
    // A draw that reused its predecessor's sample (same group, shader and
    // declaration) adds the same point again: skipped, the searches are unchanged.
    if(anySearch)for(std::size_t n=0;n<actors.size();++n){const auto& a=actors[n];if(a.rigid)continue;
        for(std::size_t i=a.first;i<a.first+a.count;++i){const auto& d=draws[i];
            if(!(d.known&&std::isfinite(d.distanceSquared)&&d.distanceSquared>=0))continue;
            if(!points.empty()){const auto& p=points.back();if(p.actor==n&&p.x==d.at[0]&&p.y==d.at[1]&&p.z==d.at[2])continue;}
            points.push_back({cellOf(d.at[0],d.at[1],0,0),d.at[0],d.at[1],d.at[2],n});}}
    std::sort(points.begin(),points.end(),[](const Point& a,const Point& b){return a.cell<b.cell||(a.cell==b.cell&&(a.x<b.x||(a.x==b.x&&a.actor<b.actor)));});
    for(auto& a:actors){if(!a.rigid)continue;float best=reach*reach;std::size_t inReach=SIZE_MAX,same=SIZE_MAX;
        auto* e=a.entry;const std::size_t linked=links.size();
        // Nearest body over all draws of the rigid actor; ties go to the earlier
        // actor. An attachment stays with last frame's body while it is in reach.
        // "same" is last frame's body: the first rigid draw that sees it wins, and
        // within that draw the point with the smallest (x, actor).
        if(a.search)for(std::size_t i=a.first;i<a.first+a.count;++i){const auto& d=draws[i];
            if(!(d.known&&std::isfinite(d.distanceSquared)&&d.distanceSquared>=0))continue;
            const Point* sameAt=nullptr;
            // The three cells of a column are adjacent in the sorted points (one search)
            // unless the row index wraps; the visiting order is the cell-by-cell order.
            for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy){const auto cell=cellOf(d.at[0],d.at[1],dx,dy),last=cellOf(d.at[0],d.at[1],dx,1);
                const bool column=dy==-1&&cell<last;if(dy>-1&&cellOf(d.at[0],d.at[1],dx,-1)<last)break;
                auto it=std::lower_bound(points.begin(),points.end(),cell,[](const Point& p,std::uint64_t c){return p.cell<c;});
                for(;it!=points.end()&&(column?it->cell<=last:it->cell==cell);++it){const float x=it->x-d.at[0],y=it->y-d.at[1],z=it->z-d.at[2],q=x*x+y*y+z*z;
                    if(q>reach*reach)continue;
                    if(q<=AttachRadius*AttachRadius&&(links.size()==linked||links.back().second!=it->actor))links.push_back({std::size_t(&a-actors.data()),it->actor});
                    if(e&&e->attachment&&same==SIZE_MAX&&actors[it->actor].key==e->bodyKey&&actors[it->actor].located&&
                       (!sameAt||it->x<sameAt->x||(it->x==sameAt->x&&it->actor<sameAt->actor))){float b=0,c=0; /* c: moved with the camera (radius only) */
                        for(unsigned k=0;k<3;++k){const float t=actors[it->actor].at[k]-e->bodyAt[k];b+=t*t;c+=(t-move[k])*(t-move[k]);}
                        if(b<=IdentityTolerance*IdentityTolerance||(moved>0&&c<=IdentityTolerance*IdentityTolerance))sameAt=&*it;}
                    if(q<best||(q==best&&inReach!=SIZE_MAX&&it->actor<inReach)){best=q;inReach=it->actor;}}}
            if(sameAt)same=sameAt->actor;}
        a.rigidTracked=true;std::memcpy(a.anchor,a.at,sizeof a.anchor);
        const std::size_t tight=best<=AttachRadius*AttachRadius?inReach:SIZE_MAX; /* new attachments need the tight radius */
        auto moved=[&](const float* anchor,float limit){float m=0;for(unsigned k=0;k<3;++k){const float x=a.at[k]-anchor[k];m+=x*x;}return m>limit*limit;};
        bool follow=false;
        if(e&&e->attachment){ /* confirmed: keep the body; free only after FreeFrames without one */
            if(same!=SIZE_MAX)inReach=same;else a.rebound=true;a.previous=e->kept;a.free=inReach!=SIZE_MAX?0:e->free+1;
            a.attachment=a.free<tuning.frames(FreeFrames);follow=a.attachment;if(!a.attachment)a.settled=true;}
        else if(e&&e->settled){ /* free-standing: only real movement beside a body re-attaches */
            a.settled=true;std::memcpy(a.anchor,e->anchor,sizeof a.anchor);
            if(!moved(e->anchor,tuning.settledMove))result.rigidStuck+=inReach!=SIZE_MAX;
            else if(tight!=SIZE_MAX){a.settled=false;a.attachment=follow=true;inReach=tight;}
            else std::memcpy(a.anchor,a.at,sizeof a.anchor);}
        else{ /* probation: attach on co-movement beside a body; follow it until still for ProbationStill frames */
            if(e)std::memcpy(a.anchor,e->anchor,sizeof a.anchor);a.free=e?e->free+1:1;inReach=tight;
            a.still=e?e->still+1:0;
            if(tight!=SIZE_MAX){a.attachment=e&&moved(e->anchor,CoMoveTolerance);follow=a.attachment||a.still<tuning.frames(ProbationStill)||tuning.probationFollow;if(follow)a.previous=e&&e->kept;}
            if(!a.attachment&&a.free>=tuning.frames(FreeFrames))a.settled=true;
            if(a.attachment)a.free=0;}
        a.everAttached=a.attachment||(e&&(e->everAttached||e->attachment)); /* sticky in the history, also off diagnostics: a rigid actor that has ever had a body */
        if(e&&diagnostics){a.toFree=e->attachment&&!a.attachment;a.toAttach=!e->attachment&&a.attachment;result.attachToFree+=a.toFree;result.freeToAttach+=a.toAttach;} /* diagnostics only */
        if(!follow){links.resize(linked);continue;}
        a.rigid=false;
        if(inReach!=SIZE_MAX){a.body=inReach;a.bodyKey=actors[inReach].key;std::memcpy(a.bodyAt,actors[inReach].at,sizeof a.bodyAt);++result.attached;result.attachedBytes+=a.bytes;}
        else{a.orphan=true;++result.orphans;}}
    // ActorShadowRadius: before the quota, so a body outside is never ranked.
    bool ranked=!radiusOn||radius->ranked;
    if(radiusOn){detail::radiusStage(draws,actors,history,*radius,origin,move,moved,tuning,result,diagnostics);
        std::size_t total=0;for(const auto& d:draws)total+=d.bytes;result.radiusInsideBytes=total-result.radiusDroppedBytes;
        if(radius->decide)ranked=budget&&history.shouldRank(result.radiusInsideBytes,budget,tuning);}
    result.ranked=ranked;
    auto finish=[&]{
        for(auto& a:actors){if(a.body!=SIZE_MAX)a.keep=actors[a.body].keep;if(a.orphan)a.keep=a.previous;
            // Radius: an attachment that lost its body (a capture gap) or binds to
            // another model, and so lands on the other side of the player's group (the player's weapon on a neighbour, a
            // neighbour's weapon on the player) holds its last decision and keeps
            // looking for its own body (whose identity it keeps).
            if(radiusOn&&a.body!=SIZE_MAX){a.selfBody=actors[a.body].self||actors[a.body].companion;
                const bool other=a.rebound||(a.entry&&a.entry->bodyKey&&a.bodyKey!=a.entry->bodyKey); /* lost its body, or bound to another model */
                if(other&&a.selfBody!=a.entry->selfBody){a.keep=a.entry->kept;a.selfBody=a.entry->selfBody;a.bodyKey=a.entry->bodyKey;std::memcpy(a.bodyAt,a.entry->bodyAt,sizeof a.bodyAt);}}}
        // Radius: an attachment within AttachRadius of the player's group casts if it
        // is nearer that group than its own body and is new or was the group's (a
        // mis-bound weapon of the player; not a neighbour's weapon, which would
        // cast without its body), or is 4x nearer the group than its body (a weapon
        // of the player kept by an escort riding along after a gap of rider and mount).
        if(radiusOn){auto gap=[&](const Actor& x,const Actor& b){float best=INFINITY;
                for(std::size_t i=x.first;i<x.first+x.count;++i)for(std::size_t j=b.first;j<b.first+b.count;++j){const auto& p=draws[i];const auto& q=draws[j];
                    if(!(p.known&&q.known&&std::isfinite(p.distanceSquared)&&std::isfinite(q.distanceSquared)))continue;float d=0;for(unsigned k=0;k<3;++k){const float t=p.at[k]-q.at[k];d+=t*t;}best=std::min(best,d);}
                return best;};
            for(const auto& l:links){auto& a=actors[l.first];const auto& b=actors[l.second];
                if(a.body==SIZE_MAX||a.selfBody||!(b.self||b.companion))continue;const float toGroup=gap(a,b),toBody=gap(a,actors[a.body]);
                if(toGroup<toBody&&(!a.entry||a.entry->selfBody||16*toGroup<toBody)){a.nearSelf=a.selfBody=true;a.keep=true;}}}
        // Two bodies in reach (a crowd): an attachment casts only if every one of
        // them does, unless it belongs to or is near the player's group (radius).
        for(const auto& l:links){auto& a=actors[l.first];if(a.body!=SIZE_MAX&&!actors[l.second].keep&&!a.selfBody&&!a.nearSelf)a.keep=false;}
        for(const auto& a:actors)for(std::size_t i=a.first;i<a.first+a.count;++i){auto& d=draws[i];d.keep=a.keep;
            if(a.rigid)continue;
            if(a.orphan||a.body!=SIZE_MAX){if(a.keep){++result.rigidDraws;result.rigidBytes+=d.bytes;}continue;} /* rigid bytes stay outside the quota */
            if(a.exempt)continue; /* stationary: outside the quota, counted in exempt* */
            if(a.keep){++result.kept;result.keptBytes+=d.bytes;}else{++result.dropped;result.droppedBytes+=d.bytes;}}};
    if(!ranked){ /* radius only, no quota this frame: every body inside casts */
        for(auto& a:actors){if(a.rigid){++result.rigidActors;result.rigidDraws+=a.count;result.rigidBytes+=a.bytes;continue;}
            if(a.body!=SIZE_MAX||a.orphan)continue;
            ++result.actors;result.unknown+=!a.known;result.rooted+=a.rooted;result.rootFallback+=a.located&&!a.rooted;a.keep=!a.outside;result.actorsKept+=a.keep;}
        finish();history.publish(actors,INFINITY,tuning,false,true);return result;}
    const bool allKept=history.allKept();const float scale=(1-tuning.margin)*(1-tuning.margin);
    std::vector<std::size_t>& candidates=scratch.candidates;candidates.clear();
    for(std::size_t n=0;n<actors.size();++n){auto& a=actors[n];
        if(a.rigid){++result.rigidActors;result.rigidDraws+=a.count;result.rigidBytes+=a.bytes;continue;}
        if(a.body!=SIZE_MAX||a.orphan)continue;
        if(a.outside){a.keep=false;if(!allKept&&a.located)if(auto* e=history.find(a.key,a.at))e->used=true;continue;} /* radius: never ranked, charged or reserved */
        ++result.actors;result.unknown+=!a.known;result.rooted+=a.rooted;result.rootFallback+=a.located&&!a.rooted;
        decltype(history.find(0,a.at)) e=nullptr;
        if(allKept)a.previous=true;
        else if(a.located)if((e=history.find(a.key,a.at))){e->used=true;a.matched=true;a.previous=e->kept;a.pending=e->pending;++result.matched;a.sinceChange=e->sinceChange;}
        a.locked=tuning.rateFrames&&a.matched&&a.sinceChange<tuning.rateFrames;result.locked+=a.locked;
        if(tuning.stationary&&a.located){
            // World samples: the sampled vertex of up to four draws, plus two
            // extra vertices of each of the first two draws when provided.
            // Rooted: the palette root is the one pose sample (waving parts ignored).
            // plus the vertex sample with a loose 2 yd tolerance: a root that stays put
            // while the object walks away (an unproven palette layout) cannot exempt it.
            if(a.rooted){std::memcpy(a.samples[0],a.at,sizeof a.at);std::memcpy(a.samples[1],a.vertexAt,sizeof a.vertexAt);a.mask=3;}
            // Stable identity orders the sampled draws by key (issue order varies).
            std::size_t slots[4];unsigned used=0;
            if(!a.rooted)for(std::size_t i=a.first;i<a.first+a.count;++i){
                if(!tuning.stableIdentity){if(used<4)slots[used++]=i;continue;}
                unsigned at=used<4?used:4;while(at&&draws[slots[at-1]].key>draws[i].key){if(at<4)slots[at]=slots[at-1];--at;}
                if(at<4){slots[at]=i;if(used<4)++used;}}
            if(!a.rooted)for(unsigned k=0;k<used;++k){const auto& d=draws[slots[k]];
                if(d.known&&std::isfinite(d.distanceSquared)&&d.distanceSquared>=0){std::memcpy(a.samples[k],d.at,sizeof d.at);a.mask|=1u<<k;}
                if(k<(tuning.stableIdentity?4u:2u))for(unsigned x=0;x<d.extras&&x<2;++x){std::memcpy(a.samples[4+2*k+x],d.extra[x],sizeof d.extra[x]);a.mask|=1u<<(4+2*k+x);}}
            if(e){const unsigned common=a.mask&e->mask;bool within=(common&15)!=0;
                for(unsigned k=0;k<Samples&&within;++k)if(common&(1u<<k)){float q=0;for(unsigned c=0;c<3;++c){const float t=a.samples[k][c]-e->samples[k][c];q+=t*t;}const float tolerance=a.rooted&&k==1?LooseTolerance:StationaryTolerance;within=q<=tolerance*tolerance;}
                if(within){a.moving=0;a.stillFrames=e->stillFrames+1;a.stillExtras=(common&0xff0)?e->stillExtras+1:0;
                    for(unsigned k=0;k<Samples;++k)if(common&(1u<<k))std::memcpy(a.samples[k],e->samples[k],sizeof a.samples[k]);
                    a.exempt=e->exempt;}
                else{a.moving=e->moving+1;
                    if(e->exempt&&a.moving<tuning.frames(ExemptMoveFrames)){std::memcpy(a.samples,e->samples,sizeof a.samples);a.mask=e->mask;a.stillFrames=e->stillFrames;a.stillExtras=e->stillExtras;a.exempt=true;}}}
            a.wasExempt=a.exempt;a.exemptCandidate=a.exempt||(a.stillFrames>=tuning.frames(StationaryFrames)&&(a.rooted||a.stillExtras>=tuning.frames(StationaryFrames)/2));
            result.stillRankedSmall+=a.stillFrames>=tuning.frames(StationaryFrames)&&!a.exemptCandidate&&a.count<=2;}
        a.exempt=false;
        if(a.exemptCandidate){candidates.push_back(n);continue;}
        a.frozen=tuning.stationary&&a.matched&&a.stillFrames>=tuning.frames(ProbationStill); /* still, not (yet) exempt */
        a.rank=a.previous?a.distanceSquared*(a.frozen?scale*scale:scale):a.distanceSquared;order.push_back(n);
    }
    if(!candidates.empty()){
        // Exempt bytes (free-standing rigid + stationary) stay under
        // exemptFraction of the budget: nearest stationary actors first, the rest
        // are ranked like any actor.
        // Already-exempt actors keep their state even if rigid bytes in view grew
        // (a moving cap boundary would flip them just like the quota cut did);
        // newly stationary ones fill the remaining room nearest first.
        std::sort(candidates.begin(),candidates.end(),[&](std::size_t x,std::size_t y){const auto& a=actors[x];const auto& b=actors[y];
            if(a.wasExempt!=b.wasExempt)return a.wasExempt;
            if(a.distanceSquared!=b.distanceSquared)return a.distanceSquared<b.distanceSquared;return a.first<b.first;});
        const double cap=double(budget)*tuning.exemptFraction;double exempt=(tuning.sharedWithRigid?double(result.rigidBytes):0.)+double(history.exemptReserved());
        for(auto n:candidates){auto& a=actors[n];
            if(a.wasExempt||exempt+double(a.bytes)<=cap){ /* the cap bounds new exemptions; granted ones stay */exempt+=double(a.bytes);a.exempt=true;a.keep=true;++result.exemptActors;result.exemptDraws+=a.count;result.exemptBytes+=a.bytes;result.exemptNpcLike+=a.count>=3;
                result.toggles+=a.matched&&!a.previous;}
            else{++result.cappedExempt;result.exemptCapBinds=1;a.frozen=a.matched;a.rank=a.previous?a.distanceSquared*scale*scale:a.distanceSquared;order.push_back(n);}}}
    if(!allKept)for(auto n:order)if(!actors[n].matched&&actors[n].located)history.claim(actors[n].key,actors[n].at);
    // Admission delay applies only near the cut: unlocated actors keep their first
    // claim and actors well inside the previous cut need no identity at all.
    const float inner=InnerCut*InnerCut*(tuning.rateFrames?history.steadyCut():history.lastCut());
    // Tiers: unknown distance (first claim), then every actor inside the inner
    // cut (near shadows are guaranteed, locked or not), then actors kept and
    // locked by the rate limit (nearest first), then the rest by rank. A full
    // budget therefore drops free actors before locked ones, but never a near
    // actor for a far locked one.
    const bool rateInner=tuning.rateFrames!=0;
    std::sort(order.begin(),order.end(),[&](std::size_t x,std::size_t y){const auto& a=actors[x];const auto& b=actors[y];
        if(a.known!=b.known)return !a.known;
        if(rateInner){const bool ia=a.distanceSquared<=inner,ib=b.distanceSquared<=inner;if(ia!=ib)return ia;if(ia&&a.rank!=b.rank)return a.rank<b.rank;}
        const bool la=a.locked&&a.previous,lb=b.locked&&b.previous;
        if(la!=lb)return la;
        if(la&&a.distanceSquared!=b.distanceSquared)return a.distanceSquared<b.distanceSquared;
        if(a.known&&a.rank!=b.rank)return a.rank<b.rank;
        return a.first<b.first;});
    const std::size_t reserve=allKept?0:std::min(budget,history.reserved(tuning.reserveAge));
    // With the rate limit, unknown and inner-cut actors are charged before the
    // reservation of absent actors (a turn into a new crowd must not lose its
    // near shadows to the crowd left behind); everyone else after it.
    bool reserving=!rateInner;std::size_t used=reserving?reserve:0;bool open=true;result.reserved=reserve;
    for(auto n:order){auto& a=actors[n];
        if(!reserving&&a.known&&!(a.distanceSquared<=inner)){reserving=true;used=std::min(budget,used+reserve);}
        const bool fits=open&&a.bytes<=budget-used;
        if(!fits){a.keep=false;a.pending=0;open=false;}
        else if(a.locked&&!a.previous&&!(a.distanceSquared<=inner)){a.keep=false;a.waiting=true;++result.waiting;} /* dropped recently: stays dropped for rateFrames unless inside the inner cut */
        else if(!a.previous&&a.located&&!(a.distanceSquared<=inner)&&a.pending+1<(a.frozen?3*tuning.admitFrames:tuning.admitFrames)){ /* a still dropped actor needs room three times as long */a.keep=false;a.waiting=true;++a.pending;++result.waiting;}
        else{a.keep=true;a.pending=0;used+=a.bytes;result.admitted+=!a.previous;}
        if(a.keep){++result.actorsKept;result.retained+=a.previous;}
        result.toggles+=(allKept||a.matched)&&a.keep!=a.previous;
    }
    finish();
    float cut=0;bool cutting=false;
    for(auto n:order){const auto& a=actors[n];cutting=cutting||!a.keep;if(a.keep&&a.known)cut=std::max(cut,a.distanceSquared);}
    history.publish(actors,cutting?cut:INFINITY,tuning,true,radiusOn);
    return result;
}
// Trial: the game merges several instances of one rigid model into one draw (Dalaran's lamp posts: 972, 1458 or 1944
// triangles for two, three or four posts). Each vertex is then on exactly one bone, but the bones differ between
// vertices. Such a draw is rigid too: SeveralRigid, which is no palette index, so the rigid memory and the static M2
// match (both need bone >= 0) leave it alone. Without this the draw is a character-like body placed at one of its
// posts, and ActorShadowRadius drops all of them when that one is too far.
constexpr float SeveralRigid=-1.f;
// One palette bone for every vertex, or NaN. Weighted lanes must agree and sum
// to one; without weights all four index lanes must agree. read(v,index,weight)
// decodes vertex v and fails closed on anything unreadable.
template<class Read> float rigidBone(std::size_t vertices,bool weighted,Read read){
    float bone=NAN;bool several=false;
    for(std::size_t v=0;v<vertices;++v){float index[4],weight[4]={1,1,1,1},sum=0,own=NAN;
        if(!read(v,index,weight))return NAN;
        for(unsigned lane=0;lane<4;++lane){
            if(weighted){if(!std::isfinite(weight[lane])||weight[lane]<0)return NAN;if(weight[lane]==0)continue;sum+=weight[lane];}
            if(!std::isfinite(index[lane])||(!std::isnan(own)&&index[lane]!=own))return NAN;own=index[lane];}
        if(weighted&&std::fabs(sum-1)>.02f)return NAN;
        if(std::isnan(bone))bone=own;else if(own!=bone)several=true;
    }
    return several?SeveralRigid:bone;
}
}
