// this software is distributed under the MIT License (http://www.opensource.org/licenses/MIT):
//
// Copyright (c) 2026 Calin Pop George
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pattern.hpp"
#include "common.hpp"
std::vector<std::unique_ptr<Pattern>> Pattern::splitIntoSubpatterns(const std::span<const uint8_t> &pattern) {
    assert(!pattern.empty());
    std::vector<std::unique_ptr<Pattern>> subpatterns{};
    int64_t startIdx = 0;
    if (pattern[0] == '%') {
        subpatterns.emplace_back();
    } else {
        while (startIdx < pattern.size() && pattern[startIdx] != '%') {
            startIdx += pattern[startIdx] == '\\';
            ++startIdx;
        }

        while (startIdx < pattern.size() && (pattern[startIdx] == '%' || pattern[startIdx] == '_')) {
            ++startIdx;
        }
        std::unique_ptr<Pattern> startPattern = Pattern::createPattern(std::span<const uint8_t>(pattern.data(), startIdx));
        subpatterns.push_back(std::move(startPattern));
    }

    int64_t endIdx = pattern.size() - 1;
    std::unique_ptr<Pattern> endPattern{};

    if (pattern[pattern.size() - 1] != '%' || (pattern.size() > 1 && pattern[pattern.size() - 2] == '\\')) {
        while (endIdx >= 0 && (pattern[endIdx] != '%' || (endIdx > 0 && pattern[endIdx - 1] == '\\'))) {
            --endIdx;
        }
        ++endIdx;
        while (endIdx < pattern.size() && pattern[endIdx] == '_') {
            ++endIdx;
        }
        endPattern = Pattern::createPattern(std::span<const uint8_t>(pattern.data() + endIdx, pattern.size() - endIdx));
    } else {
        ++endIdx;
    }

    int64_t idx = startIdx;
    uint8_t stage = 0;
    while (idx < endIdx) {
        switch (stage) {
            case 0:
                if (pattern[idx] != '%' && pattern[idx] != '_') {
                    stage = 1;
                }
                break;
            case 1:
                if (pattern[idx] == '%') {
                    stage = 2;
                }
                break;
            case 2:
                if (pattern[idx] != '%' && pattern[idx] != '_') {
                    auto currentPattern = Pattern::createPattern(std::span<const uint8_t>(pattern.data() + startIdx, idx - startIdx));
                    if (currentPattern) {
                        subpatterns.push_back(std::move(currentPattern));
                    }
                    stage = 1;
                    startIdx = idx;
                }
                break;
            default:
                throw std::runtime_error("Unexpected stage in Pattern::splitIntoSubpatterns");
        }
        idx += pattern[idx] == '\\';
        ++idx;
    }

    if (startIdx < endIdx) {
        auto currentPattern = Pattern::createPattern(std::span<const uint8_t>(pattern.data() + startIdx, endIdx - startIdx));
        if (currentPattern) {
            subpatterns.push_back(std::move(currentPattern));
        }
    }

    subpatterns.push_back(std::move(endPattern));
    return subpatterns;
}

std::unique_ptr<Pattern> Pattern::createPattern(const std::span<const uint8_t> &pattern) {
    if (pattern.empty()) {
        return std::unique_ptr<Pattern>();
    }
    int64_t startIdx = 0;
    uint8_t startUnderscores = 0;
    while (startIdx < pattern.size() && (pattern[startIdx] == '_' || pattern[startIdx] == '%')) {
        startUnderscores += pattern[startIdx] == '_';
        ++startIdx;
    }

    std::vector<std::basic_string<uint8_t>> subpatterns{};
    std::vector<uint8_t> numUnderscores;
    numUnderscores.push_back(startUnderscores);
    if (startIdx == pattern.size()) {
        return startUnderscores == 0 ? std::unique_ptr<Pattern>() : std::make_unique<UnderscorePattern>(subpatterns, numUnderscores);
    }

    int64_t endIdx = pattern.size() - 1;
    uint8_t endUnderscores = 0;
    while (endIdx >= 0 && (pattern[endIdx] == '_' || pattern[endIdx] == '%') && pattern[endIdx - 1] != '\\') {
        endUnderscores += pattern[endIdx] == '_';
        --endIdx;
    }

    std::basic_string<uint8_t> current{};
    uint8_t currentUnderscores = 0;
    int64_t idx = startIdx;
    while (idx <= endIdx) {
        switch (pattern[idx]) {
            case '_':
                if (!current.empty()) {
                    subpatterns.push_back(current);
                    current.clear();
                }
                ++currentUnderscores;
                break;
            case '\\':
                ++idx;
            default:
                if (currentUnderscores != 0) {
                    numUnderscores.push_back(currentUnderscores);
                    currentUnderscores = 0;
                }
                current.push_back(pattern[idx]);
                break;
        }
        ++idx;
    }
    subpatterns.push_back(current);
    numUnderscores.push_back(endUnderscores);
    if (numUnderscores.size() == 2 && startUnderscores + endUnderscores == 0) {
        return std::make_unique<StringPattern>(current);
    }
    return std::make_unique<UnderscorePattern>(subpatterns, numUnderscores);
}

