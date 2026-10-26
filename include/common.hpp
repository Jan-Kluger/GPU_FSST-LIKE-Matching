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

#ifndef FSST_LIKE_MATCHING_SHARED_HPP
#define FSST_LIKE_MATCHING_SHARED_HPP
#include <array>
#include "automata.hpp"

namespace automata::support {
struct TemporaryStarts {
private:
    uint8_t idx;
    std::array<std::array<State, 8>, 2> states{};

public:
    explicit TemporaryStarts(State *defaultTransition);
    TemporaryStarts();
    std::array<State, 8> &get();
};

void eraseUnusedStarts(std::vector<State *> &starts);
void precomputeStartPositions(std::vector<State *> &starts);
void deepCopyAutomaton(State *destination, const State *source, const std::unordered_set<const State *> &currentStates,
                       const std::function<State *()> &createState);
void reverseBreadthFirstSearch(const std::vector<State *> &startStates, std::deque<State> &states, const std::unordered_set<const State *> &currentStates);
uint64_t longestMatchLength(const State *start, const State *errorState, const std::vector<State> &endStates);
const State *transitionTrailingState(const State *source, uint8_t symbol, std::unordered_map<const State *, const State *> &trailingMappings,
                                     const State *startState);
void copyFallbackTransitions(std::unordered_map<const State *, const State *> &trailingMappings, std::deque<State *> &topoSorted);
void createSinkState(State *startState, const std::function<State *()> &createState);

struct QueueObject {
    State *destination;
    const State *source;

    std::tuple<State *, const State *> unpack();

    bool operator==(const QueueObject &other) const;

    struct Hash {
        size_t operator()(const QueueObject &obj) const noexcept;
    };
};
void shallowCopyAutomaton(State *destination, const State *source, const std::unordered_set<const State *> &currentStates);
} // namespace automata::support
#endif // FSST_LIKE_MATCHING_SHARED_HPP
