/*******************************************************************************
 * c7a/data/channel.hpp
 *
 * Part of Project c7a.
 *
 * Copyright (C) 2015 Timo Bingmann <tb@panthema.net>
 * Copyright (C) 2015 Tobias Sturm <mail@tobiassturm.de>
 *
 * This file has no license. Only Chunk Norris can compile it.
 ******************************************************************************/

#pragma once
#ifndef C7A_DATA_CHANNEL_HEADER
#define C7A_DATA_CHANNEL_HEADER

#include <c7a/data/block_queue.hpp>
#include <c7a/data/channel_sink.hpp>
#include <c7a/data/concat_block_source.hpp>
#include <c7a/data/file.hpp>
#include <c7a/data/stream_block_header.hpp>
#include <c7a/net/connection.hpp>
#include <c7a/net/group.hpp>

#include <sstream>
#include <string>
#include <vector>

namespace c7a {
namespace data {

//! \addtogroup data Data Subsystem
//! \{

using ChannelId = size_t;

/*!
 * A Channel is a virtual set of connections to all other workers instances,
 * hence a "Channel" bundles them to a logical communication context. We call an
 * individual connection from a worker to another worker a "Stream", though no
 * such class exists.
 *
 * To use a Channel, one can get a vector of BlockWriter via OpenWriters() of
 * outbound Stream. The vector is of size workers, including virtual
 * connections to the local worker(s). One can then write items destined to the
 * corresponding worker. The written items are buffered into a Block and only
 * sent when the Block is full. To force a send, use BlockWriter::Flush(). When
 * all items are sent, the BlockWriters **must** be closed using
 * BlockWriter::Close().
 *
 * To read the inbound Stream items, one can get a vector of BlockReader via
 * OpenReaders(), which can then be used to read items sent by individual
 * workers.
 *
 * Alternatively, one can use OpenReader() to get a BlockReader which delivers
 * all items from *all* workers in worker order (concatenating all inbound
 * Streams).
 *
 * As soon as all attached streams of the Channel have been Close()the number of
 * expected streams is reached, the channel is marked as finished and no more
 * data will arrive.
 */
class Channel
{
public:
    using BlockQueueReader = BlockReader<BlockQueueSource>;
    using ConcatBlockSource = data::ConcatBlockSource<BlockQueueSource>;
    using ConcatBlockReader = BlockReader<ConcatBlockSource>;

    using CachingConcatBlockSource = data::ConcatBlockSource<CachingBlockQueueSource>;
    using CachingConcatBlockReader = BlockReader<CachingConcatBlockSource>;

    using Reader = BlockQueueReader;
    using ConcatReader = ConcatBlockReader;
    using CachingConcatReader = CachingConcatBlockReader;

    //! Creates a new channel instance
    Channel(const ChannelId& id, net::Group& group,
            net::DispatcherThread& dispatcher)
        : id_(id),
          queues_(group.Size()),
          cache_files_(group.Size()),
          group_(group),
          dispatcher_(dispatcher) {
        // construct ChannelSink array
        for (size_t i = 0; i != group_.Size(); ++i) {
            if (i == group_.MyRank()) {
                sinks_.emplace_back();
            }
            else {
                sinks_.emplace_back(
                    &dispatcher, &group_.connection(i), id, group_.MyRank());
            }
        }
    }

    //! non-copyable: delete copy-constructor
    Channel(const Channel&) = delete;
    //! non-copyable: delete assignment operator
    Channel& operator = (const Channel&) = delete;

    //! move-constructor
    Channel(Channel&&) = default;

    const ChannelId & id() const {
        return id_;
    }

    //! Creates BlockWriters for each worker. BlockWriter can only be opened
    //! once, otherwise the block sequence is incorrectly interleaved!
    std::vector<BlockWriter> OpenWriters(size_t block_size = default_block_size) {
        std::vector<BlockWriter> result;

        for (size_t worker_id = 0; worker_id < group_.Size(); ++worker_id) {
            if (worker_id == group_.MyRank()) {
                result.emplace_back(&queues_[worker_id], block_size);
            }
            else {
                result.emplace_back(&sinks_[worker_id], block_size);
            }
        }

        assert(result.size() == group_.Size());
        return result;
    }

    //! Creates a BlockReader for each worker. The BlockReaders are attached to
    //! the BlockQueues in the Channel and wait for further Blocks to arrive or
    //! the Channel's remote close.
    std::vector<BlockQueueReader> OpenReaders() {
        std::vector<BlockQueueReader> result;

        for (size_t worker_id = 0; worker_id < group_.Size(); ++worker_id) {
            result.emplace_back(BlockQueueSource(queues_[worker_id]));
        }

        assert(result.size() == group_.Size());
        return result;
    }

