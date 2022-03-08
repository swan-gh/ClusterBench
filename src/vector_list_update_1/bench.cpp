//=======================================================================
// Modified by swan-gh and redistributed under MIT License.
//=======================================================================
// Copyright (c) 2014 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include <random>
#include <array>
#include <vector>
#include <list>
#include <algorithm>
#include <deque>
#include <thread>
#include <iostream>
#include <cstdint>
#include <typeinfo>
#include <memory>
#include <set>
#include <unordered_set>

#include "plf_colony.h"

#include "bench.hpp"
#include "policies.hpp"

#include <ClusterVector.h>
#include <ClusterMap.h>

namespace sw
{
    class default_allocator
    {
    public:

        void* allocate(size_t n)
        {
            return _aligned_malloc(n, 8);
        }

        void* allocate(size_t n, size_t alignment, size_t alignmentOffset)
        {
            if ((alignmentOffset % alignment) == 0)
            {
                return _aligned_malloc(n, alignment);
            }

            return NULL;
        }

        void deallocate(void* p, size_t n)
        {
            _aligned_free(p);
        }
    };
}

namespace {

template<typename T>
constexpr bool is_trivial_of_size(std::size_t size){
    return std::is_trivial<T>::value && sizeof(T) == size;
}

template<typename T>
constexpr bool is_non_trivial_of_size(std::size_t size){
    return
            !std::is_trivial<T>::value
        &&  sizeof(T) == size
        &&  std::is_copy_constructible<T>::value
        &&  std::is_copy_assignable<T>::value
        &&  std::is_move_constructible<T>::value
        &&  std::is_move_assignable<T>::value;
}

template<typename T>
constexpr bool is_non_trivial_nothrow_movable(){
    return
            !std::is_trivial<T>::value
        &&  std::is_nothrow_move_constructible<T>::value
        &&  std::is_nothrow_move_assignable<T>::value;
}

template<typename T>
constexpr bool is_non_trivial_non_nothrow_movable(){
    return
            !std::is_trivial<T>::value
        &&  std::is_move_constructible<T>::value
        &&  std::is_move_assignable<T>::value
        &&  !std::is_nothrow_move_constructible<T>::value
        &&  !std::is_nothrow_move_assignable<T>::value;
}

template<typename T>
constexpr bool is_non_trivial_non_movable(){
    return
            !std::is_trivial<T>::value
        &&  std::is_copy_constructible<T>::value
        &&  std::is_copy_assignable<T>::value
        &&  !std::is_move_constructible<T>::value
        &&  !std::is_move_assignable<T>::value;
}

template<typename T>
constexpr bool is_small(){
   return sizeof(T) <= sizeof(std::size_t);
}

} //end of anonymous namespace

// tested types

// trivial type with parametrized size
template<int N>
struct Trivial {
    std::size_t a;
    std::array<unsigned char, N-sizeof(a)> b;
    bool operator<(const Trivial &other) const { return a < other.a; }
};

template<>
struct Trivial<sizeof(std::size_t)> {
    std::size_t a;
    bool operator<(const Trivial &other) const { return a < other.a; }
};

// non trivial, quite expensive to copy but easy to move (noexcept not set)
class NonTrivialStringMovable {
    private:
        std::string data{"some pretty long string to make sure it is not optimized with SSO"};

    public:
        std::size_t a{0};
        NonTrivialStringMovable() = default;
        NonTrivialStringMovable(std::size_t a): a(a) {}
        ~NonTrivialStringMovable() = default;
        bool operator<(const NonTrivialStringMovable &other) const { return a < other.a; }
};

// non trivial, quite expensive to copy but easy to move (with noexcept)
class NonTrivialStringMovableNoExcept {
    private:
        std::string data{"some pretty long string to make sure it is not optimized with SSO"};

