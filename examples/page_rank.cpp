/*******************************************************************************
 * examples/page_rank.cpp
 *
 * Part of Project c7a.
 *
 *
 * This file has no license. Only Chunk Norris can compile it.
 ******************************************************************************/

#include <c7a/api/cache.hpp>
#include <c7a/api/collapse.hpp>
#include <c7a/api/read_lines.hpp>
#include <c7a/api/reduce_to_index.hpp>
#include <c7a/api/size.hpp>
#include <c7a/api/sum.hpp>
#include <c7a/api/write_lines.hpp>
#include <c7a/api/zip.hpp>
#include <c7a/common/cmdline_parser.hpp>
#include <c7a/common/string.hpp>

#include <random>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

using c7a::DIARef;
using c7a::Context;

//! The PageRank user program
void page_rank(Context& ctx) {

    static const double s = 0.85;

	using PageWithLinks = std::pair<size_t, std::vector<int> >;
    using PageWithRank = std::pair<size_t, double>;
    using Page = std::tuple<size_t, double, std::vector<int> >;

    DIARef<PageWithRank> ranks =
        ReadLines(ctx, "pagerank.in")
        .Map([](const std::string& input) {				
				auto splitted = c7a::common::split(input, " ");
				return std::make_pair((size_t) std::stoi(splitted[0]), 1.0);
			}).Cache();

	size_t size = ranks.Size();

    auto links = ReadLines(ctx, "pagerank.in")
                 .Map([](const std::string& line) {
                          auto splitted = c7a::common::split(line, " ");
                          std::vector<int> links;
                          links.reserve(splitted.size() - 1);
                          for (size_t i = 1; i < splitted.size(); i++) {
                              links.push_back(std::stoi(splitted[i]));
                          }
                          return std::make_pair(std::stoi(splitted[0]), links);
					 });

    for (size_t i = 1; i <= 10; ++i) {
        std::cout << "Iteration: " << i << std::endl;

        auto pages =
            links
            .Zip(ranks, [](PageWithLinks first, PageWithRank second) {
					return std::make_tuple(first.first, second.second, first.second);
                 });

        auto contribs =
            pages
            .FlatMap<PageWithRank>(
                [](Page page, auto emit) {
                    std::vector<int> urls = std::get<2>(page);
                    double rank = std::get<1>(page);
                    double num_urls = urls.size();
                    for (int url : urls) {
                        emit(std::make_pair(url, rank / num_urls));
                    }
                });

        ranks =
            contribs
            .ReducePairToIndex(
                [](double rank1, double rank2) {
                    return rank1 + rank2;
                },
                size)
			.Map([](PageWithRank page) {
			 		return std::make_pair(page.first, (1 - s) + page.second * s);
			 	})
			.Cache();

		pages.Size();
    }

    ranks.Map([](PageWithRank item) {
                  return std::to_string(item.first)
                  + ": " + std::to_string(item.second);
              }).
    WriteLines("pagerank.out");
}

int main(int, char**) {
    return c7a::api::Run(page_rank);
}

/******************************************************************************/