StringPattern::StringPattern(const std::basic_string<uint8_t> &pattern): pattern(pattern) {}

automata::SingleStartFiniteAutomaton StringPattern::createStartAutomaton(const Encoder& encoder, const std::vector<automata::State*>& precomputedEnds, automata::State* errorState) {
    automata::SingleStartFiniteAutomaton automaton{errorState, automata::SingleStartFiniteAutomaton::Direction::FORWARD};
    auto createStateFn = [&]() { return automaton.createState(); };
    automata::percentage::constructPrefixAutomaton(pattern, 0, encoder, precomputedEnds, createStateFn, automaton.startState.get());
    automaton.findDeterministicPath();
    return std::move(automaton);
}

automata::MultipleStartsFiniteAutomaton StringPattern::createMiddleAutomaton(const Encoder& encoder, const std::vector<automata::State*>& precomputedEnds, automata::State* errorState, std::array<automata::State, 8>& startStates, bool failureless) {
    automata::MultipleStartsFiniteAutomaton automaton{errorState};
    automata::State* defaultTransition = automaton.createState();
    automaton.starts[0] = defaultTransition;
    automaton.defaultTransition = defaultTransition;
    automaton.starts[0]->defaultTransition = defaultTransition;
    for (size_t idx = 0; idx < 8; ++idx) {
        startStates[idx].defaultTransition = defaultTransition;
        automaton.starts[idx + 1] = &startStates[idx];
    }
    std::unordered_set<const automata::State*> currentStates{};
    auto createStateFn = [&]() {
        automata::State* newState =  automaton.createState();
        currentStates.insert(newState);
        return newState;
    };
    std::vector<automata::State*> cache(pattern.size(), nullptr);
    std::array<automata::State*, 255> transitionStates{nullptr};

    uint8_t max_n = pattern.size() >= 8 ? 7 : pattern.size() - 1;
    std::vector<std::vector<uint8_t>> symbols = encoder.findAllSymbolsWithSuffix(pattern, max_n);
    for (uint8_t i = 0; i < max_n; ++i) {
        if (symbols[i].empty()) {
            continue;
        }
        automata::percentage::connectStartsToSubautomaton(
            pattern, encoder, precomputedEnds, createStateFn, errorState, automaton.starts, currentStates,
            transitionStates, cache, i, symbols[i]
        );
    }

    automata::percentage::integrateSymbolsContainingPattern(
        pattern, encoder, precomputedEnds, automaton.starts
    );
    automata::percentage::constructCachedPrefixAutomaton(
        pattern, 0, encoder, precomputedEnds, createStateFn, false, cache, errorState,
        currentStates, automaton.starts[0]
    );

    automata::percentage::splitStarts(automaton.starts, currentStates);
    automata::support::createSinkState(automaton.starts[0], createStateFn);
    automata::support::eraseUnusedStarts(automaton.starts);
    automata::percentage::linkStarts(automaton.starts);
    if (!failureless) {
        automata::percentage::constructFallbackTransitions(automaton.starts, currentStates);
    }
    automata::support::reverseBreadthFirstSearch(automaton.starts, automaton.states, currentStates);
    automata::support::precomputeStartPositions(automaton.starts);
    return std::move(automaton);
}

automata::SingleStartFiniteAutomaton StringPattern::createEndAutomaton(const Encoder& encoder, const std::vector<automata::State*>& precomputedEnds, automata::State* errorState) {
    automata::SingleStartFiniteAutomaton automaton{errorState, automata::SingleStartFiniteAutomaton::Direction::BACKWARD};
    std::vector<automata::State*> pseudoEnds = automata::percentage::initialisePseudoEnds(&automaton, precomputedEnds);
    automata::percentage::constructSuffixAutomaton(pattern, encoder, &automaton, pseudoEnds, automaton.startState.get());
    automaton.findDeterministicPath();
    return std::move(automaton);
}

UnderscorePattern::UnderscorePattern(const std::vector<std::basic_string<uint8_t>> &subpatterns, const std::vector<uint8_t> &numUnderscores): subpatterns(subpatterns), numUnderscores(numUnderscores) {}

automata::SingleStartFiniteAutomaton UnderscorePattern::createStartAutomaton(const Encoder &encoder, const std::vector<automata::State *> &precomputedEnds, automata::State *errorState) {
    throw std::runtime_error("Not implemented");
}

automata::MultipleStartsFiniteAutomaton UnderscorePattern::createMiddleAutomaton(const Encoder &encoder, const std::vector<automata::State*> &precomputedEnds, automata::State *errorState, std::array<automata::State, 8> &startStates, bool) {
    throw std::runtime_error("Not implemented");
}

automata::SingleStartFiniteAutomaton UnderscorePattern::createEndAutomaton(const Encoder &encoder, const std::vector<automata::State *> &precomputedEnds, automata::State *errorState) {
    throw std::runtime_error("Not implemented");
}