    public:
        std::size_t a{0};
        NonTrivialStringMovableNoExcept() = default;
        NonTrivialStringMovableNoExcept(std::size_t a): a(a) {}
        NonTrivialStringMovableNoExcept(const NonTrivialStringMovableNoExcept &) = default;
        NonTrivialStringMovableNoExcept(NonTrivialStringMovableNoExcept &&) noexcept = default;
        ~NonTrivialStringMovableNoExcept() = default;
        NonTrivialStringMovableNoExcept &operator=(const NonTrivialStringMovableNoExcept &) = default;
        NonTrivialStringMovableNoExcept &operator=(NonTrivialStringMovableNoExcept &&other) noexcept {
            std::swap(data, other.data);
            std::swap(a, other.a);
            return *this;
        }
        bool operator<(const NonTrivialStringMovableNoExcept &other) const { return a < other.a; }
};

// non trivial, quite expensive to copy and move
template<int N>
class NonTrivialArray {
    public:
        std::size_t a = 0;

    private:
        std::array<unsigned char, N-sizeof(a)> b;

    public:
        NonTrivialArray() = default;
        NonTrivialArray(std::size_t a): a(a) {}
        ~NonTrivialArray() = default;
        bool operator<(const NonTrivialArray &other) const { return a < other.a; }
};

// type definitions for testing and invariants check
using TrivialSmall   = Trivial<32>;       static_assert(is_trivial_of_size<TrivialSmall>(32),        "Invalid type");
using TrivialMedium  = Trivial<64>;      static_assert(is_trivial_of_size<TrivialMedium>(64),      "Invalid type");
using TrivialLarge   = Trivial<128>;     static_assert(is_trivial_of_size<TrivialLarge>(128),      "Invalid type");
using TrivialHuge    = Trivial<1024>;    static_assert(is_trivial_of_size<TrivialHuge>(1024),      "Invalid type");
using TrivialMonster = Trivial<4*1024>;  static_assert(is_trivial_of_size<TrivialMonster>(4*1024), "Invalid type");

static_assert(is_non_trivial_nothrow_movable<NonTrivialStringMovableNoExcept>(), "Invalid type");
static_assert(is_non_trivial_non_nothrow_movable<NonTrivialStringMovable>(), "Invalid type");

using NonTrivialArrayMedium = NonTrivialArray<32>;
static_assert(is_non_trivial_of_size<NonTrivialArrayMedium>(32), "Invalid type");

template<typename T>
using VectorHandleBench = ContainerBenchType<std::vector<T>, size_t>;
template<typename T>
using ListHandleBench = ContainerBenchType<std::list<T>, typename std::list<T>::iterator >;
template<typename T>
using DequeHandleBench = ContainerBenchType<std::vector<T>, size_t>;
template<typename T>
using ColonyHandleBench = ContainerBenchType<plf::colony<T>, typename plf::colony<T>::iterator >;

template<typename T>
using ClusterVectorHandleBench = ClusterContainerBenchType< sw::cluster_vector<T, sw::default_allocator>, typename sw::cluster_vector<T, sw::default_allocator>::iterator >;
template<typename T>
using ClusterMapHandleBench = ClusterContainerBenchType< sw::cluster_map<T, sw::default_allocator>, typename sw::cluster_map<T, sw::default_allocator>::handle_type >;

// Define all benchmarks

template<typename T>
struct bench_fill_back {
    static void run(){
        const char * testName = "fill_back";
        new_graph<T>(testName, "us");

        auto sizes = { 10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000};
        bench<std::vector<T>, microseconds, Empty, PushBack>(testName, "vector", sizes);
        bench<std::vector<T>, microseconds, Empty, ReserveSize, PushBack>(testName, "vector_reserve", sizes);
        bench<std::list<T>,   microseconds, Empty, PushBack>(testName, "list",   sizes);
        bench<std::deque<T>,  microseconds, Empty, PushBack>(testName, "deque",  sizes);

        bench<plf::colony<T>, microseconds, Empty, InsertSimple>(testName, "colony",  sizes);
        bench<sw::cluster_vector<T, sw::default_allocator>, microseconds, Empty, PushBack>(testName, "cluster_vector",  sizes);
        bench<sw::cluster_map<T, sw::default_allocator>, microseconds, Empty, InsertSimple>(testName, "cluster_map",  sizes);
    }
};

