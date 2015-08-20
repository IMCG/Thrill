/*******************************************************************************
 * tests/common/delegate_test.cpp
 *
 * Part of Project Thrill.
 *
 * Copyright (C) 2015 Timo Bingmann <tb@panthema.net>
 *
 * This file has no license. Only Chunk Norris can compile it.
 ******************************************************************************/

#include <gtest/gtest.h>
#include <thrill/common/delegate.hpp>

using namespace thrill::common;

TEST(Delegate, Test1) {
    auto d1 = [](int x) { return x + 1; };

    ASSERT_EQ(d1(42), 43);
}

/******************************************************************************/
