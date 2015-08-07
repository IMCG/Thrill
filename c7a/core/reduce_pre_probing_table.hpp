/*******************************************************************************
 * c7a/core/reduce_pre_probing_table.hpp
 *
 * Part of Project c7a.
 *
 * Copyright (C) 2015 Matthias Stumpp <mstumpp@gmail.com>
 *
 * This file has no license. Only Chunk Norris can compile it.
 ******************************************************************************/

#pragma once
#ifndef C7A_CORE_REDUCE_PRE_PROBING_TABLE_HEADER
#define C7A_CORE_REDUCE_PRE_PROBING_TABLE_HEADER

#include <c7a/common/function_traits.hpp>
#include <c7a/common/logger.hpp>
#include <c7a/data/block_writer.hpp>

#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

namespace c7a {
namespace core {

/**
 *
 * A data structure which takes an arbitrary value and extracts a key using
 * a key extractor function from that value. A key may also be provided initially as
 * part of a key/value pair, not requiring to extract a key.
 *
 * Afterwards, the key is hashed and the hash is used to assign that key/value pair
 * to some slot.
 *
 * In case a slot already has a key/value pair and the key of that value and the key of
 * the value to be inserted are them same, the values are reduced according to
 * some reduce function. No key/value is added to the data structure.
 *
 * If the keys are different, the next slot (moving to the right) is considered.
 * If the slot is occupied, the same procedure happens again (know as linear probing.)
 *
 * Finally, the key/value pair to be inserted may either:
 *
 * 1.) Be reduced with some other key/value pair, sharing the same key.
 * 2.) Inserted at a free slot.
 * 3.) Trigger a resize of the data structure in case there are no more free slots in
 *     the data structure.
 *
 * The following illustrations shows the general structure of the data structure.
 * The set of slots is divided into 1..n partitions. Each key is hashed into exactly
 * one partition.
 *
 *
 *     Partition 0 Partition 1 Partition 2 Partition 3 Partition 4
 *     P00 P01 P02 P10 P11 P12 P20 P21 P22 P30 P31 P32 P40 P41 P42
 *    +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *    ||  |   |   ||  |   |   ||  |   |   ||  |   |   ||  |   |  ||
 *    +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *                <-   LI  ->
 *                     LI..Local Index
 *    <-        GI         ->
 *              GI..Global Index
 *         PI 0        PI 1        PI 2        PI 3        PI 4
 *         PI..Partition ID
 *
 */
template <typename Key, typename HashFunction = std::hash<Key> >
class PreProbingReduceByHashKey
{
public:
    PreProbingReduceByHashKey(const HashFunction& hash_function = HashFunction())
        : hash_function_(hash_function)
    { }

    template <typename ReducePreProbingTable>
    typename ReducePreProbingTable::index_result
    operator () (Key v, ReducePreProbingTable* ht) const {

        using index_result = typename ReducePreProbingTable::index_result;

        size_t hashed = hash_function_(v);

        size_t local_index = hashed %
                             ht->NumItemsPerPartition();
        size_t partition_id = hashed % ht->NumPartitions();
        size_t global_index = partition_id *
                              ht->NumItemsPerPartition() +
                              local_index;
        return index_result(partition_id, local_index, global_index);
    }

private:
    HashFunction hash_function_;
};

template <typename Key, typename Value,
          typename KeyExtractor, typename ReduceFunction,
          const bool RobustKey = false,
          typename IndexFunction = PreProbingReduceByHashKey<Key>,
          typename EqualToFunction = std::equal_to<Key>
          >
class ReducePreProbingTable
{
    static const bool debug = false;

    typedef std::pair<Key, Value> KeyValuePair;

public:
    struct index_result
    {
    public:
        //! which partition number the item belongs to.
        size_t partition_id;
        //! index within the partition's sub-hashtable of this item
        size_t local_index;
        //! index within the whole hashtable
        size_t global_index;

