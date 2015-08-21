/*******************************************************************************
 * thrill/data/block_writer.hpp
 *
 * Part of Project Thrill.
 *
 * Copyright (C) 2015 Timo Bingmann <tb@panthema.net>
 *
 * This file has no license. Only Chunk Norris can compile it.
 ******************************************************************************/

#pragma once
#ifndef THRILL_DATA_BLOCK_WRITER_HEADER
#define THRILL_DATA_BLOCK_WRITER_HEADER

#include <thrill/common/config.hpp>
#include <thrill/common/item_serialization_tools.hpp>
#include <thrill/data/block.hpp>
#include <thrill/data/block_sink.hpp>
#include <thrill/data/serialization.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace thrill {
namespace data {

//! \addtogroup data Data Subsystem
//! \{

/*!
 * BlockWriter contains a temporary Block object into which a) any serializable
 * item can be stored or b) any arbitrary integral data can be appended. It
 * counts how many serializable items are stored and the offset of the first new
 * item. When a Block is full it is emitted to an attached BlockSink, like a
 * File, a ChannelSink, etc. for further delivery. The BlockWriter takes care of
 * segmenting items when a Block is full.
 */
template <bool DisableSelfVerify>
class BlockWriterBase
    : public common::ItemWriterToolsBase<BlockWriterBase<DisableSelfVerify> >
{
public:
    static const bool self_verify = common::g_self_verify && !DisableSelfVerify;

    //! Start build (appending blocks) to a File
    explicit BlockWriterBase(BlockSink* sink,
                         size_t block_size = default_block_size)
        : sink_(sink),
          block_size_(block_size) {
        AllocateBlock();
    }

    //! non-copyable: delete copy-constructor
    BlockWriterBase(const BlockWriterBase&) = delete;
    //! non-copyable: delete assignment operator
    BlockWriterBase& operator = (const BlockWriterBase&) = delete;

    //! move-constructor
    BlockWriterBase(BlockWriterBase&&) = default;
    //! move-assignment
    BlockWriterBase& operator = (BlockWriterBase&&) = default;

    //! On destruction, the last partial block is flushed.
    ~BlockWriterBase() {
        if (bytes_)
            Close();
    }

    //! Explicitly close the writer
    void Close() {
        if (!closed_) { //potential race condition
            closed_ = true;
            MaybeFlushBlock();
            if (sink_)
                sink_->Close();
        }
    }

    //! Return whether an actual BlockSink is attached.
    bool IsValid() const { return sink_ != nullptr; }

    //! Flush the current block (only really meaningful for a network sink).
    void Flush() {
        FlushBlock();
        AllocateBlock();
    }

    //! Directly write Blocks to the underlying BlockSink (after flushing the
    //! current one if need be).
    void AppendBlocks(const std::vector<Block>& blocks) {
        MaybeFlushBlock();

        for (const Block& b : blocks)
            sink_->AppendBlock(b);

        AllocateBlock();
    }

    //! \name Appending (Generic) Serializable Items
    //! \{

    //! Mark beginning of an item.
    BlockWriterBase & MarkItem() {
        if (current_ == end_)
            Flush();

        if (nitems_ == 0)
            first_offset_ = current_ - bytes_->begin();

        ++nitems_;

        return *this;
    }

    //! operator() appends a complete item
    template <typename T>
    BlockWriterBase& operator () (const T& x) {
        assert(!closed_);
        MarkItem();
        if (self_verify) {
            // for self-verification, prefix T with its hash code
            Put(typeid(T).hash_code());
        }
        Serialization<BlockWriterBase, T>::Serialize(x, *this);
        return *this;
    }

    //! \}

    //! \name Appending Write Functions
    //! \{

    //! Append a memory range to the block
    BlockWriterBase & Append(const void* data, size_t size) {
        assert(!closed_);

        const Byte* cdata = reinterpret_cast<const Byte*>(data);

        while (THRILL_UNLIKELY(current_ + size > end_)) {
            // partial copy of beginning of buffer
            size_t partial_size = end_ - current_;
            std::copy(cdata, cdata + partial_size, current_);

            cdata += partial_size;
            size -= partial_size;
            current_ += partial_size;

            Flush();
        }

        // copy remaining bytes.
        std::copy(cdata, cdata + size, current_);
        current_ += size;

        return *this;
    }

    //! Append a single byte to the block
    BlockWriterBase & PutByte(Byte data) {
        assert(!closed_);

        if (THRILL_UNLIKELY(current_ == end_))
            Flush();

        *current_++ = data;
        return *this;
    }

    //! Append to contents of a std::string, excluding the null (which isn't
    //! contained in the string size anyway).
    BlockWriterBase & Append(const std::string& str) {
        return Append(str.data(), str.size());
    }

    //! Put (append) a single item of the template type T to the buffer. Be
    //! careful with implicit type conversions!
    template <typename Type>
    BlockWriterBase & Put(const Type& item) {
        static_assert(std::is_pod<Type>::value,
                      "You only want to Put() POD types as raw values.");

        assert(!closed_);

        // fast path for writing item into block if it fits.
        if (THRILL_LIKELY(current_ + sizeof(Type) <= end_)) {
            *reinterpret_cast<Type*>(current_) = item;

            current_ += sizeof(Type);
            return *this;
        }

        return Append(&item, sizeof(item));
    }

    //! \}

protected:
    //! Allocate a new block (overwriting the existing one).
    void AllocateBlock() {
        bytes_ = ByteBlock::Allocate(block_size_);
        current_ = bytes_->begin();
        end_ = bytes_->end();
        nitems_ = 0;
        first_offset_ = 0;
    }

    //! Flush the currently created block into the underlying File.
    void FlushBlock() {
        sink_->AppendBlock(bytes_, 0, current_ - bytes_->begin(),
                           first_offset_, nitems_);
    }

    //! Flush the currently created block if it contains at least one byte
    void MaybeFlushBlock() {
        if (current_ != bytes_->begin() || nitems_) {
            FlushBlock();
            nitems_ = 0;
            bytes_ = ByteBlockPtr();
            current_ = nullptr;
        }
    }

    //! current block, already allocated as shared ptr, since we want to use
    //! make_shared.
    ByteBlockPtr bytes_;

    //! current write pointer into block.
    Byte* current_;

    //! current end of block pointer. this is == bytes_.end(), just one
    //! indirection less.
    Byte* end_;

    //! number of items in current block
    size_t nitems_;

    //! offset of first item
    size_t first_offset_;

    //! file or stream sink to output blocks to.
    BlockSink* sink_;

    //! size of data blocks to construct
    size_t block_size_;

    //! Flag if Close was called explicitly
    bool closed_ = false;
};

using BlockWriter = BlockWriterBase<false>;
using BlockWriterNoVerify = BlockWriterBase<true>;

//! \}

} // namespace data
} // namespace thrill

#endif // !THRILL_DATA_BLOCK_WRITER_HEADER

/******************************************************************************/
