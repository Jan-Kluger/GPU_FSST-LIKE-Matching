#include "shared.hpp"
#include "automata.hpp"
#include "env.hpp"
#include "pattern.hpp"
#include <gtest/gtest.h>

namespace test {
// taken from test/MiddlePatternMatchingTest, basic sequential traversal of automoton to see if we accept.
bool sequentialMatch(const automata::MultipleStartsFiniteAutomaton &automaton, const std::span<const uint8_t> &compressed) {
    size_t pos = 0;
    automata::State *state = automaton.starts[0];
    while (pos < compressed.size() && state != automaton.errorState && !automata::isEndState(state, endStates)) {
        state = state->transition(compressed[pos++]);
    }
    return automata::isEndState(state, endStates);
}

// PFAC: one independent walk per byte position. This is the algo 1 discussed, just not on gpu
// // (failureless = true), where a mismatch lands in the error state
bool pfacMatch(const automata::MultipleStartsFiniteAutomaton &automaton, const std::span<const uint8_t> &compressed) {
    for (size_t start = 0; start < compressed.size(); ++start) {
        automata::State *state = automaton.starts[0];
        for (size_t pos = start; pos < compressed.size(); ++pos) {
            state = state->transition(compressed[pos]);
            if (state == automaton.errorState) {
                break;
            }
            if (automata::isEndState(state, endStates)) {
                // Escape check, this is the on terminate chceck we discussed in meeting.
                // TODO: other termination algorithms discussed in meeting (always check pre byte, and some third one)
                if (start == 0 || compressed[start - 1] != 255) {
                    return true;
                }
                break;
            }
        }
    }
    return false;
}

// Builds both automata for `pattern` and asserts PFAC agrees with the sequential walk on
// every string in the test file.
void comparePattern(const std::basic_string<uint8_t> &pattern) {
    StringPattern sp{pattern};
    std::array<automata::State, 8> refScratch{};
    std::array<automata::State, 8> pfacScratch{};
    automata::MultipleStartsFiniteAutomaton refAut = sp.createMiddleAutomaton(encoder, precomputedEnds, &errorState, refScratch, false);
    automata::MultipleStartsFiniteAutomaton pfacAut = sp.createMiddleAutomaton(encoder, precomputedEnds, &errorState, pfacScratch, true);

    for (size_t idx = 0; idx < out->getNumElements(); ++idx) {
        std::span<const uint8_t> compressed = out->get(idx);
        bool expected = sequentialMatch(refAut, compressed);
        bool got = pfacMatch(pfacAut, compressed);
        ASSERT_EQ(expected, got) << fmt::format("pattern {} disagrees on string {} (encoded as {}) at index {}: sequential={}, pfac={}",
                                                byteSpanToString(pattern), byteSpanToString(in->get(idx)), byteSpanToByteList(compressed), idx, expected, got);
    }
}

TEST(PfacMiddlePatternMatchTest, TestSymbolsAsPatterns) {
    for (size_t i = 0; i < encoder.nSymbols(); ++i) {
        const libfsst::Symbol &symbol = encoder.symbols()[i];
        if (symbol.val.num == 0) {
            continue;
        }
        std::basic_string<uint8_t> pattern(symbol.val.str, symbol.val.str + symbol.length());
        comparePattern(pattern);
    }
}

// Fuzzing. Safe to reseed or raise num_trials freely
TEST(PfacMiddlePatternMatchTest, TestFuzzing) {
    RNG rng{420};
    size_t num_trials = 500;
    for (size_t t = 0; t < num_trials; ++t) {
        std::vector<uint8_t> patternVector = rng.generate_pattern(1, 6, chars);
        comparePattern(std::basic_string<uint8_t>(patternVector.data(), patternVector.size()));
    }
}
} // namespace test
