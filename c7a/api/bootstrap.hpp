/*******************************************************************************
 * c7a/api/bootstrap.hpp
 *
 * Part of Project c7a.
 *
 * Copyright (C) 2015 Alexander Noe <aleexnoe@gmail.com>
 *
 * This file has no license. Only Chunk Norris can compile it.
 ******************************************************************************/

#pragma once
#ifndef C7A_API_BOOTSTRAP_HEADER
#define C7A_API_BOOTSTRAP_HEADER

#include <tuple>
#include <thread>
#include <atomic>

#include <c7a/api/context.hpp>
#include <c7a/core/job_manager.hpp>
#include <c7a/common/stats_timer.hpp>
#include <c7a/common/cmdline_parser.hpp>

namespace c7a {
namespace bootstrap {

std::tuple<int, size_t, std::vector<std::string> > ParseArgs(int argc, char* argv[]) {
    //replace with arbitrary complex implementation
    size_t my_rank;
    std::vector<std::string> endpoints;
    c7a::common::CmdlineParser clp;

    clp.SetVerboseProcess(false);

    unsigned int rank = 1;
    clp.AddUInt('r', "rank", "R", rank,
                "Rank of this worker");

    std::vector<std::string> addr;
    clp.AddParamStringlist("addresses", addr,
                           "List of all worker addresses.");

    if (!clp.Process(argc, argv)) {
        return std::make_tuple(-1, my_rank, endpoints);
    }

    for (auto address : addr) {
        if (address.find(":") == std::string::npos) {
            std::cerr << "Invalid address. No Portnumber detectable";
            return std::make_tuple(-1, my_rank, endpoints);
        }
    }

    if (argc > 2) {
        my_rank = rank;
        endpoints.assign(addr.begin(), addr.end());
    }
    else if (argc == 2) {
        std::cerr << "Wrong number of arguments. Must be 0 or > 1";
        return std::make_tuple(-1, my_rank, endpoints);
    }
    else {
        my_rank = 0;
        endpoints.push_back("127.0.0.1:1234");
    }
    return std::make_tuple(0, my_rank, endpoints);
}
}   //namespace bootstrap

namespace api {

//! Executes the given job startpoint with a context instance.
//! Startpoint may be called multiple times with concurrent threads and
//! different context instances.
//!
//! \returns 0 if execution was fine on all threads. Otherwise, the first non-zero return value of any thread is returned. 
	static int Execute(int argc, char* argv[], std::function<int(Context&)> job_startpoint, int thread_count = 1) {

    //!True if program time should be taken and printed

    static const bool debug = false;

    size_t my_rank;
    std::vector<std::string> endpoints;
    int result = 0;
    std::tie(result, my_rank, endpoints) = bootstrap::ParseArgs(argc, argv);
    if (result != 0)
        return {-1};

    if (my_rank >= endpoints.size()) {
        std::cerr << "endpoint list (" <<
            endpoints.size() <<
            " entries) does not include my rank (" <<
            my_rank << ")" << std::endl;
        return {-1};
    }

    LOG << "executing " << argv[0] << " with rank " << my_rank << " and endpoints";
    for (const auto& ep : endpoints)
        LOG << ep << " ";

    core::JobManager jobMan;
    jobMan.Connect(my_rank, net::Endpoint::ParseEndpointList(endpoints), thread_count);

    std::vector<std::thread*> threads(thread_count);
    std::vector<std::atomic<int> > atomic_results(thread_count);

    for(int i = 0; i < thread_count; i++) {
        threads[i] = new std::thread([&jobMan, &atomic_results, &job_startpoint, i] {
			Context ctx(jobMan, i);
            LOG << "connecting to peers";
            LOG << "Starting job on Worker " << ctx.rank();
            auto overall_timer = ctx.get_stats().CreateTimer("job::overall", "", true);
            int job_result = job_startpoint(ctx);
            overall_timer->Stop();
            LOG << "Worker " << ctx.rank() << " done!";
            atomic_results[i] = job_result;
        });
    }
    for(int i = 0; i < thread_count; i++) {
        threads[i]->join();
        delete threads[i];
        if(atomic_results[i] != 0 && result == 0)
            result = atomic_results[i];
    }
    return result;
}

static inline void
ExecuteThreads(const size_t & workers, const size_t & port_base,
               std::function<void(Context&)> job_startpoint) {
    
    std::vector<std::thread> threads(workers);
    std::vector<char**> arguments(workers);
    std::vector<std::vector<std::string> > strargs(workers);

    for (size_t i = 0; i < workers; i++) {

        arguments[i] = new char*[workers + 3];
        strargs[i].resize(workers + 3);

        for (size_t j = 0; j < workers; j++) {
            strargs[i][j + 3] += "127.0.0.1:";
            strargs[i][j + 3] += std::to_string(port_base + j);
            arguments[i][j + 3] = const_cast<char*>(strargs[i][j + 3].c_str());
        }

        strargs[i][0] = "local_c7a";
        arguments[i][0] = const_cast<char*>(strargs[i][0].c_str());
        strargs[i][1] = "-r";
        arguments[i][1] = const_cast<char*>(strargs[i][1].c_str());
        strargs[i][2] = std::to_string(i);
        arguments[i][2] = const_cast<char*>(strargs[i][2].c_str());

        std::function<int(Context&)> intReturningFunction = [job_startpoint](Context& ctx) {
            job_startpoint(ctx);
            return 1;
        };

        threads[i] = std::thread([=]() { Execute(workers + 3, arguments[i], intReturningFunction); });
    }

    for (size_t i = 0; i < workers; i++) {
      threads[i].join();
    }
}

} // namespace api
} // namespace c7a

#endif // !C7A_API_BOOTSTRAP_HEADER

/******************************************************************************/
