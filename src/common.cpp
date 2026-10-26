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

#include "common.hpp"
#include <optional>
#include <ranges>
#include <stdexcept>
automata::support::TemporaryStarts::TemporaryStarts(State *defaultTransition): idx(0) {
    for (std::array<State, 8> &line: states) {
        for (State &state: line) {
            state.defaultTransition = defaultTransition;
        }
    }
}

automata::support::TemporaryStarts::TemporaryStarts(): idx(0), states() {}

std::array<automata::State, 8> &automata::support::TemporaryStarts::get() {
    std::array<State, 8> &ret = states[idx];
    idx = 1 - idx;
    for (uint8_t i = 0; i < 8; ++i) {
        ret[i].transitions.clear();
        ret[i].level = UINT64_MAX;
    }
    return ret;
}

void automata::support::eraseUnusedStarts(std::vector<State *> &starts) {
    for (uint8_t idx = 1; idx < 9; ++idx) {
        if (starts[idx]->transitions.empty()) {
            starts[idx] = nullptr;
        }
    }
}

void automata::support::precomputeStartPositions(std::vector<State *> &starts) {
    State *current = starts[0];
    for (int8_t i = 8; i >= 0; --i) {
        if (starts[i] != nullptr) {
            current = starts[i];
        } else {
            starts[i] = current;
        }
    }
}

void automata::support::deepCopyAutomaton(State *destination, const State *source, const std::unordered_set<const State *> &currentStates,
                                          const std::function<State *()> &createState) {
    std::queue<std::pair<State *, const State *>> queue{};
    queue.emplace(destination, source);
    std::unordered_map<const State *, State *> mappings{};
    mappings[source] = destination;
    while (!queue.empty()) {
        destination = queue.front().first;
        source = queue.front().second;
        queue.pop();
        for (const auto &[symbol, next]: source->transitions) {
            if (!currentStates.contains(next)) {
                destination->addTransition(symbol, next);
            } else {
                if (!mappings.contains(next)) {
                    mappings[next] = createState();
                    queue.emplace(mappings[next], next);
                }
                destination->addTransition(symbol, mappings[next]);
            }
        }
    }
}

void automata::support::reverseBreadthFirstSearch(const std::vector<State *> &startStates, std::deque<State> &states,
                                                  const std::unordered_set<const State *> &currentStates) {
    std::unordered_map<State *, std::unordered_set<State *>> parents{};
    std::unordered_set<State *> otherStates{};
    auto updateParents = [&parents, &otherStates, &currentStates](State *current) {
        // sink state; look into default
        if (current->transitions.empty()) {
            parents[current->defaultTransition].insert(current);
        } else {
            for (auto &[symbol, next]: current->transitions) {
                if (!currentStates.contains(next)) {
                    otherStates.insert(next);
                }
                parents[next].insert(current);
            }
        }
    };

    for (State *state: startStates) {
        if (state != nullptr && !currentStates.contains(state)) {
            updateParents(state);
        }
    }

    for (State &state: states) {
        updateParents(&state);
    }

    std::unordered_set<State *> visited{};
    using PQElement = std::pair<uint64_t, State *>;
    std::priority_queue<PQElement, std::vector<PQElement>, std::greater<>> pq{};
    auto enqueue = [&visited, &pq](uint64_t level, State *current) {
        if (visited.contains(current)) {
            return;
        }
        pq.emplace(level, current);
    };

    for (State *end: otherStates) {
        for (State *parent: parents[end]) {
            enqueue(end->level + 1, parent);
        }
    }

    while (!pq.empty()) {
        auto [level, current] = pq.top();
        pq.pop();
        if (visited.contains(current)) {
            continue;
        }
        current->level = level;
        visited.insert(current);
        for (State *parent: parents[current]) {
            enqueue(level + 1, parent);
        }
    }
}

