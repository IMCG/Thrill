/*******************************************************************************
 * c7a/data/file.hpp
 *
 * Part of Project c7a.
 *
 * Copyright (C) 2015 Timo Bingmann <tb@panthema.net>
 *
 * This file has no license. Only Chunk Norris can compile it.
 ******************************************************************************/

#pragma once
#ifndef C7A_DATA_FILE_HEADER
#define C7A_DATA_FILE_HEADER

#include <c7a/common/logger.hpp>
#include <c7a/data/block.hpp>
#include <c7a/data/block_reader.hpp>
#include <c7a/data/block_sink.hpp>
#include <c7a/data/block_writer.hpp>

#include <cassert>
#include <memory>
#include <string>
#include <vector>

namespace c7a {
namespace data {

//! \addtogroup data Data Subsystem
//! \{

template <size_t BlockSize>
class FileBlockSource;

template <size_t BlockSize>
class CachingBlockQueueSource;

/*!
 * A File or generally FileBase<BlockSize> is an ordered sequence of
 * VirtualBlock objects for storing items. By using the VirtualBlock
 * indirection, the File can be composed using existing Block objects (via
 * reference counting), but only contain a subset of the items in those
 * Blocks. This may be used for Zip() and Repartition().
 *
 * A File can be written using a BlockWriter instance, which is delivered by
 * GetWriter(). Thereafter it can be read (multiple times) using a BlockReader,
 * delivered by GetReader().
 *
 * Using a prefixsum over the number of items in a Block, one can seek to the
 * block contained any item offset in log_2(Blocks) time, though seeking within
 * the Block goes sequentially.
 */
template <size_t BlockSize>
class FileBase : public BlockSink<BlockSize>
{
public:
    enum { block_size = BlockSize };

    using Block = data::Block<BlockSize>;
    using BlockCPtr = std::shared_ptr<const Block>;
    using VirtualBlock = data::VirtualBlock<BlockSize>;

    using FileBlockSource = data::FileBlockSource<BlockSize>;

    using Writer = BlockWriterBase<BlockSize>;
    using Reader = BlockReader<FileBlockSource>;

    //! Append a block to this file, the block must contain given number of
    //! items after the offset first.
    void AppendBlock(const VirtualBlock& vb) {
        assert(!closed_);
        if (vb.size() == 0) return;
        virtual_blocks_.push_back(vb);
        nitems_sum_.push_back(NumItems() + vb.nitems());
    }

    void Close() {
        assert(!closed_);
        closed_ = true;
    }

    // returns a string that identifies this string instance
    std::string ToString() {
        return "File@" + std::to_string((size_t) this);
    }

    bool closed() const {
        return closed_;
    }

    //! Return the number of blocks
    size_t NumBlocks() const { return virtual_blocks_.size(); }

    //! Return the number of items in the file
    size_t NumItems() const {
        return nitems_sum_.size() ? nitems_sum_.back() : 0;
    }

    //! Return the number of bytes used by the underlying blocks
    size_t TotalBytes() const { return NumBlocks() * block_size; }

    //! Return shared pointer to a block
    const VirtualBlock & virtual_block(size_t i) const {
        assert(i < virtual_blocks_.size());
        return virtual_blocks_[i];
    }

    //! Return number of items starting in block i
    size_t ItemsStartIn(size_t i) const {
        assert(i < virtual_blocks_.size());
        return nitems_sum_[i] - (i == 0 ? 0 : nitems_sum_[i - 1]);
    }

    //! Get BlockWriter.
    Writer GetWriter() {
        return Writer(this);
    }

    //! Get BlockReader for beginning of File
    Reader GetReader() const;

    //! Seek in File: return a VirtualBlock range containing items begin, end of
    //! given type.
    template <typename ItemType>
    std::vector<VirtualBlock> GetItemRange(size_t begin, size_t end) const;

protected:
    //! the container holding virtual blocks and thus shared pointers to all
    //! blocks.
    std::vector<VirtualBlock> virtual_blocks_;