    //! Creates a BlockReader for all workers. The BlockReader is attached to
    //! one \ref ConcatBlockSource which includes all incoming queues of
    //! this channel.
    ConcatBlockReader OpenReader() {
        // construct vector of BlockQueueSources to read from queues_.
        std::vector<BlockQueueSource> result;
        for (size_t worker_id = 0; worker_id < group_.Size(); ++worker_id) {
            result.emplace_back(queues_[worker_id]);
        }
        // move BlockQueueSources into concatenation BlockSource, and to Reader.
        return ConcatBlockReader(ConcatBlockSource(result));
    }

    //! Creates a BlockReader for all workers. The BlockReader is attached to
    //! one \ref ConcatBlockSource which includes all incoming queues of this
    //! channel. The received Blocks are also cached in the Channel, hence this
    //! function can be called multiple times to read the items again.
    CachingConcatBlockReader OpenCachingReader() {
        // construct vector of CachingBlockQueueSources to read from queues_.
        std::vector<CachingBlockQueueSource> result;
        for (size_t worker_id = 0; worker_id < group_.Size(); ++worker_id) {
            result.emplace_back(queues_[worker_id], cache_files_[worker_id]);
        }
        // move CachingBlockQueueSources into concatenation BlockSource, and to
        // Reader.
        return CachingConcatBlockReader(CachingConcatBlockSource(result));
    }

    /*!
     * Scatters a File to many workers
     *
     * elements from 0..offset[0] are sent to the first worker,
     * elements from (offset[0] + 1)..offset[1] are sent to the second worker.
     * elements from (offset[my_rank - 1] + 1)..(offset[my_rank]) are copied
     * The offset values range from 0..Manager::GetNumElements().
     * The number of given offsets must be equal to the net::Group::Size().
     *
     * /param source File containing the data to be scattered.
     *
     * /param offsets - as described above. offsets.size must be equal to group.size
     */
    template <typename ItemType>
    void Scatter(const File& source, const std::vector<size_t>& offsets) {
        // current item offset in Reader
        size_t current = 0;
        typename File::Reader reader = source.GetReader();

        std::vector<BlockWriter> writers = OpenWriters();

        for (size_t worker_id = 0; worker_id < offsets.size(); ++worker_id) {
            // write [current,limit) to this worker
            size_t limit = offsets[worker_id];
            assert(current <= limit);
#if 0
            for ( ; current < limit; ++current) {
                assert(reader.HasNext());
                // move over one item (with deserialization and serialization)
                writers[worker_id](reader.template Next<ItemType>());
            }
#else
            if (current != limit) {
                writers[worker_id].AppendBlocks(
                    reader.template GetItemBatch<ItemType>(limit - current));
                current = limit;
            }
#endif
            writers[worker_id].Close();
        }
    }

    //! shuts the channel down.
    void Close() {
        // close all sinks, this should emit sentinel to all other workers.
        for (size_t i = 0; i != sinks_.size(); ++i) {
            if (sinks_[i].closed()) continue;
            sinks_[i].Close();
        }

        // close self-loop queues
        if (!queues_[group_.MyRank()].write_closed())
            queues_[group_.MyRank()].Close();

        // wait for close packets to arrive (this is a busy waiting loop, try to
        // do it better -tb)
        for (size_t i = 0; i != queues_.size(); ++i) {
            while (!queues_[i].write_closed()) {
                LOG << "wait for close from worker" << i;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    //! Indicates if the channel is closed - meaning all remite streams have
    //! been closed. This does *not* include the loopback stream
    bool closed() const {
        bool closed = true;
        for (auto& q : queues_) {
            closed = closed && q.write_closed();
        }
        return closed;
    }

protected:
    static const bool debug = false;

    ChannelId id_;

    //! ChannelSink objects are receivers of Blocks outbound for other workers.
    std::vector<ChannelSink> sinks_;

    //! BlockQueues to store incoming Blocks with no attached destination.
    std::vector<BlockQueue> queues_;

    //! Vector of Files to cache inbound Blocks needed for OpenCachingReader().
    std::vector<File> cache_files_;

    net::Group& group_;
    net::DispatcherThread& dispatcher_;

    friend class ChannelMultiplexer;

    //! called from ChannelMultiplexer when there is a new Block on a
    //! Stream.
    void OnStreamBlock(size_t from, VirtualBlock&& vb) {
        assert(from < queues_.size());

        sLOG << "OnStreamBlock" << vb;

        if (debug) {
            sLOG << "channel" << id_ << "receive from" << from << ":"
                 << common::hexdump(vb.ToString());
        }

        queues_[from].AppendBlock(vb);
    }

    //! called from ChannelMultiplexer when a Stream closed notification was
    //! received.
    void OnCloseStream(size_t from) {
        assert(from < queues_.size());
        assert(!queues_[from].write_closed());
        queues_[from].Close();
    }
};

using ChannelPtr = std::shared_ptr<Channel>;

//! \}

} // namespace data
} // namespace c7a

#endif // !C7A_DATA_CHANNEL_HEADER

/******************************************************************************/