        index_result(size_t p_id, size_t p_off, size_t g_id) {
            partition_id = p_id;
            local_index = p_off;
            global_index = g_id;
        }
    };

public:
    /**
     * A data structure which takes an arbitrary value and extracts a key using a key extractor
     * function from that value. Afterwards, the value is hashed based on the key into some slot.
     *
     * \param num_partitions The number of partitions.
     * \param key_extractor Key extractor function to extract a key from a value.
     * \param reduce_function Reduce function to reduce to values.
     * \param emit A set of BlockWriter to flush items. One BlockWriter per partition.
     * \param sentinel Sentinel element used to flag free slots.
     * \param num_items_init_scale Used to calculate the initial number of slots
     *                  (num_partitions * num_items_init_scale).
     * \param num_items_resize_scale Used to calculate the number of slots during resize
     *                  (size * num_items_resize_scale).
     * \param max_partition_fill_ratio Used to decide when to resize. If the current number of items
     *                  in some partitions divided by the number of maximal number of items per partition
     *                  is greater than max_partition_fill_ratio, resize.
     * \param max_num_items_table Maximal number of items allowed before some items are flushed. The items
     *                  of the partition with the most items gets flushed.
     * \param index_function Function to be used for computing the slot the item to be inserted.
     * \param equal_to_function Function for checking equality fo two keys.
     */
    ReducePreProbingTable(size_t num_partitions,
                          KeyExtractor key_extractor,
                          ReduceFunction reduce_function,
                          std::vector<data::BlockWriter>& emit,
                          Key sentinel,
                          size_t num_items_init_scale = 10,
                          size_t num_items_resize_scale = 2,
                          double max_partition_fill_ratio = 1.0,
                          size_t max_num_items_table = 1048576,
                          const IndexFunction& index_function = IndexFunction(),
                          const EqualToFunction& equal_to_function = EqualToFunction())
        : num_partitions_(num_partitions),
          num_items_init_scale_(num_items_init_scale),
          num_items_resize_scale_(num_items_resize_scale),
          max_partition_fill_ratio_(max_partition_fill_ratio),
          max_num_items_table_(max_num_items_table),
          key_extractor_(key_extractor),
          reduce_function_(reduce_function),
          emit_(emit),
          index_function_(index_function),
          equal_to_function_(equal_to_function)
    {
        assert(num_partitions >= 0);
        assert(num_partitions == emit_.size());
        assert(num_items_init_scale > 0);
        assert(num_items_resize_scale > 1);
        assert(max_partition_fill_ratio >= 0.0 && max_partition_fill_ratio <= 1.0);
        assert(max_num_items_table > 0);

        init(sentinel);
    }

    //! non-copyable: delete copy-constructor
    ReducePreProbingTable(const ReducePreProbingTable&) = delete;
    //! non-copyable: delete assignment operator
    ReducePreProbingTable& operator = (const ReducePreProbingTable&) = delete;

    ~ReducePreProbingTable() { }

    /**
     * Initializes the data structure by calculating some metrics based on input.
     *
     * \param sentinel Sentinel element used to flag free slots.
     */
    void init(Key sentinel) {

        sLOG << "creating ReducePreProbingTable with" << emit_.size() << "output emiters";
        for (size_t i = 0; i < emit_.size(); i++)
            emit_stats_.push_back(0);

        table_size_ = num_partitions_ * num_items_init_scale_;
        if (num_partitions_ > table_size_ &&
            table_size_ % num_partitions_ != 0) {
            throw std::invalid_argument("partition_size must be less than or equal to num_items "
                                        "AND partition_size a divider of num_items");
        }
        num_items_per_partition_ = table_size_ / num_partitions_;

        // set the key to initial key
        sentinel_ = KeyValuePair(sentinel, Value());
        vector_.resize(table_size_, sentinel_);
        items_per_partition_.resize(num_partitions_, 0);
    }

    /*!
     * Inserts a value. Calls the key_extractor_, makes a key-value-pair and
     * inserts the pair into the hashtable.
     */
    void Insert(const Value& p) {
        Key key = key_extractor_(p);

        Insert(std::make_pair(key, p));
    }

    /*!
     * Inserts a value into the table, potentially reducing it in case both the key of the value
     * already in the table and the key of the value to be inserted are the same.
     *
     * An insert may trigger a partial flush of the partition with the most items if the maximal
     * number of items in the table (max_num_items_table) is reached.
     *
     * Alternatively, it may trigger a resize of the table in case the maximal fill ratio
     * per partition is reached.
     *
     * \param p Value to be inserted into the table.
     */
    void Insert(const KeyValuePair& kv) {

        index_result h = index_function_(kv.first, this);

        if (!(h.partition_id >= 0 && h.partition_id < num_partitions_))
            std::cout << "bamm" << std::endl;

        assert(h.partition_id >= 0 && h.partition_id < num_partitions_);
        assert(h.local_index >= 0 && h.local_index < num_items_per_partition_);
        assert(h.global_index >= 0 && h.global_index < table_size_);

        KeyValuePair* initial = &vector_[h.global_index];
        KeyValuePair* current = initial;
        KeyValuePair* next_partition = &vector_[h.global_index -
                                                (h.global_index % num_items_per_partition_) + num_items_per_partition_];

        while (!equal_to_function_(current->first, sentinel_.first))
        {
            if (equal_to_function_(current->first, kv.first))
            {
                LOG << "match of key: " << kv.first
                    << " and " << current->first << " ... reducing...";

                current->second = reduce_function_(current->second, kv.second);

                LOG << "...finished reduce!";
                return;
            }

            ++current;

            if (current == next_partition)
            {
                current -= num_items_per_partition_;
            }

            if (current == initial)
            {
                ResizeUp();
                Insert(std::move(kv));
                return;
            }
        }

        // insert new pair
        if (equal_to_function_(current->first, sentinel_.first))
        {
            current->first = std::move(kv.first);
            current->second = std::move(kv.second);

            // increase total counter
            num_items_++;

            // increase counter for partition
            items_per_partition_[h.partition_id]++;
        }

        if (num_items_ > max_num_items_table_)
        {
            LOG << "flush";
            FlushLargestPartition();
        }

        if (static_cast<double>(items_per_partition_[h.partition_id]) /
            static_cast<double>(num_items_per_partition_)
            > max_partition_fill_ratio_)
        {
            LOG << "resize";
            ResizeUp();
        }
    }

