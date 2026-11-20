#include "shared.hpp"
#include "automata.hpp"
#include "env.hpp"
#include "gpu/middle_table.hpp"
#include "pattern.hpp"
#include <gtest/gtest.h>

namespace test {
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

// Same as pfacMatch, but on the flattened table instead of the State graph.
bool pfacTableMatch(const automata::parsing::AutomatonTable &table, const std::span<const uint8_t> &compressed, gpu::EscapeCheck check) {
    for (size_t start = 0; start < compressed.size(); ++start) {
        if (gpu::pfacWalk(table, compressed.data(), compressed.size(), start, check)) {
            return true;
        }
    }
    return false;
}

// Builds both automata for `pattern` and asserts PFAC agrees with the sequential walk on
// every string in the test file.
void comparePfacPattern(const std::basic_string<uint8_t> &pattern) {
    StringPattern sp{pattern};
    std::array<automata::State, 8> refScratch{};
    std::array<automata::State, 8> pfacScratch{};
    automata::MultipleStartsFiniteAutomaton refAut = sp.createMiddleAutomaton(encoder, precomputedEnds, &errorState, refScratch, false);
    automata::MultipleStartsFiniteAutomaton pfacAut = sp.createMiddleAutomaton(encoder, precomputedEnds, &errorState, pfacScratch, true);
    automata::parsing::AutomatonTable pfacTable = gpu::buildMiddleTable(pfacAut, endStates);
    // an unset level (UINT64_MAX) would overflow pos + level and disable the bound
    for (size_t s = 0; s < pfacTable.error; ++s) {
        ASSERT_NE(pfacTable.levels[s], UINT64_MAX) << fmt::format("pattern {}: state {} has no level", byteSpanToString(pattern), s);
    }

    for (size_t idx = 0; idx < out->getNumElements(); ++idx) {
        std::span<const uint8_t> compressed = out->get(idx);
        bool expected = sequentialMatch(refAut, compressed);
        bool got = pfacMatch(pfacAut, compressed);
        ASSERT_EQ(expected, got) << fmt::format("pattern {} disagrees on string {} (encoded as {}) at index {}: sequential={}, pfac={}",
                                                byteSpanToString(pattern), byteSpanToString(in->get(idx)), byteSpanToByteList(compressed), idx, expected, got);
        for (gpu::EscapeCheck check: {gpu::EscapeCheck::OnAccept, gpu::EscapeCheck::First}) {
            bool gotTable = pfacTableMatch(pfacTable, compressed, check);
            ASSERT_EQ(expected, gotTable) << fmt::format(
                "pattern {} disagrees on string {} (encoded as {}) at index {}: sequential={}, pfac table={} (escape check {})", byteSpanToString(pattern),
                byteSpanToString(in->get(idx)), byteSpanToByteList(compressed), idx, expected, gotTable,
                check == gpu::EscapeCheck::First ? "first" : "on accept");
        }
    }
}

TEST(PfacMiddlePatternMatchTest, TestSymbolsAsPatterns) {
    for (size_t i = 0; i < encoder.nSymbols(); ++i) {
        const libfsst::Symbol &symbol = encoder.symbols()[i];
        if (symbol.val.num == 0) {
            continue;
        }
        std::basic_string<uint8_t> pattern(symbol.val.str, symbol.val.str + symbol.length());
        comparePfacPattern(pattern);
    }
}

// Fuzzing. Safe to reseed or raise num_trials freely
TEST(PfacMiddlePatternMatchTest, TestFuzzing) {
    RNG rng{420};
    size_t num_trials = 500;
    for (size_t t = 0; t < num_trials; ++t) {
        std::vector<uint8_t> patternVector = rng.generate_pattern(1, 6, chars);
        comparePfacPattern(std::basic_string<uint8_t>(patternVector.data(), patternVector.size()));
    }
}
} // namespace test
