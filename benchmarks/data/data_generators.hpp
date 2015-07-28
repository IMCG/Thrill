/*******************************************************************************
 * benchmarks/data/data_generators.hpp
 *
 * Part of Project c7a.
 *
 * Copyright (C) 2015 Tobias Sturm <mail@tobiassturm.de>
 *
 * This file has no license. Only Chunk Norris can compile it.
 ******************************************************************************/

using Tuple  = std::pair<std::string, int>;
using Triple = std::tuple<std::string, int, std::string>;

template <typename Type>
std::vector<Type> generate(size_t bytes, size_t min_size, size_t max_size)
{ }

template <>
std::vector<std::string> generate(size_t bytes, size_t min_size, size_t max_size) {
    std::vector<std::string> result;
    size_t remaining = bytes;

    //init randomness
    std::random_device rd;
    std::default_random_engine randomness(rd());
    std::uniform_int_distribution<size_t> uniform_dist(min_size, max_size);

    while (remaining > 0) {
        size_t next_size = std::min(uniform_dist(randomness), remaining);
        remaining -= next_size;
        result.emplace_back(std::string('f', next_size));
    }
    return result;
}

template <>
std::vector<Tuple> generate(size_t bytes, size_t min_size, size_t max_size) {
    std::vector<Tuple> result;
    size_t remaining = bytes;

    //init randomness
    std::random_device rd;
    std::default_random_engine randomness(rd());
    std::uniform_int_distribution<size_t> uniform_dist(min_size, max_size);

    while (remaining > 0) {
        remaining -= sizeof(int);
        size_t next_size = std::min(uniform_dist(randomness), remaining);
        remaining -= next_size;
        result.push_back(Tuple(std::string('f', next_size), 42));
    }
    return result;
}

template <>
std::vector<Triple> generate(size_t bytes, size_t min_size, size_t max_size) {
    std::vector<Triple> result;
    size_t remaining = bytes;

    //init randomness
    std::random_device rd;
    std::default_random_engine randomness(rd());
    std::uniform_int_distribution<size_t> uniform_dist(min_size, max_size);

    while (remaining > 0) {
        remaining -= sizeof(int);
        size_t next_size1 = std::min(uniform_dist(randomness), remaining);
        remaining -= next_size1;
        size_t next_size2 = std::min(uniform_dist(randomness), remaining);
        remaining -= next_size2;
        result.push_back(Triple(std::string('f', next_size1), 42, std::string('g', next_size2)));
    }
    return result;
}

template <>
std::vector<int> generate(size_t bytes, size_t /*min_size*/, size_t /*max_size*/) {
    assert(bytes % sizeof(int) == 0);
    std::vector<int> result;

    for (size_t current = 0; current < bytes; current += sizeof(int)) {
        result.emplace_back(42);
    }
    return result;
}