    //! inclusive prefixsum of number of elements of blocks, hence
    //! nitems_sum_[i] is the number of items starting in all blocks preceding
    //! and including the i-th block.
    std::vector<size_t> nitems_sum_;

    //! for access to blocks_ and used_
    friend class data::FileBlockSource<BlockSize>;

    //! Closed files can not be altered
    bool closed_ = false;
};

//! Default File class, using the default block size.
using File = FileBase<default_block_size>;

/*!
 * A BlockSource to read Blocks from a File. The FileBlockSource mainly contains
 * an index to the current block, which is incremented when the NextBlock() must
 * be delivered.
 */
template <size_t BlockSize>
class FileBlockSource
{
public:
    using Byte = unsigned char;

    using FileBase = data::FileBase<BlockSize>;

    using Block = data::Block<BlockSize>;
    using BlockCPtr = std::shared_ptr<const Block>;
    using VirtualBlock = typename data::VirtualBlock<BlockSize>;

    //! Advance to next block of file, delivers current_ and end_ for
    //! BlockReader
    VirtualBlock NextBlock() {
        ++current_block_;

        if (current_block_ >= file_.NumBlocks())
            return VirtualBlock();

        if (current_block_ == first_block_) {
            // construct first block differently, in case we want to shorten it.
            VirtualBlock vb = file_.virtual_block(current_block_);
            vb.set_begin(first_offset_);
            return vb;
        }
        else {
            return file_.virtual_block(current_block_);
        }
    }

    bool closed() const {
        return file_.closed();
    }

protected:
    //! Start reading a File
    FileBlockSource(const FileBase& file,
                    size_t first_block = 0, size_t first_offset = 0)
        : file_(file), first_block_(first_block), first_offset_(first_offset) {
        current_block_ = first_block_ - 1;
    }

    //! for calling the protected constructor
    friend class data::FileBase<BlockSize>;
    friend class data::CachingBlockQueueSource<BlockSize>;

    //! file to read blocks from
    const FileBase& file_;

    //! index of current block.
    size_t current_block_ = -1;

    //! number of the first block
    size_t first_block_;

    //! offset of first item in first block read
    size_t first_offset_;
};

//! Get BlockReader for beginning of File
template <size_t BlockSize>
typename FileBase<BlockSize>::Reader FileBase<BlockSize>::GetReader() const {
    return Reader(FileBlockSource(*this, 0, 0));
}

//! Seek in File: return a VirtualBlock range containing items begin, end of
//! given type.
template <size_t BlockSize>
template <typename ItemType>
std::vector<typename FileBase<BlockSize>::VirtualBlock>
FileBase<BlockSize>::GetItemRange(size_t begin, size_t end) const {
    static const bool debug = false;
    assert(begin <= end);

    // perform binary search for item block with largest exclusive size
    // prefixsum less or equal to begin.
    auto it =
        std::lower_bound(nitems_sum_.begin(), nitems_sum_.end(), begin);

    if (it == nitems_sum_.end())
        return std::vector<VirtualBlock>();

    size_t begin_block = it - nitems_sum_.begin();

    sLOG << "item" << begin << "in block" << begin_block
         << "psum" << nitems_sum_[begin_block]
         << "first_item" << virtual_blocks_[begin_block].first_item();

    // start Reader at given first valid item in located block
    Reader fr(
        FileBlockSource(*this, begin_block,
                        virtual_blocks_[begin_block].first_item()));

    // skip over extra items in beginning of block
    size_t items_before = it == nitems_sum_.begin() ? 0 : *(it - 1);

    sLOG << "items_before" << items_before;

    for (size_t i = items_before; i < begin; ++i) {
        if (!fr.HasNext())
            die("Underflow in GetItemRange()");
        fr.template Next<ItemType>();
    }

    // deliver array of remaining virtual blocks
    return fr.GetItemRange<ItemType>(end - begin);
}

//! \}

} // namespace data
} // namespace c7a

#endif // !C7A_DATA_FILE_HEADER

/******************************************************************************/
