/*******************************************************************************
 * c7a/api/context.hpp
 *
 * Part of Project c7a.
 *
 * Copyright (C) 2015 Alexander Noe <aleexnoe@gmail.com>
 * Copyright (C) 2015 Tobias Sturm <mail@tobiassturm.de>
 *
 * This file has no license. Only Chunk Norris can compile it.
 ******************************************************************************/

#pragma once
#ifndef C7A_API_CONTEXT_HEADER
#define C7A_API_CONTEXT_HEADER

#include <c7a/api/stats_graph.hpp>
#include <c7a/common/config.hpp>
#include <c7a/common/stats.hpp>
#include <c7a/data/manager.hpp>
#include <c7a/net/manager.hpp>
#include <c7a/net/flow_control_channel.hpp>
#include <c7a/net/flow_control_manager.hpp>

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace c7a {
namespace api {

//! \addtogroup api Interface
//! \{

/*!
 * The Context of a job is a unique instance per worker which holds
 *  references to all underlying parts of c7a. The context is able to give
 *  references to the  \ref c7a::data::Manager "data manager", the
 * \ref c7a::net::Group  "net group"
 * \ref c7a::common::Stats "stats" and
 * \ref c7a::common::StatsGraph "stats graph".
 * Threads share the data manager and
 * the net group via the context object.
 */
class Context
{
public:
    Context(net::Manager& net_manager, net::FlowControlChannelManager& flow_manager, data::Manager& data_manager, size_t workers_per_computing_node, size_t worker_id)
        : net_manager_(net_manager)
        , flow_manager_(flow_manager)
        , data_manager_(data_manager)
        , worker_id_(worker_id)
        , workers_per_computing_node_(workers_per_computing_node)
    { }

    //! Returns a reference to the data manager, which gives iterators and
    //! emitters for data.
    data::Manager & data_manager() const {
        return data_manager_;
    }

    /**
     * @brief Gets the flow control channel for the current worker.
     *
     * @return The flow control channel instance for this worker.
     */
    net::FlowControlChannel & flow_control_channel() {
        return flow_manager_.GetFlowControlChannel(worker_id_);
    }

    //! Returns the total number of computing_nodes.
    size_t number_computing_node() const {
        return net_manager_.num_nodes();
    }

    //! Returns the number of nodes that is hosted on each computing_node
    size_t workers_per_computing_node() const {
        return workers_per_computing_node_;
    }

    size_t my_rank() const {
        return workers_per_computing_node() * computing_node_id() + worker_id();
    }

    size_t max_rank() const {
        return number_computing_node() * workers_per_computing_node() - 1;
    }

    size_t num_workers() const {
        return max_rank() + 1;
    }

    //! Returns id of this computing_node in the cluser
    //! A computing_node is a machine in the cluster that hosts multiple workers
    size_t computing_node_id() const {
        return net_manager_.my_node_rank();
    }

    //! Returns the local id ot this worker on the computing_node
    //! A worker is _locally_ identified by this id
    size_t worker_id() const {
        return worker_id_;
    }

    //! Returns the stas object for this worker
    common::Stats<common::g_enable_stats> & stats() {
        return stats_;
    }

    //! Returns the stats graph object for this worker
    api::StatsGraph & stats_graph() {
        return stats_graph_;
    }

private:
    //! net::Manager instance that is shared among workers
    net::Manager& net_manager_;

    //! net::FlowControlChannelManager instance that is shared among workers
    net::FlowControlChannelManager& flow_manager_;

    //! data::Manager instance that is shared among workers
    data::Manager& data_manager_;

    //! StatsGrapg object that is uniquely held for this worker
    api::StatsGraph stats_graph_;
    common::Stats<common::g_enable_stats> stats_;


    //! number of this computing_node context, 0..p-1, within this compute node
    size_t worker_id_;

    //! number of workers hosted per computing_node
    size_t workers_per_computing_node_;
};

//! Outputs the context as <computing node id>:<worker id> to an std::ostream
static inline std::ostream& operator << (std::ostream& os, const Context& ctx) {
    return os << ctx.computing_node_id() << ":" << ctx.worker_id();
}

//! Executes the given job startpoint with a context instance.
//! Startpoint may be called multiple times with concurrent workers (threads)
//! and different context instances.
//!
//! \returns 0 if execution was fine on all threads. Otherwise, the first
//! non-zero return value of any thread is returned.
int Execute(
    int argc, char* const* argv,
    std::function<void(Context&)> job_startpoint,
    const std::string& log_prefix = "");

/*!
 * Function to run a number of computing_nodes as locally independent threads, which
 * still communicate via TCP sockets (threads_per_computing_node = 1)
 */
void
ExecuteLocalThreadsTCP(const size_t& computing_nodes, const size_t& port_base,
                       std::function<void(Context&)> job_startpoint);
//TODO maybe this should be moved somewhere into test/helpers -ts

/*!
 * Helper Function to ExecuteLocalThreads in test suite for many different
 * numbers of local computing_nodes as independent threads.
 */
void ExecuteLocalTestsTCP(std::function<void(Context&)> job_startpoint);
//TODO maybe this should be moved somewhere into test/helpers -ts

/*!
 * Function to run a number of mock compute nodes as locally independent
 * threads, which communicate via internal stream sockets.
 */
void
ExecuteLocalMock(size_t node_count, size_t local_computing_node_count,
                 std::function<void(api::Context&, size_t)> job_startpoint);

/*!
 * Helper Function to execute tests using mock networks in test suite for many
 * different numbers of node and computing_nodes as independent threads in one program.
 */
void ExecuteLocalTests(std::function<void(Context&)> job_startpoint);

/*!
 * Executes the given job_startpoint within the same thread -->
 * one computing node with one thread
 */
void ExecuteSameThread(std::function<void(Context&)> job_startpoint);

/*!
 * Executes the given job startpoint with a context instance.  Startpoints may
 * be called multiple times with concurrent threads and different context
 * instances across different workers.  The c7a configuration is taken from
 * environment variables starting the C7A_.
 *
 * C7A_RANK contains the rank of this worker
 *
 * C7A_HOSTLIST contains a space- or comma-separated list of host:ports to connect to.
 *
 * \returns 0 if execution was fine on all threads. Otherwise, the first
 * non-zero return value of any thread is returned.
 */
int ExecuteEnv(
    std::function<void(Context&)> job_startpoint,
    const std::string& log_prefix = std::string());

//! \}

} // namespace api

//! imported from api namespace
using c7a::api::Context;

} // namespace c7a

#endif // !C7A_API_CONTEXT_HEADER

/******************************************************************************/
