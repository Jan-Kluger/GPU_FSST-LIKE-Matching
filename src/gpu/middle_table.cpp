#include "gpu/middle_table.hpp"
#include <cassert>
#include <queue>
#include <unordered_map>

automata::parsing::AutomatonTable gpu::buildMiddleTable(const automata::MultipleStartsFiniteAutomaton &automaton, std::vector<automata::State> &endStates) {
    assert(endStates.size() == 9); // AutomatonTable hardcodes 9 accepts + 1 error
    automata::State *errorState = automaton.errorState;

    // number every state reachable from S0, in BFS order; S0 is 0
    std::unordered_map<automata::State *, size_t> indexMap{};
    std::queue<automata::State *> queue{};
    size_t numStates = 0;
    indexMap[automaton.starts[0]] = numStates++;
    queue.push(automaton.starts[0]);
    while (!queue.empty()) {
        automata::State *state = queue.front();
        queue.pop();
        for (size_t c = 0; c < 256; ++c) {
            automata::State *next = state->transition(static_cast<uint8_t>(c));
            if (next == errorState || automata::isEndState(next, endStates) || indexMap.contains(next)) {
                continue;
            }
            indexMap[next] = numStates++;
            queue.push(next);
        }
    }

    // error and accepts last, accepts in endStates order so that state - firstAccept == end index
    size_t error = numStates;
    indexMap[errorState] = numStates++;
    size_t firstAccept = numStates;
    for (automata::State &end: endStates) {
        indexMap[&end] = numStates++;
    }
    return {indexMap, false, error, firstAccept, {}, 0, 1};
}

namespace {
// copied from AutomatonTable::parse (like_pattern_automaton.hpp), where these are private / local
constexpr size_t kTransitionArraySize = 32;

bool getBit(const uint8_t *bitmap, uint8_t c) { return (bitmap[c / 8] >> (c % 8)) & 1u; }

template <typename T, bool EscapeFirst> bool pfacWalkImpl(const automata::parsing::AutomatonTable &t, const uint8_t *str, size_t len, size_t start) {
    // with N == 1 the layout is [bitmap 0][row 0][row 1][row 2]..., so every row sits at s * rowSize + 32
    auto row = [&](size_t s) { return reinterpret_cast<const T *>(t.table + (s << (8 + t.shift)) + kTransitionArraySize); };

    if constexpr (EscapeFirst) {
        if (start > 0 && str[start - 1] == FSST_ESC) {
            return false; // start is an escaped literal, not a symbol code
        }
    }
    if (!getBit(t.table, str[start])) {
        return false; // S0 has no edge on this byte, thread dies immediately
    }
    T state = 0;
    size_t pos = start;
    while (state < t.error && pos + t.levels[state] <= len) {
        state = row(state)[str[pos++]];
    }
    if (state < t.firstAccept) {
        return false;
    }
    if constexpr (EscapeFirst) {
        return true; // already checked before walking
    } else {
        return start == 0 || str[start - 1] != FSST_ESC; // escape check, only on accept
    }
}

template <bool EscapeFirst> bool pfacWalkDispatch(const automata::parsing::AutomatonTable &table, const uint8_t *str, size_t len, size_t start) {
    switch (table.shift) {
    case 0:
        return pfacWalkImpl<uint8_t, EscapeFirst>(table, str, len, start);
    case 1:
        return pfacWalkImpl<uint16_t, EscapeFirst>(table, str, len, start);
    case 2:
        return pfacWalkImpl<uint32_t, EscapeFirst>(table, str, len, start);
    default:
        return pfacWalkImpl<uint64_t, EscapeFirst>(table, str, len, start);
    }
}
} // namespace

bool gpu::pfacWalk(const automata::parsing::AutomatonTable &table, const uint8_t *str, size_t len, size_t start, EscapeCheck check) {
    if (check == EscapeCheck::First) {
        return pfacWalkDispatch<true>(table, str, len, start);
    }
    return pfacWalkDispatch<false>(table, str, len, start);
}
