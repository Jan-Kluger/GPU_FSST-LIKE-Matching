//
// Created by popca on 19-May-25.
//
#include "shared.hpp"
#include "env.hpp"
#include <unistd.h>
#include <chrono>

std::string byteSpanToByteList(const std::span<const uint8_t> &bytes) {
    std::string res = "{";
    for (size_t idx = 0; idx < bytes.size(); ++idx) {
        res = fmt::format("{}{}", res, (int)bytes[idx]);
        char nextChar = (idx == bytes.size() - 1) ? '}' : ',';
        res.push_back(nextChar);
    }
    return res;
}

std::string byteSpanToString(const std::span<const uint8_t> &span) { return std::string(reinterpret_cast<const char *>(span.data()), span.size()); }

int64_t getTimeNow() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(high_resolution_clock::now().time_since_epoch()).count();
}

MatchIndex::MatchIndex(uint8_t symbolIndex, int64_t strIndex): symbolIndex(symbolIndex), strIndex(strIndex) {}

void readExpectedResults(int fd, std::vector<uint16_t> &matchIndexes, std::vector<uint8_t> &symIndexes, std::vector<uint8_t> &strIndexes) {
    size_t numMatches;
    read(fd, &numMatches, sizeof(numMatches));
    matchIndexes.resize(numMatches);
    symIndexes.resize(numMatches);
    strIndexes.resize(numMatches);

    read(fd, matchIndexes.data(), numMatches * sizeof(uint16_t));
    read(fd, symIndexes.data(), numMatches * sizeof(uint8_t));
    read(fd, strIndexes.data(), numMatches * sizeof(uint8_t));
}

std::span<const uint8_t> stringToByteSpan(const std::string &s) { return std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(s.data()), s.size()); }

std::span<const uint8_t> stringViewToByteSpan(const std::string_view &s) {
    return std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(s.data()), s.size());
}

std::basic_string<uint8_t> stringToBasicString(const std::string &s) {
    return std::basic_string<uint8_t>(reinterpret_cast<const uint8_t *>(s.data()), s.size());
}

std::basic_string<uint8_t> stringViewToBasicString(const std::string_view &s) {
    return std::basic_string<uint8_t>(reinterpret_cast<const uint8_t *>(s.data()), s.size());
}

bool sequentialMatch(const automata::MultipleStartsFiniteAutomaton &automaton, const std::span<const uint8_t> &compressed) {
    size_t pos = 0;
    automata::State *state = automaton.starts[0];
    while (pos < compressed.size() && state != automaton.errorState && !automata::isEndState(state, endStates)) {
        state = state->transition(compressed[pos++]);
    }
    return automata::isEndState(state, endStates);
}
