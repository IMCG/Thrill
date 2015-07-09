/*******************************************************************************
 * tests/api/operations_test.cpp
 *
 * Part of Project c7a.
 *
 * Copyright (C) 2015 Alexander Noe <aleexnoe@gmail.com>
 * Copyright (C) 2015 Timo Bingmann <tb@panthema.net>
 *
 * This file has no license. Only Chunk Norris can compile it.
 ******************************************************************************/

#include <c7a/api/dia.hpp>
#include <c7a/api/generate_node.hpp>
#include <c7a/api/generate_file_node.hpp>
#include <c7a/api/write_node.hpp>
#include <c7a/api/allgather_node.hpp>
#include <c7a/api/read_node.hpp>
#include <c7a/api/bootstrap.hpp>
#include <c7a/net/endpoint.hpp>

#include <c7a/c7a.hpp>

#include <algorithm>
#include <random>
#include <string>

#include "gtest/gtest.h"

TEST(Operations, GenerateFromFileCorrectAmountOfCorrectIntegers) {

    std::vector<std::string> self = { "127.0.0.1:1234" };
	c7a::core::JobManager jobMan;
    jobMan.Connect(0, c7a::net::Endpoint::ParseEndpointList(self), 1);
	c7a::Context ctx(jobMan, 0);

    std::random_device random_device;
    std::default_random_engine generator(random_device());
    std::uniform_int_distribution<int> distribution(1000, 10000);

    size_t generate_size = distribution(generator);

    auto input = GenerateFromFile(
        ctx,
        "test1",
        [](const std::string& line) {
            return std::stoi(line);
        },
        generate_size);

    size_t writer_size = 0;

    input.WriteToFileSystem("test1.out",
                            [&writer_size](const int& item) {
                                //file contains ints between 1  and 15
                                //fails if wrong integer is generated
                                EXPECT_GE(item, 1);
                                EXPECT_GE(16, item);
                                writer_size++;
                                return std::to_string(item);
                            });

    ASSERT_EQ(generate_size, writer_size);
}

TEST(Operations, ReadAndAllGatherElementsCorrect) {

    std::function<void(c7a::Context&)> start_func =
        [](c7a::Context& ctx) {

        auto integers = ReadLines(
            ctx,
            "test1",
            [](const std::string& line) {
                return std::stoi(line);
            });

        std::vector<int> out_vec;

        integers.AllGather(&out_vec);

        std::sort(out_vec.begin(), out_vec.end());

        int i = 1;
        for (int element : out_vec) {
            ASSERT_EQ(element, i++);
        }

        ASSERT_EQ((size_t)16, out_vec.size());
    };

    c7a::api::ExecuteLocalTests(start_func);
}

TEST(Operations, MapResultsCorrectChangingType) {

    std::function<void(c7a::Context&)> start_func =
        [](c7a::Context& ctx) {

        auto integers = Generate(
            ctx,
            [](const size_t& index) {
                return (int)index + 1;
            },
            16);

        std::function<double(int)> double_elements = [](int in) {
            return (double)2 * in;
        };

        auto doubled = integers.Map(double_elements);

        std::vector<double> out_vec;

        doubled.AllGather(&out_vec);

        std::sort(out_vec.begin(), out_vec.end());

        int i = 1;
        for (int element : out_vec) {
            ASSERT_DOUBLE_EQ(element, (i++ *2));
        }

        ASSERT_EQ((size_t)16, out_vec.size());
        static_assert(std::is_same<decltype(doubled)::ItemType, double>::value, "DIA must be double");
        static_assert(std::is_same<decltype(doubled)::StackInput, int>::value, "Node must be int");
    };

    c7a::api::ExecuteLocalTests(start_func);
}

TEST(Operations, FlatMapResultsCorrectChangingType) {

    std::function<void(c7a::Context&)> start_func =
        [](c7a::Context& ctx) {

        auto integers = Generate(
            ctx,
            [](const size_t& index) {
                return (int)index + 1;
            },
            16);

        auto flatmap_double = [](int in, auto emit) {
            emit((double)2 * in);
            emit((double)2 * (in + 16));
        };

        auto doubled = integers.FlatMap<double>(flatmap_double);

        std::vector<double> out_vec;

        doubled.AllGather(&out_vec);

        std::sort(out_vec.begin(), out_vec.end());

        int i = 1;
        for (int element : out_vec) {
            ASSERT_DOUBLE_EQ(element, (i++ *2));
        }

        ASSERT_EQ((size_t)32, out_vec.size());
        static_assert(std::is_same<decltype(doubled)::ItemType, double>::value, "DIA must be double");
        static_assert(std::is_same<decltype(doubled)::StackInput, int>::value, "Node must be int");
    };

    c7a::api::ExecuteLocalTests(start_func);
}

TEST(Operations, PrefixSumCorrectResults) {


    std::function<void(c7a::Context&)> start_func =
		[](c7a::Context& ctx) {

		auto integers = Generate(
			ctx,
			[](const size_t& input) {
				return input + 1;
			},
			16);

		auto prefixsums = integers.PrefixSum(
			[](size_t in1, size_t in2) {
				return in1 + in2;
			});

		std::vector<size_t> out_vec;

		prefixsums.AllGather(&out_vec);

		std::sort(out_vec.begin(), out_vec.end());
		size_t ctr = 0;
		for (size_t i = 0; i < out_vec.size(); i++) {
			ctr += i + 1;
			ASSERT_EQ(out_vec[i], ctr);
		}

		ASSERT_EQ((size_t)16, out_vec.size());
	};

    c7a::api::ExecuteLocalTests(start_func);
}

TEST(Operations, PrefixSumFacultyCorrectResults) {

    std::function<void(c7a::Context&)> start_func =
		[](c7a::Context& ctx) {

		auto integers = Generate(
			ctx,
			[](const size_t& input) {
				return input + 1;
			},
			10);

		auto prefixsums = integers.PrefixSum(
			[](size_t in1, size_t in2) {
				return in1 * in2;
			}, 1);

		std::vector<size_t> out_vec;

		prefixsums.AllGather(&out_vec);

		std::sort(out_vec.begin(), out_vec.end());
		size_t ctr = 1;
		for (size_t i = 0; i < out_vec.size(); i++) {
			ctr *= i + 1;
			ASSERT_EQ(out_vec[i], ctr);
		}

		ASSERT_EQ((size_t)10, out_vec.size());
	};

    c7a::api::ExecuteLocalTests(start_func);
}

TEST(Operations, FilterResultsCorrectly) {

    std::function<void(c7a::Context&)> start_func =
        [](c7a::Context& ctx) {

        auto integers = Generate(
            ctx,
            [](const size_t& index) {
                return (int)index + 1;
            },
            16);

        std::function<bool(int)> even = [](int in) {
            return (in % 2 == 0);
        };

        auto doubled = integers.Filter(even);

        std::vector<int> out_vec;

        doubled.AllGather(&out_vec);

        std::sort(out_vec.begin(), out_vec.end());

        int i = 1;

        for (int element : out_vec) {
            ASSERT_DOUBLE_EQ(element, (i++ *2));
        }

        ASSERT_EQ((size_t)8, out_vec.size());
    };

    c7a::api::ExecuteLocalTests(start_func);
}

/******************************************************************************/
