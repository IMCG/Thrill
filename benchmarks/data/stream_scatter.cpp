/*******************************************************************************
 * benchmarks/data/stream_scatter.cpp
 *
 * Part of Project Thrill.
 *
 * Copyright (C) 2015 Tobias Sturm <mail@tobiassturm.de>
 *
 * This file has no license. Only Chunk Norris can compile it.
 ******************************************************************************/

#include <thrill/api/context.hpp>
#include <thrill/common/cmdline_parser.hpp>
#include <thrill/common/logger.hpp>
#include <thrill/common/thread_pool.hpp>

#include <iostream>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "data_generators.hpp"

using namespace thrill; // NOLINT
using common::StatsTimer;

//! Creates three threads / workers that work with three context instances
//! Worker 0 and 1 hold 50% of the DIA each
//! Data is scattered such that worker 0 transfers 1/3 of his data to worker 1
//! Worker 1 scatters 2/3 of his data to worker 2
//! Number of elements depends on the number of bytes.
//! one RESULT line will be printed for each iteration
//! All iterations use the same generated data.
//! Variable-length elements range between 1 and 100 bytes
template <typename Type>
void ConductExperiment(uint64_t bytes, int iterations,
                       api::Context& ctx0, api::Context& ctx1, api::Context& ctx2,
                       const std::string& type_as_string) {

    // prepare file with random data
    auto data0 = generate<Type>(bytes / 2, 1, 100);
    auto data1 = generate<Type>(bytes / 2, 1, 100);
    std::vector<data::File> files;
    files.reserve(3);
    {
        files.emplace_back(ctx0.GetFile());
        auto writer0 = files[0].GetWriter();
        for (auto& d : data0)
            writer0(d);

        files.emplace_back(ctx1.GetFile());
        auto writer1 = files[1].GetWriter();
        for (auto& d : data1)
            writer1(d);

        files.emplace_back(ctx2.GetFile());
        auto writer2 = files[2].GetWriter();
    }

    // worker 0 and worker 1 hold 50% each
    // worker 0 keeps 2/3 of his data, sends 1/3 to worker 1
    // worker 1 keeps first 1/3 of his data, sends 2/3 to worker 2
    // worker 2 receives 2/3 from worker 1
    // afterwards everybody holds 33% of the data
    std::vector<std::vector<size_t> > offsets;
    offsets.push_back({ (size_t)(2 * data0.size() / 3), data0.size(), data0.size() });
    offsets.push_back({ 0, (size_t)(data1.size() / 3), data1.size() });
    offsets.push_back({ 0, 0, 0 });

    std::vector<std::shared_ptr<data::CatStream> > streams;
    streams.push_back(ctx0.GetNewCatStream());
    streams.push_back(ctx1.GetNewCatStream());
    streams.push_back(ctx2.GetNewCatStream());

    std::vector<StatsTimer<true> > read_timers(3);
    std::vector<StatsTimer<true> > write_timers(3);

    common::ThreadPool pool;
    for (int i = 0; i < iterations; i++) {
        for (int id = 0; id < 3; id++) {
            pool.Enqueue([&files, &streams, &offsets, &read_timers, &write_timers, id]() {
                             write_timers[id].Start();
                             streams[id]->Scatter<Type>(files[id], offsets[id]);
                             write_timers[id].Stop();
                             auto reader = streams[id]->OpenCatReader(true);
                             read_timers[id].Start();
                             while (reader.HasNext()) {
                                 reader.Next<Type>();
                             }
                             read_timers[id].Stop();
                         });
        }
        pool.LoopUntilEmpty();
        std::cout << "RESULT"
                  << " datatype=" << type_as_string
                  << " size=" << bytes
                  << " write_time_worker0=" << write_timers[0].Microseconds()
                  << " read_time_worker0=" << read_timers[0].Microseconds()
                  << " write_time_worker1=" << write_timers[1].Microseconds()
                  << " read_time_worker1=" << read_timers[1].Microseconds()
                  << " write_time_worker2=" << write_timers[2].Microseconds()
                  << " read_time_worker2=" << read_timers[2].Microseconds()
                  << std::endl;
    }
}

int main(int argc, const char** argv) {
    common::ThreadPool connect_pool;
    std::vector<std::string> endpoints;
    endpoints.push_back("127.0.0.1:8000");
    endpoints.push_back("127.0.0.1:8001");
    endpoints.push_back("127.0.0.1:8002");

    std::unique_ptr<api::HostContext> host_ctx1, host_ctx2, host_ctx3;
    connect_pool.Enqueue(
        [&host_ctx1, &endpoints]() {
            host_ctx1 = std::make_unique<api::HostContext>(0, endpoints, 1);
        });
    connect_pool.Enqueue(
        [&host_ctx2, &endpoints]() {
            host_ctx2 = std::make_unique<api::HostContext>(1, endpoints, 1);
        });
    connect_pool.Enqueue(
        [&host_ctx3, &endpoints]() {
            host_ctx3 = std::make_unique<api::HostContext>(2, endpoints, 1);
        });
    connect_pool.LoopUntilEmpty();

    api::Context ctx1(*host_ctx1, 0);
    api::Context ctx2(*host_ctx2, 0);
    api::Context ctx3(*host_ctx3, 0);
    common::NameThisThread("benchmark");

    common::CmdlineParser clp;
    clp.SetDescription("thrill::data benchmark for disk I/O");
    clp.SetAuthor("Tobias Sturm <mail@tobiassturm.de>");
    int iterations;
    uint64_t bytes;
    std::string type;
    clp.AddBytes('b', "bytes", bytes, "number of bytes to process");
    clp.AddParamInt("n", iterations, "Iterations");
    clp.AddParamString("type", type,
                       "data type (int, string, pair, triple)");
    if (!clp.Process(argc, argv)) return -1;

    if (type == "int")
        ConductExperiment<int>(bytes, iterations, ctx1, ctx2, ctx3, type);
    if (type == "string")
        ConductExperiment<std::string>(bytes, iterations, ctx1, ctx2, ctx3, type);
    if (type == "pair")
        ConductExperiment<std::pair<std::string, int> >(bytes, iterations, ctx1, ctx2, ctx3, type);
    if (type == "triple")
        ConductExperiment<std::tuple<std::string, int, std::string> >(bytes, iterations, ctx1, ctx2, ctx3, type);
}

/******************************************************************************/
