//=======================================================================
// Modified by swan-gh and redistributed under MIT License.
//=======================================================================
// Copyright (c) 2014 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include <chrono>

#include "graphs.hpp"
#include "demangle.hpp"

// chrono typedefs

using std::chrono::milliseconds;
using std::chrono::microseconds;

using Clock = std::chrono::high_resolution_clock;

// Number of repetitions of each test

static const std::size_t REPEAT = 20;

// variadic policy runner

template<class Container>
inline static void run(Container &, std::size_t){
    //End of recursion
}

template<template<class> class Test, template<class> class ...Rest, class Container>
inline static void run(Container &container, std::size_t size){
    Test<Container>::run(container, size);
    run<Rest...>(container, size);
}

// benchmarking procedure

template<typename Container,
         typename DurationUnit,
         template<class> class CreatePolicy,
         template<class> class ...TestPolicy>
void bench(const std::string& test_name, const std::string& type, const std::initializer_list<int> &sizes){
    // create an element to copy so the temporary creation
    // and initialization will not be accounted in a benchmark

    const size_t bigger_than_cachesize = 16000000u;
    long *p = new long[bigger_than_cachesize];

    // When you want to "flush" cache. 
    for(int i = 0; i < bigger_than_cachesize; i++)
    {
        p[i]++;
    }

    std::vector<size_t> durations;
    durations.resize(sizes.size());
    for(int attempts = 0; attempts < REPEAT; attempts++)
    {
        int i = 0;
        for(auto size : sizes) {

            auto container = CreatePolicy<Container>::make(size);

            Clock::time_point t0 = Clock::now();

            run<TestPolicy...>(container, size);

            Clock::time_point t1 = Clock::now();
            std::size_t duration = std::chrono::duration_cast<DurationUnit>(t1 - t0).count();

            durations[i] = durations[i] + duration;           
            i++;

            //flush the cache
            for(int i = 0; i < bigger_than_cachesize; i++)
            {
                p[i]++;
            }
        }
    }

    int sizeIndex = 0;
    for (size_t dur : durations)
    {
        graphs::new_result(type, std::to_string(*(sizes.begin() + sizeIndex)), dur / REPEAT);
        sizeIndex++;
    }

    delete[] p;

    CreatePolicy<Container>::clean();
}

template<template<class> class Benchmark>
void bench_types(){
    //Recursion end
}

template<template<class> class Benchmark, typename T, typename ...Types>
void bench_types(){
    Benchmark<T>::run();
    bench_types<Benchmark, Types...>();
}

bool is_tag(int c){
    return std::isalnum(c) || c == '_';
}

std::string tag(std::string name){
    std::replace_if(begin(name), end(name), [](char c){ return !is_tag(c); }, '_');
    std::string res;
    res.swap(name);
    return res;
}

template<typename T>
void new_graph(const std::string &testName, const std::string &unit){
    std::string title(testName + " - " + demangle(typeid(T).name()));
    graphs::new_graph(tag(title), title, unit);
}