    /*!
    * Flushes all items in the whole table.
    */
    void Flush() {
        LOG << "Flushing all items";

        // retrieve items
        for (size_t i = 0; i < num_partitions_; i++)
        {
            FlushPartition(i);
        }

        LOG << "Flushed all items";
    }

    /*!
     * Retrieves all items belonging to the partition
     * having the most items. Retrieved items are then pushed
     * to the provided emitter.
     */
    void FlushLargestPartition() {
        LOG << "Flushing items of largest partition";

        // get partition with max size
        size_t p_size_max = 0;
        size_t p_idx = 0;
        for (size_t i = 0; i < num_partitions_; i++)
        {
            if (items_per_partition_[i] > p_size_max)
            {
                p_size_max = items_per_partition_[i];
                p_idx = i;
            }
        }

        LOG << "currMax: "
            << p_size_max
            << " currentIdx: "
            << p_idx
            << " currentIdx*p_size: "
            << p_idx * num_items_per_partition_
            << " CurrentIdx*p_size+p_size-1 "
            << p_idx * num_items_per_partition_ + num_items_per_partition_ - 1;

        LOG << "Largest patition id: "
            << p_idx;

        FlushPartition(p_idx);

        LOG << "Flushed items of largest partition";
    }

    /*!
     * Flushes all items of a partition.
     *
     * \param partition_id The id of the partition to be flushed.
     */
    void FlushPartition(size_t partition_id) {
        LOG << "Flushing items of partition with id: "
            << partition_id;

        for (size_t i = partition_id * num_items_per_partition_;
             i < partition_id * num_items_per_partition_ + num_items_per_partition_; i++)
        {
            KeyValuePair current = vector_[i];
            if (current.first != sentinel_.first)
            {
                if (RobustKey) {
                    emit_[partition_id](std::move(current.second));
                }
                else {
                    emit_[partition_id](std::move(current));
                }
                vector_[i] = sentinel_;
            }
        }

        // reset total counter
        num_items_ -= items_per_partition_[partition_id];
        // reset partition specific counter
        items_per_partition_[partition_id] = 0;
        // flush elements pushed into emitter
        emit_[partition_id].Flush();

        LOG << "Flushed items of partition with id: "
            << partition_id;
    }

    /*!
     * Returns the size of the table. The size corresponds to the number of slots.
     * A slot may be free or used.
     *
     * @return Size of the table.
     */
    size_t Size() const {
        return table_size_;
    }

    /*!
     * Returns the total num of items in the table in all partitions.
     *
     * @return Number of items in the table.
     */
    size_t NumItems() const {
        return num_items_;
    }

    /*!
     * Returns the maximal number of items any partition can hold.
     *
     * @return Maximal number of items a partition can hold.
     */
    size_t NumItemsPerPartition() const {
        return num_items_per_partition_;
    }

    /*!
     * Returns the number of partitions.
     *
     * @return The number of partitions.
     */
    size_t NumPartitions() const {
        return num_partitions_;
    }

    /*!
     * Returns the number of items of a partition.
     *
     * \param partition_id The id of the partition the number of
     *                  items to be returned..
     * @return The number of items in the partitions.
     */
    size_t PartitionNumItems(size_t partition_id) {
        return items_per_partition_[partition_id];
    }

    /*!
     * Sets the maximum number of items of the hash table. We don't want to push 2vt
     * elements before flush happens.
     *
     * \param size The maximal number of items the table may hold.
     */
    void SetMaxNumItems(size_t size) {
        max_num_items_table_ = size;
    }

