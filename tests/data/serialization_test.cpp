/*******************************************************************************
 * tests/data/serialization_test.cpp
 *
 * Part of Project c7a.
 *
 *
 * This file has no license. Only Chunk Norris can compile it.
 ******************************************************************************/

#include <c7a/common/logger.hpp>
#include <c7a/data/block_queue.hpp>
#include <c7a/data/file.hpp>
#include <c7a/data/serialization.hpp>
#include <gtest/gtest.h>

#include <string>
#include <tuple>
#include <typeinfo>
#include <utility>

using namespace c7a::data;

static const bool debug = false;

TEST(Serialization, string) {
    c7a::data::File f;
    std::string foo = "foo";
    {
        auto w = f.GetWriter();
        w(foo); //gets serialized
    }
    auto r = f.GetReader();
    auto fooserial = r.Next<decltype(foo)>();
    ASSERT_EQ(foo, fooserial);
    ASSERT_EQ(typeid(foo).name(), typeid(fooserial).name());
}

TEST(Serialization, int) {
    int foo = -123;
    c7a::data::File f;
    {
        auto w = f.GetWriter();
        w(foo); //gets serialized
    }
    auto r = f.GetReader();
    auto fooserial = r.Next<decltype(foo)>();
    ASSERT_EQ(foo, fooserial);
    ASSERT_EQ(typeid(foo).name(), typeid(fooserial).name());
}

TEST(Serialization, pair_string_int) {
    auto foo = std::make_pair("foo", 123);
    c7a::data::File f;
    {
        auto w = f.GetWriter();
        w(foo); //gets serialized
    }
    auto r = f.GetReader();
    auto fooserial = r.Next<decltype(foo)>();
    ASSERT_EQ(std::get<0>(foo), std::get<0>(fooserial));
    ASSERT_EQ(std::get<1>(foo), std::get<1>(fooserial));
}

TEST(Serialization, pair_int_int) {
    auto t1 = 3;
    auto t2 = 4;
    auto foo = std::make_pair(t1, t2);
    c7a::data::File f;
    {
        auto w = f.GetWriter();
        w(foo); //gets serialized
    }
    auto r = f.GetReader();
    auto fooserial = r.Next<decltype(foo)>();
    ASSERT_EQ(std::get<0>(foo), std::get<0>(fooserial));
    ASSERT_EQ(std::get<1>(foo), std::get<1>(fooserial));
}

TEST(Serialization, tuple) {
    auto foo = std::make_tuple(3, "foo", 5.5);
    c7a::data::File f;
    {
        auto w = f.GetWriter();
        w(foo); //gets serialized
    }
    auto r = f.GetReader();
    auto fooserial = r.Next<decltype(foo)>();
    ASSERT_EQ(std::get<0>(foo), std::get<0>(fooserial));
    ASSERT_EQ(std::get<1>(foo), std::get<1>(fooserial));
    ASSERT_EQ(std::get<2>(foo), std::get<2>(fooserial));
}

TEST(Serialization, tuple_w_pair) {
    auto p = std::make_pair(-4.673, "string");
    auto foo = std::make_tuple(3, "foo", 5.5, p);
    c7a::data::File f;
    {
        auto w = f.GetWriter();
        w(foo); //gets serialized
    }
    ASSERT_EQ(1u, f.NumItems());
    auto r = f.GetReader();
    auto fooserial = r.Next<decltype(foo)>();
    ASSERT_EQ(std::get<0>(foo), std::get<0>(fooserial));
    ASSERT_EQ(std::get<1>(foo), std::get<1>(fooserial));
    ASSERT_EQ(std::get<2>(foo), std::get<2>(fooserial));
    ASSERT_FLOAT_EQ(std::get<3>(foo).first, std::get<3>(fooserial).first);
    ASSERT_EQ(std::get<3>(foo).second, std::get<3>(fooserial).second);
}

TEST(Serialization, tuple_check_fixed_size) {
    c7a::data::File f;
    auto n = std::make_tuple(1, 2, 3, std::string("blaaaa"));
    auto y = std::make_tuple(1, 2, 3, "4");
    auto w = f.GetWriter();
    auto no = Serialization<decltype(w), decltype(n)>::fixed_size;
    auto yes = Serialization<decltype(w), decltype(y)>::fixed_size;

    ASSERT_EQ(no, false);
    ASSERT_EQ(yes, true);
}

TEST(Serialization, StringVector) {
    std::vector<std::string> vec1 = {
        "what", "a", "wonderful", "world", "this", "could", "be"
    };
    c7a::data::File f;
    {
        auto w = f.GetWriter();
        w(vec1);
        w(static_cast<int>(42));
    }
    ASSERT_EQ(2u, f.NumItems());
    auto r = f.GetReader();
    auto vec2 = r.Next<std::vector<std::string> >();
    ASSERT_EQ(7u, vec1.size());
    ASSERT_EQ(vec1, vec2);

    auto check42 = r.Next<int>();
    ASSERT_EQ(42, check42);
}

TEST(Serialization, StringArray) {
    std::array<std::string, 7> vec1 = {
        "what", "a", "wonderful", "world", "this", "could", "be"
    };
    c7a::data::File f;
    {
        auto w = f.GetWriter();
        w(vec1);
        w(static_cast<int>(42));
    }
    ASSERT_EQ(2u, f.NumItems());
    auto r = f.GetReader();
    auto vec2 = r.Next<std::array<std::string, 7> >();
    ASSERT_EQ(7u, vec1.size());
    ASSERT_EQ(vec1, vec2);

    auto check42 = r.Next<int>();
    ASSERT_EQ(42, check42);
}

/******************************************************************************/