template<typename T>
struct bench_sequential_read {
    static void run(){
        const char * testName = "sequential_read";
        new_graph<T>(testName, "us");

        auto sizes = { 10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000};
        bench<std::vector<T>, microseconds, FilledRandom, IterateRead>(testName, "vector", sizes);
        bench<std::list<T>,   microseconds, FilledRandom, IterateRead>(testName, "list",   sizes);
        bench<std::deque<T>,  microseconds, FilledRandom, IterateRead>(testName, "deque",  sizes);

        bench<plf::colony<T>, microseconds, FilledRandomInsert, IterateRead>(testName, "colony",  sizes);
        bench<sw::cluster_vector<T, sw::default_allocator>, microseconds, FilledRandom, IterateRead>(testName, "cluster_vector",  sizes);
        bench<sw::cluster_map<T, sw::default_allocator>, microseconds, FilledRandomInsert, IterateRead>(testName, "cluster_map",  sizes);
    }
};

template<typename T>
struct bench_sequential_write {
    static void run(){
        const char * testName = "sequential_write";
        new_graph<T>(testName, "us");

        auto sizes = { 10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000};
        bench<std::vector<T>, microseconds, FilledRandom, Write>(testName, "vector", sizes);
        bench<std::list<T>,   microseconds, FilledRandom, Write>(testName, "list",   sizes);
        bench<std::deque<T>,  microseconds, FilledRandom, Write>(testName, "deque",  sizes);

        bench<plf::colony<T>, microseconds, FilledRandomInsert, Write>(testName, "colony",  sizes);
        bench<sw::cluster_vector<T, sw::default_allocator>, microseconds, FilledRandom, Write>(testName, "cluster_vector",  sizes);
        bench<sw::cluster_map<T, sw::default_allocator>, microseconds, FilledRandomInsert, Write>(testName, "cluster_map",  sizes);
    }
};

template<typename T>
struct bench_random_read {
    static void run(){
        const char * testName = "random_read";
        new_graph<T>(testName, "us");

        auto sizes = { 10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000};
        bench<VectorHandleBench<T>, microseconds, FilledRandomIntegerIndexable, RandomReadIntegerIndex>(testName, "vector", sizes);
        bench<ListHandleBench<T>,   microseconds, FilledRandomIterators, RandomReadIterators>(testName, "list", sizes);
        bench<DequeHandleBench<T>,  microseconds, FilledRandomIntegerIndexable, RandomReadIntegerIndex>(testName, "deque", sizes);

        bench<ColonyHandleBench<T>, microseconds, FilledRandomColony, RandomReadIterators>(testName, "colony", sizes);
        bench<ClusterVectorHandleBench<T>, microseconds, FilledRandomClusterVector, RandomReadIterators>(testName, "cluster_vector", sizes);
        bench<ClusterMapHandleBench<T>, microseconds, FilledRandomClusterMap, RandomReadClusterMap>(testName, "cluster_map",  sizes);
    }
};

template<typename T>
struct bench_random_write {
    static void run(){
        const char * testName = "random_write";
        new_graph<T>(testName, "us");

        auto sizes = { 10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000};
        bench<std::vector<T>, microseconds, FilledRandom, Write>(testName, "vector", sizes);
        bench<std::list<T>,   microseconds, FilledRandom, Write>(testName, "list",   sizes);
        bench<std::deque<T>,  microseconds, FilledRandom, Write>(testName, "deque",  sizes);

        bench<plf::colony<T>, microseconds, FilledRandomInsert, Write>(testName, "colony",  sizes);
        bench<sw::cluster_vector<T, sw::default_allocator>, microseconds, FilledRandom, Write>(testName, "cluster_vector",  sizes);
        bench<sw::cluster_map<T, sw::default_allocator>, microseconds, FilledRandomInsert, Write>(testName, "cluster_map",  sizes);
    }
};

//Launch the benchmark

template<typename ...Types>
void bench_all(){
    bench_types<bench_fill_back,        Types...>();
    bench_types<bench_sequential_read,  Types...>();
    bench_types<bench_sequential_write, Types...>();
    bench_types<bench_random_read,      Types...>();
    bench_types<bench_random_write,     Types...>();
}

int main(){
    //Launch all the graphs
    bench_all<
        TrivialSmall,
        TrivialMedium,
        TrivialLarge,
        TrivialHuge,
        NonTrivialArray<32> >();

    //Generate the graphs
    graphs::output(graphs::Output::GOOGLE);

    return 0;
}
