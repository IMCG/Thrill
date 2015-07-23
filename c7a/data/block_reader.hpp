/*******************************************************************************
 * c7a/data/block_reader.hpp
 *
 * Part of Project c7a.
 *
 * Copyright (C) 2015 Timo Bingmann <tb@panthema.net>
 *
 * This file has no license. Only Chunk Norris can compile it.
 ******************************************************************************/

#pragma once
#ifndef C7A_DATA_BLOCK_READER_HEADER
#define C7A_DATA_BLOCK_READER_HEADER

#include <c7a/common/config.hpp>
#include <c7a/common/item_serializer_tools.hpp>
#include <c7a/common/logger.hpp>
#include <c7a/data/block.hpp>
#include <c7a/data/serializer.hpp>

#include <algorithm>
#include <string>

namespace c7a {
namespace data {

//! \addtogroup data Data Subsystem
//! \{

/*!
 * BlockReader takes VirtualBlock objects from BlockSource and allows reading of
 * a) serializable Items or b) arbitray data from the Block sequence. It takes
 * care of fetching the next Block when the previous one underruns and also of
 * data items split between two Blocks.
 */
template <typename BlockSource>
class BlockReader
    : public common::ItemReaderToolsBase<BlockReader<BlockSource> >
{
public:
    static const bool self_verify = common::g_self_verify;

    using Byte = unsigned char;

    using Block = typename BlockSource::Block;
    using BlockCPtr = std::shared_ptr<const Block>;
    using VirtualBlock = typename BlockSource::VirtualBlock;

    //! Start reading a File
    explicit BlockReader(BlockSource&& source)
        : source_(std::move(source)) { }

    //! Return reference to enclosed BlockSource
    BlockSource & source() { return source_; }

    //! \name Reading (Generic) Items
    //! \{

    //! Next() reads a complete item T
    template <typename T>
    T Next() {
        assert(HasNext());
        assert(nitems_ > 0);
        --nitems_;

        if (self_verify) {
            // for self-verification, T is prefixed with its hash code
            size_t code = Get<size_t>();
            if (code != typeid(T).hash_code()) {
                throw std::runtime_error(
                          "BlockReader::Next() attempted to retrieve item "
                          "with different typeid!");
            }
        }

        return Serializer<BlockReader, T>::deserialize(*this);
    }

    //! HasNext() returns true if at least one more byte is available.
    bool HasNext() {
        while (current_ == end_) {
            if (!NextBlock())
                return false;
        }
        return true;
    }

    //! Return complete contents until empty as a std::vector<T>. Use this only
    //! if you are sure that it will fit into memory, -> only use it for tests.
    template <typename ItemType>
    std::vector<ItemType> ReadComplete() {
        std::vector<ItemType> out;
        while (HasNext()) out.emplace_back(Next<ItemType>());
        return out;
    }

    //! Read n items, however, do not deserialize them but deliver them as a
    //! vector of VirtualBlock objects. This is used to take out a range of
    //! items, the internal item cursor is advanced by n.
    template <typename ItemType>
    std::vector<VirtualBlock> GetItemRange(size_t n) {
        static const bool debug = false;

        std::vector<VirtualBlock> out;

        while (current_ == end_) {
            if (!NextBlock()) {
                // no items left
                return out;
            }
        }

        const Byte* begin_output = current_;
        size_t first_output = current_ - block_->begin();

        // inside the if-clause the current_ may not point to a valid item
        // boundary.
        if (n >= nitems_)
        {
            // if the current block still contains items, push it partially

            if (n >= nitems_) {
                // construct first VirtualBlock using current_ pointer
                out.emplace_back(
                    block_,
                    // valid range: excludes preceding items.
                    current_ - block_->begin(), end_ - block_->begin(),
                    // first item is at begin_ (we may have dropped some)
                    current_ - block_->begin(),
                    // remaining items in this block
                    nitems_);

                sLOG << "partial first:" << out.back();

                n -= nitems_;

                // get next block. if not possible -> may be okay since last item
                // might just terminate the current block.
                if (!NextBlock())
                    return out;
            }

            // then append complete blocks without deserializing them

            while (n >= nitems_) {
                out.emplace_back(
                    block_,
                    // full range is valid.
                    current_ - block_->begin(), end_ - block_->begin(),
                    first_item_, nitems_);

                sLOG << "middle:" << out.back();

                n -= nitems_;

                if (!NextBlock())
                    return out;
            }

            // move current_ to the first valid item of the block we got (at
            // least one NextBlock() has been called). But when constructing the
            // last VirtualBlock, we have to include the partial item in the
            // front.
            begin_output = current_;
            first_output = first_item_;

            current_ = block_->begin() + first_item_;
        }

        // skip over remaining items in this block
        BlockCPtr b = block_;
        size_t last_items = n;

        while (n > 0) {
            Next<ItemType>();
            --n;
        }
        assert(b == block_); // must still be in the same block.

        out.emplace_back(
            block_,
            // full range is valid.
            begin_output - block_->begin(), current_ - block_->begin(),
            first_output, last_items);

        sLOG << "partial last:" << out.back();

        return out;
    }

    //! \}

    //! \name Cursor Reading Methods
    //! \{

    //! Fetch a number of unstructured bytes from the current block, advancing
    //! the cursor.
    BlockReader & Read(void* outdata, size_t size) {

        Byte* cdata = reinterpret_cast<Byte*>(outdata);

        while (current_ + size > end_) {
            // partial copy of remainder of block
            size_t partial_size = end_ - current_;
            std::copy(current_, current_ + partial_size, cdata);

            cdata += partial_size;
            size -= partial_size;

            if (!NextBlock())
                throw std::runtime_error("Data underflow in BlockReader.");
        }

        // copy rest from current block
        std::copy(current_, current_ + size, cdata);
        current_ += size;

        return *this;
    }

    //! Fetch a number of unstructured bytes from the buffer as std::string,
    //! advancing the cursor.
    std::string Read(size_t datalen) {
        std::string out(datalen, 0);
        Read(const_cast<char*>(out.data()), out.size());
        return out;
    }

    //! Fetch a single byte from the current block, advancing the cursor.
    Byte GetByte() {
        // loop, since blocks can actually be empty.
        while (current_ == end_) {
            if (!NextBlock())
                throw std::runtime_error("Data underflow in BlockReader.");
        }
        return *current_++;
    }

    //! Fetch a single item of the template type Type from the buffer,
    //! advancing the cursor. Be careful with implicit type conversions!
    template <typename Type>
    Type Get() {
        static_assert(std::is_pod<Type>::value,
                      "You only want to Get() POD types as raw values.");

        Type ret;
        Read(&ret, sizeof(ret));
        return ret;
    }

    //! \}

protected:
    //! Instance of BlockSource. This is NOT a reference, as to enable embedding
    //! of FileBlockSource to compose classes into File::Reader.
    BlockSource source_;

    //! The current block being read, this holds a shared pointer reference.
    BlockCPtr block_;

    //! current read pointer into current block of file.
    const Byte* current_ = nullptr;

    //! pointer to end of current block.
    const Byte* end_ = nullptr;

    //! offset of first valid item in block (needed only during direct copying
    //! of VirtualBlocks).
    size_t first_item_;

    //! remaining number of items starting in this block
    size_t nitems_;

    //! Call source_.NextBlock with appropriate parameters
    bool NextBlock() {
        VirtualBlock vb = source_.NextBlock();
        block_ = vb.block();
        if (!vb.IsValid()) return false;
        current_ = vb.data_begin();
        end_ = vb.data_end();
        first_item_ = vb.first_item();
        nitems_ = vb.nitems();
        return true;
    }
};

//! \}

} // namespace data
} // namespace c7a

#endif // !C7A_DATA_BLOCK_READER_HEADER

/******************************************************************************/
