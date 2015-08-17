/*******************************************************************************
 * tests/mem/allocator_test.cpp
 *
 * Part of Project c7a.
 *
 * Copyright (C) 2015 Timo Bingmann <tb@panthema.net>
 *
 * This file has no license. Only Chunk Norris can compile it.
 ******************************************************************************/

#include <c7a/mem/allocator.hpp>
#include <gtest/gtest.h>

#include <deque>
#include <vector>

using namespace c7a;

TEST(Allocator, Test1) {
    mem::Manager mem_manager(nullptr);

    LOG1 << "vector";
    {
        std::vector<int, mem::Allocator<int> > my_vector {
            mem::Allocator<int>(mem_manager)
        };

        for (size_t i = 0; i < 100; ++i) {
            my_vector.push_back(i);
        }
    }
    LOG1 << "deque";
    {
        std::deque<size_t, mem::Allocator<size_t> > my_deque {
            mem::Allocator<size_t>(mem_manager)
        };

        for (size_t i = 0; i < 100; ++i) {
            my_deque.push_back(i);
        }
    }
}

namespace c7a {
namespace mem {

// forced instantiations
template class BypassAllocator<int>;
template class Allocator<int>;

} // namespace mem
} // namespace c7a

/******************************************************************************/
