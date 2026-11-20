#ifndef FSST_LIKE_MATCHING_MIDDLE_TABLE_HPP
#define FSST_LIKE_MATCHING_MIDDLE_TABLE_HPP

#include "automata.hpp"
#include "like_pattern_automaton.hpp"

namespace gpu {
// Flattens one middle automaton (entered at starts[0])
// S0 gets index 0 and is the only state with a bitmap (N = 1) error and accepts go last.
automata::parsing::AutomatonTable buildMiddleTable(const automata::MultipleStartsFiniteAutomaton &automaton, std::vector<automata::State> &endStates);

// When a PFAC thread checks whether its start byte is the literal half of an escape pair.
enum class EscapeCheck {
    OnAccept, // only threads that reach an accept state look at the byte before
    First,    // every thread looks at the byte before, before walking
};

// One PFAC thread walks the table from byte start. True if it accepts and start is not an escaped literal.
bool pfacWalk(const automata::parsing::AutomatonTable &table, const uint8_t *str, size_t len, size_t start, EscapeCheck check = EscapeCheck::OnAccept);
} // namespace gpu

#endif // FSST_LIKE_MATCHING_MIDDLE_TABLE_HPP
