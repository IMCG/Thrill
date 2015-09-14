/*******************************************************************************
 * thrill/data/multiplexer.hpp
 *
 * Part of Project Thrill.
 *
 * Copyright (C) 2015 Timo Bingmann <tb@panthema.net>
 * Copyright (C) 2015 Tobias Sturm <mail@tobiassturm.de>
 *
 * This file has no license. Only Chunk Norris can compile it.
 ******************************************************************************/

#pragma once
#ifndef THRILL_DATA_MULTIPLEXER_HEADER
#define THRILL_DATA_MULTIPLEXER_HEADER

#include <thrill/data/repository.hpp>
#include <thrill/net/dispatcher_thread.hpp>
#include <thrill/net/group.hpp>

#include <algorithm>
#include <atomic>
#include <memory>

namespace thrill {
namespace data {

//! \addtogroup data Data Subsystem
//! \{

class ChannelSetBase;

template <typename Channel>
class ChannelSet;

class ConcatChannel;
using ConcatChannelPtr = std::shared_ptr<ConcatChannel>;
using ConcatChannelSet = ChannelSet<ConcatChannel>;
using ConcatChannelSetPtr = std::shared_ptr<ConcatChannelSet>;

class MixedChannel;
using MixedChannelPtr = std::shared_ptr<MixedChannel>;
using MixedChannelSet = ChannelSet<MixedChannel>;
using MixedChannelSetPtr = std::shared_ptr<MixedChannelSet>;

class BlockQueue;
class MixedBlockQueueSink;

struct ChannelBlockHeader;

/*!
 * Multiplexes virtual Connections on Dispatcher.
 *
 * A worker as a TCP conneciton to each other worker to exchange large amounts
 * of data. Since multiple exchanges can occur at the same time on this single
 * connection we use multiplexing. The slices are called Blocks and are
 * indicated by a \ref ChannelBlockHeader. Multiple Blocks form a Channel on a
 * single TCP connection. The multiplexer multiplexes all streams on all
 * sockets.
 *
 * All sockets are polled for headers. As soon as the a header arrives it is
 * either attached to an existing channel or a new channel instance is
 * created.
 */
class Multiplexer
{
public:
    explicit Multiplexer(mem::Manager& mem_manager,
                         data::BlockPool& block_pool,
                         size_t num_workers_per_host, net::Group& group)
        : mem_manager_(mem_manager),
          block_pool_(block_pool),
          dispatcher_(mem_manager, group, "multiplexer"),
          group_(group),
          num_workers_per_host_(num_workers_per_host),
          channel_sets_(num_workers_per_host) {
        for (size_t id = 0; id < group_.num_hosts(); id++) {
            if (id == group_.my_host_rank()) continue;
            AsyncReadBlockHeader(group_.connection(id));
        }
    }

    //! non-copyable: delete copy-constructor
    Multiplexer(const Multiplexer&) = delete;
    //! non-copyable: delete assignment operator
    Multiplexer& operator = (const Multiplexer&) = delete;

    //! Closes all client connections
    ~Multiplexer();

    //! total number of hosts.
    size_t num_hosts() const {
        return group_.num_hosts();
    }

    //! my rank among the hosts.
    size_t my_host_rank() const {
        return group_.my_host_rank();
    }

    //! total number of workers.
    size_t num_workers() const {
        return num_hosts() * num_workers_per_host_;
    }

    //! number of workers per host
    size_t num_workers_per_host() const {
        return num_workers_per_host_;
    }

    //! Get the used BlockPool
    BlockPool & block_pool() { return block_pool_; }

    //! \name ConcatChannel
    //! \{

    //! Allocate the next channel
    size_t AllocateConcatChannelId(size_t local_worker_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return channel_sets_.AllocateId(local_worker_id);
    }

    //! Get channel with given id, if it does not exist, create it.
    ConcatChannelPtr GetOrCreateConcatChannel(size_t id, size_t local_worker_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return _GetOrCreateConcatChannel(id, local_worker_id);
    }

    //! Request next channel.
    ConcatChannelPtr GetNewConcatChannel(size_t local_worker_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return _GetOrCreateConcatChannel(
            channel_sets_.AllocateId(local_worker_id), local_worker_id);
    }

    //! \}

    //! \name MixedChannel
    //! \{

    //! Allocate the next channel
    size_t AllocateMixedChannelId(size_t local_worker_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return channel_sets_.AllocateId(local_worker_id);
    }

    //! Get channel with given id, if it does not exist, create it.
    MixedChannelPtr GetOrCreateMixedChannel(size_t id, size_t local_worker_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return _GetOrCreateMixedChannel(id, local_worker_id);
    }

    //! Request next channel.
    MixedChannelPtr GetNewMixedChannel(size_t local_worker_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return _GetOrCreateMixedChannel(
            channel_sets_.AllocateId(local_worker_id), local_worker_id);
    }

    //! \}

protected:
    static const bool debug = false;

    //! reference to host-global memory manager
    mem::Manager& mem_manager_;

    //! reference to host-global BlockPool.
    data::BlockPool& block_pool_;

    //! dispatcher used for all communication by data::Multiplexer, the thread
    //! never leaves the data components!
    net::DispatcherThread dispatcher_;

    // Holds NetConnections for outgoing Channels
    net::Group& group_;

    //! Number of workers per host
    size_t num_workers_per_host_;

    //! protects critical sections
    std::mutex mutex_;

    //! friends for access to network components
    friend class ConcatChannel;
    friend class MixedChannel;

    //! Pointer to queue that is used for communication between two workers on
    //! the same host.
    //! \param from_worker_id is the id of the sending worker
    //! \param to_worker_id is the id of the receiving worker, that owns the queue
    BlockQueue * ConcatLoopback(
        size_t channel_id, size_t from_worker_id, size_t to_worker_id);

    MixedBlockQueueSink * MixedLoopback(
        size_t channel_id, size_t from_worker_id, size_t to_worker_id);

    /**************************************************************************/

    //! Channels have an ID in block headers. (worker id, channel id)
    Repository<ChannelSetBase> channel_sets_;

    ConcatChannelPtr _GetOrCreateConcatChannel(size_t id, size_t local_worker_id);
    MixedChannelPtr _GetOrCreateMixedChannel(size_t id, size_t local_worker_id);

    /**************************************************************************/

    using Connection = net::Connection;

    //! expects the next BlockHeader from a socket and passes to OnBlockHeader
    void AsyncReadBlockHeader(Connection& s);

    //! parses BlockHeader and decides whether to receive Block or close Channel
    void OnBlockHeader(Connection& s, net::Buffer&& buffer);

    //! Receives and dispatches a Block to a ConcatChannel
    void OnConcatChannelBlock(
        Connection& s, const ChannelBlockHeader& header,
        const ConcatChannelPtr& channel, const ByteBlockPtr& bytes);

    //! Receives and dispatches a Block to a MixedChannel
    void OnMixedChannelBlock(
        Connection& s, const ChannelBlockHeader& header,
        const MixedChannelPtr& channel, const ByteBlockPtr& bytes);
};

//! \}

} // namespace data
} // namespace thrill

#endif // !THRILL_DATA_MULTIPLEXER_HEADER

/******************************************************************************/