uint64_t automata::support::longestMatchLength(const State *start, const State *errorState, const std::vector<State> &endStates) {
    // nullopt marks a dead end, i.e. a state from which no end state is reachable
    std::unordered_map<const State *, std::optional<uint64_t>> memo{};
    std::unordered_set<const State *> onStack{};
    std::function<std::optional<uint64_t>(const State *)> visit = [&](const State *state) -> std::optional<uint64_t> {
        if (isEndState(state, endStates)) {
            return 0;
        }
        if (state == errorState) {
            return std::nullopt;
        }
        if (auto it = memo.find(state); it != memo.end()) {
            return it->second;
        }
        if (!onStack.insert(state).second) {
            throw std::logic_error("longestMatchLength: automaton contains a cycle, pass a failureless automaton");
        }
        std::optional<uint64_t> longest{};
        auto relax = [&](const State *next) {
            if (std::optional<uint64_t> length = visit(next)) {
                longest = std::max(longest.value_or(0), *length + 1);
            }
        };
        for (const auto &[symbol, next]: state->transitions) {
            relax(next);
        }
        relax(state->defaultTransition);
        onStack.erase(state);
        memo[state] = longest;
        return longest;
    };

    std::optional<uint64_t> longest = visit(start);
    if (!longest.has_value()) {
        throw std::logic_error("longestMatchLength: no end state reachable from start");
    }
    return *longest;
}

const automata::State *automata::support::transitionTrailingState(const State *source, uint8_t symbol,
                                                                  std::unordered_map<const State *, const State *> &trailingMappings, const State *startState) {
    if (source == nullptr) {
        return symbol == FSST_ESC ? nullptr : startState;
    }
    if (source->canTransition(symbol)) {
        return source->transition(symbol);
    }
    const State *trailing = trailingMappings[source];
    while (trailing != nullptr) {
        if (trailing->canTransition(symbol)) {
            return trailing->transition(symbol);
        }
        trailing = trailingMappings[trailing];
    }
    return source->defaultTransition;
}

void automata::support::copyFallbackTransitions(std::unordered_map<const State *, const State *> &trailingMappings, std::deque<State *> &topoSorted) {
    for (State *state: topoSorted | std::views::reverse) {
        const State *trailing = trailingMappings.find(state)->second;
        while (trailing != nullptr) {
            state->copyTransitions(trailing);
            trailing = trailingMappings[trailing];
        }
    }
}

void automata::support::createSinkState(State *startState, const std::function<State *()> &createState) {
    if (!startState->canTransition(FSST_ESC)) {
        State *escapeTransition = createState();
        startState->addTransition(FSST_ESC, escapeTransition);
    }
}

std::tuple<automata::State *, const automata::State *> automata::support::QueueObject::unpack() { return {destination, source}; }

bool automata::support::QueueObject::operator==(const QueueObject &other) const { return destination == other.destination && source == other.source; }

size_t automata::support::QueueObject::Hash::operator()(const QueueObject &obj) const noexcept {
    size_t h1 = std::hash<State *>()(obj.destination);
    std::size_t h2 = std::hash<const State *>()(obj.source);

    std::size_t seed = h1;
    seed ^= h2 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
}

void automata::support::shallowCopyAutomaton(State *destination, const State *source, const std::unordered_set<const State *> &currentStates) {
    State *nextTreeNode = nullptr, *nextSymbolNode = nullptr;

    std::queue<QueueObject> queue{};
    std::unordered_set<QueueObject, QueueObject::Hash> visited{};
    queue.emplace(destination, source);
    visited.emplace(destination, source);

    std::vector<std::tuple<State *, uint8_t, State *>> addedTransitions{};
    while (!queue.empty()) {
        auto [destination, source] = queue.front().unpack();
        queue.pop();
        if (!currentStates.contains(destination)) {
            continue;
        }

        for (const auto &[symbol, next]: source->transitions) {
            nextSymbolNode = next;
            if (destination->canTransition(symbol)) {
                nextTreeNode = destination->transition(symbol);

                QueueObject nextObj{nextTreeNode, nextSymbolNode};
                if (!visited.contains(nextObj)) {
                    visited.insert(nextObj);
                    queue.push(nextObj);
                }
            } else {
                addedTransitions.emplace_back(destination, symbol, nextSymbolNode);
            }
        }
    }

    for (auto &[sourceState, symbol, destinationState]: addedTransitions) {
        sourceState->addTransition(symbol, destinationState);
    }
}
