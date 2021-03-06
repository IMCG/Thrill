/*******************************************************************************
 * benchmarks/data/stream_a_to_b.cpp
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

//! Creates two threads that work with two context instances
//! one worker sends elements to the other worker.
//! Number of elements depends on the number of bytes.
//! one RESULT line will be printed for each iteration
//! All iterations use the same generated data.
//! Variable-length elements range between 1 and 100 bytes
template <typename Type>
void ConductExperiment(uint64_t bytes, int iterations, api::Context& ctx1, api::Context& ctx2, const std::string& type_as_string) {

    auto data = generate<Type>(bytes, 1, 100);
    common::ThreadPool pool;
    for (int i = 0; i < iterations; i++) {
        StatsTimer<true> write_timer;
        pool.Enqueue([&data, &ctx1, &write_timer]() {
                         auto stream = ctx1.GetNewCatStream();
                         auto writers = stream->OpenWriters();
                         assert(writers.size() == 2);
                         write_timer.Start();
                         auto& writer = writers[1];
                         for (auto& s : data) {
                             writer(s);
                         }
                         writer.Close();
                         writers[0].Close();
                         write_timer.Stop();
                     });

        StatsTimer<true> read_timer;
        pool.Enqueue([&ctx2, &read_timer]() {
                         auto stream = ctx2.GetNewCatStream();
                         auto readers = stream->OpenReaders();
                         assert(readers.size() == 2);
                         auto& reader = readers[0];
                         read_timer.Start();
                         while (reader.HasNext()) {
                             reader.Next<Type>();
                         }
                         read_timer.Stop();
                     });
        pool.LoopUntilEmpty();
        std::cout << "RESULT"
                  << " datatype=" << type_as_string
                  << " size=" << bytes
                  << " write_time=" << write_timer
                  << " read_time=" << read_timer
                  << std::endl;
    }
}

int main(int argc, const char** argv) {
    common::ThreadPool connect_pool;
    std::vector<std::string> endpoints;
    endpoints.push_back("127.0.0.1:8000");
    endpoints.push_back("127.0.0.1:8001");
    std::unique_ptr<api::HostContext> host_ctx1, host_ctx2;
    connect_pool.Enqueue(
        [&host_ctx1, &endpoints]() {
            host_ctx1 = std::make_unique<api::HostContext>(0, endpoints, 1);
        });

    connect_pool.Enqueue(
        [&host_ctx2, &endpoints]() {
            host_ctx2 = std::make_unique<api::HostContext>(1, endpoints, 1);
        });
    connect_pool.LoopUntilEmpty();

    api::Context ctx1(*host_ctx1, 0);
    api::Context ctx2(*host_ctx2, 0);

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
        ConductExperiment<int>(bytes, iterations, ctx1, ctx2, type);
    else if (type == "size_t")
        ConductExperiment<size_t>(bytes, iterations, ctx1, ctx2, type);
    else if (type == "string")
        ConductExperiment<std::string>(bytes, iterations, ctx1, ctx2, type);
    else if (type == "pair")
        ConductExperiment<std::pair<std::string, int> >(bytes, iterations, ctx1, ctx2, type);
    else if (type == "triple")
        ConductExperiment<std::tuple<std::string, int, std::string> >(bytes, iterations, ctx1, ctx2, type);
    else
        abort();
}

/******************************************************************************/