    /*!
     * Closes all emitter.
     */
    void CloseEmitter() {
        sLOG << "emit stats: ";
        unsigned int i = 0;
        for (auto& e : emit_) {
            e.Close();
            sLOG << "emitter " << i << " pushed " << emit_stats_[i++];
        }
    }

    /*!
     * Resizes the table by increasing the number of slots using some
     * scale factor (num_items_resize_scale_). All items are rehashed as
     * part of the operation.
     */
    void ResizeUp() {
        LOG << "Resizing";
        table_size_ *= num_items_resize_scale_;
        num_items_per_partition_ = table_size_ / num_partitions_;
        // reset items_per_partition and table_size
        std::fill(items_per_partition_.begin(), items_per_partition_.end(), 0);
        num_items_ = 0;

        // move old hash array
        std::vector<KeyValuePair> vector_old;
        std::swap(vector_old, vector_);

        // init new hash array
        vector_.resize(table_size_, sentinel_);

        // rehash all items in old array
        for (KeyValuePair k_v_pair : vector_old)
        {
            if (k_v_pair.first != sentinel_.first)
            {
                Insert(std::move(k_v_pair.second));
            }
        }
        LOG << "Resized";
    }

    /*!
     * Removes all items from the table, but does not flush them nor does
     * it resets the table to it's initial size.
     */
    void Clear() {
        LOG << "Clearing";

        for (KeyValuePair k_v_pair : vector_)
        {
            k_v_pair.first = sentinel_.first;
            k_v_pair.second = sentinel_.second;
        }

        std::fill(items_per_partition_.begin(), items_per_partition_.end(), 0);
        num_items_ = 0;
        LOG << "Cleared";
    }

    /*!
     * Removes all items from the table, but does not flush them. However, it does
     * reset the table to it's initial size.
     */
    void Reset() {
        LOG << "Resetting";
        table_size_ = num_partitions_ * num_items_init_scale_;
        num_items_per_partition_ = table_size_ / num_partitions_;

        for (KeyValuePair k_v_pair : vector_)
        {
            k_v_pair.first = sentinel_.first;
            k_v_pair.second = sentinel_.second;
        }

        vector_.resize(table_size_, sentinel_);
        std::fill(items_per_partition_.begin(), items_per_partition_.end(), 0);
        num_items_ = 0;
        LOG << "Resetted";
    }

    /*!
    * Prints content of hash table.
    */
    void Print() {

        std::string log = "Printing\n";

        for (size_t i = 0; i < table_size_; i++)
        {
            if (vector_[i].first == sentinel_.first)
            {
                log += "item: ";
                log += std::to_string(i);
                log += " empty\n";
                continue;
            }

            log += "item: ";
            log += std::to_string(i);
            log += " (";
            //log += std::is_arithmetic<Key>::value || strcmp(typeid(Key).name(), "string")
            //       ? std::to_string(vector_[i].first) : "_";
            log += ", ";
            //log += std::is_arithmetic<Value>::value || strcmp(typeid(Value).name(), "string")
            //       ? std::to_string(vector_[i].second) : "_";
            log += ")\n";
        }

        std::cout << log << std::endl;

        return;
    }

private:
    //! Number of partitions
    size_t num_partitions_;

    //! Scale factor to compute the initial size
    //! (=number of slots for items).
    size_t num_items_init_scale_;

    //! Scale factor to compute the number of slots
    //! during resize relative to current size.
    size_t num_items_resize_scale_;

    //! Maximal allowed fill ratio per partition before
    //! resize.
    double max_partition_fill_ratio_;

    //! Maximal number of items before some items
    //! are flushed (-> partial flush).
    size_t max_num_items_table_;

    //! Keeps the total number of items in the table.
    size_t num_items_ = 0;

    //! Maximal number of items allowed per partition.
    size_t num_items_per_partition_;

    //! Number of items per partition.
    std::vector<size_t> items_per_partition_;

    //! Size of the table, which is the number of slots
    //! available for items.
    size_t table_size_ = 0;

    //! Key extractor function for extracting a key from a value.
    KeyExtractor key_extractor_;

    //! Reduce function for reducing two values.
    ReduceFunction reduce_function_;

    //! Set of emitters, one per partition.
    std::vector<data::BlockWriter>& emit_;

    //! Emitter stats.
    std::vector<int> emit_stats_;

    //! Data structure for actually storing the items.
    std::vector<KeyValuePair> vector_;

    //! Sentinel element used to flag free slots.
    KeyValuePair sentinel_;

    //! Hash functions.
    IndexFunction index_function_;

    //! Comparator function for keys.
    EqualToFunction equal_to_function_;
};

} // namespace core
} // namespace c7a

#endif // !C7A_CORE_REDUCE_PRE_PROBING_TABLE_HEADER

/******************************************************************************/
