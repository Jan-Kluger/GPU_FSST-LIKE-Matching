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

#ifndef PATTERN_HPP
#define PATTERN_HPP
#include <span>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "encoder.hpp"
#include "percentage.hpp"

struct Pattern {
    virtual automata::SingleStartFiniteAutomaton createStartAutomaton(const Encoder& encoder, const std::vector<automata::State*>& precomputedEnds, automata::State* errorState) = 0;
    virtual automata::MultipleStartsFiniteAutomaton createMiddleAutomaton(const Encoder& encoder, const std::vector<automata::State*>& precomputedEnds, automata::State* errorState, std::array<automata::State, 8>& startStates, bool failureless = true) = 0;
    virtual automata::SingleStartFiniteAutomaton createEndAutomaton(const Encoder& encoder, const std::vector<automata::State*>& precomputedEnds, automata::State* errorState) = 0;
    virtual ~Pattern() = default;

    static std::vector<std::unique_ptr<Pattern>> splitIntoSubpatterns(const std::span<const uint8_t>& pattern);
    static std::unique_ptr<Pattern> createPattern(const std::span<const uint8_t>& subpattern);
};

struct StringPattern: public Pattern {
private:

public:
    std::basic_string<uint8_t> pattern;
    explicit StringPattern(const std::basic_string<uint8_t>& pattern);
    automata::SingleStartFiniteAutomaton createStartAutomaton(const Encoder& encoder, const std::vector<automata::State*>& precomputedEnds, automata::State* errorState) override;
    automata::MultipleStartsFiniteAutomaton createMiddleAutomaton(const Encoder& encoder, const std::vector<automata::State*>& precomputedEnds, automata::State* errorState, std::array<automata::State, 8>& startStates, bool failureless = true) override;
    automata::SingleStartFiniteAutomaton createEndAutomaton(const Encoder& encoder, const std::vector<automata::State*>& precomputedEnds, automata::State* errorState) override;
    ~StringPattern() override = default;
};

struct UnderscorePattern: public Pattern {
public:
    std::vector<std::basic_string<uint8_t>> subpatterns;
    std::vector<uint8_t> numUnderscores;
    UnderscorePattern(const std::vector<std::basic_string<uint8_t>>& subpatterns, const std::vector<uint8_t>& numUnderscores);
    automata::SingleStartFiniteAutomaton createStartAutomaton(const Encoder& encoder, const std::vector<automata::State*>& precomputedEnds, automata::State* errorState) override;
    automata::MultipleStartsFiniteAutomaton createMiddleAutomaton(const Encoder& encoder, const std::vector<automata::State*>& precomputedEnds, automata::State* errorState, std::array<automata::State, 8>& startStates, bool failureless = true) override;
    automata::SingleStartFiniteAutomaton createEndAutomaton(const Encoder& encoder, const std::vector<automata::State*>& precomputedEnds, automata::State* errorState) override;
};

#endif //PATTERN_HPP
