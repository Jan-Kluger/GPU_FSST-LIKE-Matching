#include "shared.hpp"
#include "automata.hpp"
#include "common.hpp"
#include "env.hpp"
#include "pattern.hpp"
#include <gtest/gtest.h>

namespace test {
bool dpacMatch(const automata::MultipleStartsFiniteAutomaton &automaton, const std::span<const uint8_t> &compressed, size_t chunkSize, size_t overlap) {
    for (size_t chunkStart = 0; chunkStart < compressed.size(); chunkStart += chunkSize) {
        size_t end = std::min(chunkStart + chunkSize + overlap, compressed.size());
        size_t pos = chunkStart;
        if (pos > 0 && compressed[pos - 1] == 255) {
            ++pos;
        }
        automata::State *state = automaton.starts[0];
        while (pos < end && state != automaton.errorState && !automata::isEndState(state, endStates)) {
            state = state->transition(compressed[pos++]);
        }
        if (automata::isEndState(state, endStates)) {
            return true;
        }
    }
    return false;
}

constexpr std::array<size_t, 8> dpacChunkSizes{1, 2, 3, 4, 7, 16, 64, 1 << 20};

void compareDpacPattern(const std::basic_string<uint8_t> &pattern) {
    StringPattern sp{pattern};
    std::array<automata::State, 8> refScratch{};
    std::array<automata::State, 8> pfacScratch{};
    automata::MultipleStartsFiniteAutomaton refAut = sp.createMiddleAutomaton(encoder, precomputedEnds, &errorState, refScratch, false);
    automata::MultipleStartsFiniteAutomaton pfacAut = sp.createMiddleAutomaton(encoder, precomputedEnds, &errorState, pfacScratch, true);
    // a match starting on a chunk's last byte reads at most longest - 1 further bytes
    uint64_t longest = automata::support::longestMatchLength(pfacAut.starts[0], &errorState, endStates);
    ASSERT_GE(longest, pfacAut.starts[0]->level) << fmt::format("pattern {}: longest match shorter than shortest", byteSpanToString(pattern));
    size_t overlap = longest - 1;

    for (size_t idx = 0; idx < out->getNumElements(); ++idx) {
        std::span<const uint8_t> compressed = out->get(idx);
        bool expected = sequentialMatch(refAut, compressed);
        for (size_t chunkSize: dpacChunkSizes) {
            bool got = dpacMatch(refAut, compressed, chunkSize, overlap);
            ASSERT_EQ(expected, got) << fmt::format(
                "pattern {} disagrees on string {} (encoded as {}) at index {}: sequential={}, dpac={} (chunk size {}, overlap {})", byteSpanToString(pattern),
                byteSpanToString(in->get(idx)), byteSpanToByteList(compressed), idx, expected, got, chunkSize, overlap);
        }
    }
}

TEST(DpacMiddlePatternMatchTest, TestSymbolsAsPatterns) {
    for (size_t i = 0; i < encoder.nSymbols(); ++i) {
        const libfsst::Symbol &symbol = encoder.symbols()[i];
        if (symbol.val.num == 0) {
            continue;
        }
        std::basic_string<uint8_t> pattern(symbol.val.str, symbol.val.str + symbol.length());
        compareDpacPattern(pattern);
    }
}

TEST(DpacMiddlePatternMatchTest, TestFuzzing) {
    RNG rng{420};
    size_t num_trials = 500;
    for (size_t t = 0; t < num_trials; ++t) {
        std::vector<uint8_t> patternVector = rng.generate_pattern(1, 6, chars);
        compareDpacPattern(std::basic_string<uint8_t>(patternVector.data(), patternVector.size()));
    }
}
} // namespace test
