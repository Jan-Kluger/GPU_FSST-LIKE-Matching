//
// Created by popca on 19-May-25.
//

#ifndef FSST_SHARED_HPP
#define FSST_SHARED_HPP

#include <random>
#include <span>

#include <fmt/format.h>
#include "automata.hpp"

struct MatchIndex {
    MatchIndex(uint8_t symbolIndex, int64_t strIndex);

    uint8_t symbolIndex;
    int64_t strIndex;
};

int64_t getTimeNow();

struct RNG {
private:
    std::mt19937 engine;

public:
    RNG(int seed): engine(seed) {}
    RNG(): engine(0) {}

    template <typename I> I generate_uniform_int(I start, I end) {
        std::uniform_int_distribution<I> dist(start, end);
        return dist(engine);
    }

    std::vector<uint8_t> generate_pattern(size_t min_len, size_t max_len, const std::vector<uint8_t> &chars) {
        size_t len = generate_uniform_int(min_len, max_len);
        std::vector<uint8_t> s(len, 0);
        for (size_t idx = 0; idx < len; ++idx) {
            size_t char_idx = generate_uniform_int((size_t)0, chars.size() - 1);
            s[idx] = chars[char_idx];
        }
        return s;
    }
};

std::string byteSpanToByteList(const std::span<const uint8_t> &span);
std::string byteSpanToString(const std::span<const uint8_t> &span);

void readExpectedResults(int fd, std::vector<uint16_t> &matchIndexes, std::vector<uint8_t> &symIndexes, std::vector<uint8_t> &strIndexes);

bool sequentialMatch(const automata::MultipleStartsFiniteAutomaton &automaton, const std::span<const uint8_t> &compressed);
std::span<const uint8_t> stringToByteSpan(const std::string &s);
std::span<const uint8_t> stringViewToByteSpan(const std::string_view &s);
std::basic_string<uint8_t> stringToBasicString(const std::string &s);
std::basic_string<uint8_t> stringViewToBasicString(const std::string_view &s);

#endif // FSST_SHARED_HPP
