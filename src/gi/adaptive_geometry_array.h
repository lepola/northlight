#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "memory_footprint.h"
#include "temporary_geometry.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <initializer_list>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace NorthlightGI {
template<class T> struct GeometryBlockDelete {
    void operator()(T* data)const{
#ifdef _WIN32
        if(data)VirtualFree(data,0,MEM_RELEASE);
#else
        delete[] data;
#endif
    }
};
template<class T> using GeometryBlock=std::unique_ptr<T[],GeometryBlockDelete<T>>;
template<class T> GeometryBlock<T> geometryBlock(size_t count){
    static_assert(std::is_trivially_destructible_v<T>);
    if(count>SIZE_MAX/sizeof(T))throw std::bad_alloc();
#ifdef _WIN32
    // Exact OS blocks avoid CRT arena/granularity overhead for 512 KiB chunks
    // and return scratch VA immediately when its owner retires.
    T* data=static_cast<T*>(VirtualAlloc(nullptr,count*sizeof(T),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!data)throw std::bad_alloc();GeometryBlock<T> owner(data);if constexpr(std::is_arithmetic_v<T>)std::uninitialized_default_construct_n(data,count);else std::uninitialized_value_construct_n(data,count);return owner;
#else
    return GeometryBlock<T>(new T[count]());
#endif
}
template<class T> class AdaptiveGeometryArray {
    static_assert(std::is_trivially_copyable_v<T>);
    static constexpr size_t BlockSize=512u*1024u/sizeof(T);
    std::vector<T> flat_;std::vector<GeometryBlock<T>> chunks_;size_t size_=0;
    std::unique_ptr<TemporaryGeometryArray<T>> temporary_;
    bool chunked_=false;const std::function<bool(uint64_t)>* admit_=nullptr;
    void block(){if(admit_&&*admit_&&!(*admit_)(1024u*1024u))throw std::bad_alloc();chunks_.push_back(geometryBlock<T>(BlockSize));}
public:
    using value_type=T;
    AdaptiveGeometryArray()=default;
    AdaptiveGeometryArray& operator=(std::initializer_list<T> values){clear();insert(end(),values.begin(),values.end());return *this;}
    template<class I> AdaptiveGeometryArray(I first,I last):flat_(first,last){}
    AdaptiveGeometryArray(const AdaptiveGeometryArray& a){if(a.chunked_){chunked_=true;insert(end(),a.begin(),a.end());}else flat_=a.flat_;}
    AdaptiveGeometryArray& operator=(const AdaptiveGeometryArray& a){if(this!=&a){AdaptiveGeometryArray copy(a);swap(copy);}return *this;}
    AdaptiveGeometryArray(AdaptiveGeometryArray&& a)noexcept:flat_(std::move(a.flat_)),chunks_(std::move(a.chunks_)),size_(std::exchange(a.size_,0)),temporary_(std::move(a.temporary_)),chunked_(std::exchange(a.chunked_,false)),admit_(std::exchange(a.admit_,nullptr)){}
    AdaptiveGeometryArray& operator=(AdaptiveGeometryArray&& a)noexcept{if(this!=&a){AdaptiveGeometryArray moved(std::move(a));swap(moved);}return *this;}
    void chunks(const std::function<bool(uint64_t)>* a){if(chunked_)return;AdaptiveGeometryArray next;next.chunked_=true;next.admit_=a;next.insert(next.end(),flat_.begin(),flat_.end());swap(next);}
    void spool(const std::function<bool(uint64_t)>* admit){if(size())throw std::logic_error("Spooling requires empty scratch geometry");if(admit&&*admit&&!(*admit)(1024u*1024u))throw std::bad_alloc();temporary_=std::make_unique<TemporaryGeometryArray<T>>();chunked_=true;admit_=admit;}
    bool spooled()const{return bool(temporary_);}
    void detachAdmission(){admit_=nullptr;}
    bool chunked()const{return chunked_;}
    static constexpr size_t blockSize(){return BlockSize;}
    void releaseBlock(size_t index){if(!temporary_)chunks_[index].reset();}
    size_t size()const{return temporary_?temporary_->size():chunked_?size_:flat_.size();}bool empty()const{return size()==0;}
    size_t capacity()const{return temporary_?temporary_->capacity():chunked_?chunks_.size()*BlockSize:flat_.capacity();}
    size_t overhead()const{return chunks_.capacity()*sizeof(chunks_[0]);}
    T& operator[](size_t i){return temporary_?(*temporary_)[i]:chunked_?chunks_[i/BlockSize][i%BlockSize]:flat_[i];}
    const T& operator[](size_t i)const{return temporary_?static_cast<const TemporaryGeometryArray<T>&>(*temporary_)[i]:chunked_?chunks_[i/BlockSize][i%BlockSize]:flat_[i];}
    T& front(){return (*this)[0];}const T& front()const{return (*this)[0];}
    T& back(){return (*this)[size()-1];}const T& back()const{return (*this)[size()-1];}
    T* data(){return chunked_?nullptr:flat_.data();}const T* data()const{return chunked_?nullptr:flat_.data();}
    std::vector<T>* flat(){return chunked_?nullptr:&flat_;}const std::vector<T>* flat()const{return chunked_?nullptr:&flat_;}
    void reserve(size_t n){if(!chunked_)flat_.reserve(n);}
    void resize(size_t n){if(temporary_){temporary_->resize(n);return;}if(!chunked_){flat_.resize(n);return;}while(size_<n){if(size_/BlockSize>=chunks_.size())block();const size_t count=std::min(n-size_,BlockSize-size_%BlockSize);std::fill_n(chunks_[size_/BlockSize].get()+size_%BlockSize,count,T{});size_+=count;}size_=n;}
    void assign(size_t n,const T& v){const T copy=v;clear();if(!chunked_){flat_.assign(n,copy);return;}if(temporary_){for(size_t i=0;i<n;++i)push_back(copy);return;}while(size_<n){if(size_/BlockSize>=chunks_.size())block();const size_t count=std::min(n-size_,BlockSize-size_%BlockSize);std::fill_n(chunks_[size_/BlockSize].get()+size_%BlockSize,count,copy);size_+=count;}}
    void push_back(const T& v){if(temporary_){temporary_->push_back(v);return;}if(!chunked_){flat_.push_back(v);return;}if(size_/BlockSize>=chunks_.size())block();chunks_[size_/BlockSize][size_%BlockSize]=v;++size_;}
    template<class... A> T& emplace_back(A&&... a){T v{std::forward<A>(a)...};push_back(v);return (*this)[size()-1];}
    void clear(){if(temporary_)temporary_->clear();else if(chunked_)size_=0;else flat_.clear();}
    void shrink_to_fit(){if(!chunked_)flat_.shrink_to_fit();}
    void free(){temporary_.reset();std::vector<T>().swap(flat_);std::vector<GeometryBlock<T>>().swap(chunks_);size_=0;chunked_=false;admit_=nullptr;}
    void swap(AdaptiveGeometryArray& b){temporary_.swap(b.temporary_);flat_.swap(b.flat_);chunks_.swap(b.chunks_);std::swap(size_,b.size_);std::swap(chunked_,b.chunked_);std::swap(admit_,b.admit_);}
    template<bool Const> class It {
        using Owner=std::conditional_t<Const,const AdaptiveGeometryArray,AdaptiveGeometryArray>;Owner* owner_;size_t i_;
    public:
        using iterator_category=std::random_access_iterator_tag;using value_type=T;using difference_type=std::ptrdiff_t;
        using reference=std::conditional_t<Const,const T&,T&>;using pointer=std::conditional_t<Const,const T*,T*>;
        It():owner_(nullptr),i_(0){}It(Owner* o,size_t i):owner_(o),i_(i){}reference operator*()const{return (*owner_)[i_];}pointer operator->()const{return &**this;}reference operator[](difference_type n)const{return (*owner_)[i_+n];}
        Owner* owner()const{return owner_;}size_t position()const{return i_;}
        It& operator++(){++i_;return *this;}It operator++(int){auto c=*this;++*this;return c;}It& operator--(){--i_;return *this;}It operator--(int){auto c=*this;--*this;return c;}
        It& operator+=(difference_type n){i_+=n;return *this;}It& operator-=(difference_type n){i_-=n;return *this;}It operator+(difference_type n)const{auto c=*this;return c+=n;}friend It operator+(difference_type n,It a){return a+n;}It operator-(difference_type n)const{auto c=*this;return c-=n;}difference_type operator-(It b)const{return difference_type(i_)-difference_type(b.i_);}
        bool operator==(It b)const{return i_==b.i_;}bool operator!=(It b)const{return i_!=b.i_;}bool operator<(It b)const{return i_<b.i_;}bool operator>(It b)const{return i_>b.i_;}bool operator<=(It b)const{return i_<=b.i_;}bool operator>=(It b)const{return i_>=b.i_;}
    };
    using iterator=It<false>;using const_iterator=It<true>;
    iterator begin(){return {this,0};}iterator end(){return {this,size()};}const_iterator begin()const{return {this,0};}const_iterator end()const{return {this,size()};}
    template<class I> void insert(iterator position,I first,I last){
        if(!chunked_){
            if constexpr(std::is_same_v<I,iterator>||std::is_same_v<I,const_iterator>){
                if(!first.owner()->chunked_){flat_.insert(flat_.begin()+position.position(),first.owner()->flat_.begin()+first.position(),last.owner()->flat_.begin()+last.position());return;}
            }
            flat_.insert(flat_.begin()+position.position(),first,last);return;
        }
        if(position.position()!=size())throw std::logic_error("paged insert must append");
        if(temporary_){for(;first!=last;++first)push_back(*first);return;}
        using Category=typename std::iterator_traits<I>::iterator_category;
        if constexpr(std::is_base_of_v<std::forward_iterator_tag,Category>){
            size_t remaining=size_t(std::distance(first,last));
            while(remaining){
                if(size_/BlockSize>=chunks_.size())block();
                size_t count=std::min(remaining,BlockSize-size_%BlockSize);T* destination=chunks_[size_/BlockSize].get()+size_%BlockSize;
                if constexpr(std::is_same_v<I,iterator>||std::is_same_v<I,const_iterator>){
                    const auto* source=first.owner();
                    if(!source->temporary_){
                        if(source->chunked_){count=std::min(count,BlockSize-first.position()%BlockSize);std::copy_n(source->chunks_[first.position()/BlockSize].get()+first.position()%BlockSize,count,destination);}
                        else std::copy_n(source->flat_.data()+first.position(),count,destination);
                    }else std::copy_n(first,count,destination);
                }else std::copy_n(first,count,destination);
                std::advance(first,count);size_+=count;remaining-=count;
            }
        }else for(;first!=last;++first)push_back(*first);
    }
    void accountMemory(NorthlightMemoryFootprint::Counter& m,NorthlightMemoryFootprint::Kind k)const{if(temporary_)temporary_->accountMemory(m,k);else if(!chunked_)m.vector(k,flat_);else{m.vector(NorthlightMemoryFootprint::Metadata,chunks_);for(const auto& b:chunks_)m.allocation(k,b.get(),BlockSize*sizeof(T));}}
};
}
