// this software is distributed under the MIT License
// (http://www.opensource.org/licenses/MIT):
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
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "codegen/cppcodegen.hpp"
#include "utils.hpp"
#include <dlfcn.h>
#include <filesystem>

void automata::codegen::cpp::CppCompiler::addIncludes() {
  ss << "#include <cstddef>\n";
  ss << "#include <cstdint>\n";
  ss << "#include <bit>\n";
  ss << "#include <string_view>\n";
  if (enableSIMD) {
    ss << "#include <nmmintrin.h>\n";
    ss << "#include <pmmintrin.h>\n";
  }
}

void automata::codegen::cpp::CppCompiler::generateTransitionArrays(
    const std::unique_ptr<parsing::LikePatternAutomaton> &automaton) {
  for (const auto &middleAutomaton : automaton->middleMatches) {
    State *start = middleAutomaton.defaultTransition;
    ss << fmt::format("constexpr bool {}[256] = ",
                      getTransitionArrayName(start));
    ss << "{";
    for (uint8_t symbol = 0; symbol < 255; ++symbol) {
      ss << (start->canTransition(symbol) ? "true" : "false");
      ss << ", ";
    }
    if (start->transition(255)->transitions.empty()) {
      ss << "false";
    } else {
      ss << "true";
    }
    ss << "};\n";

    auto parsingType = StateCodegen::doesParsingUseSIMD(start, enableSIMD);
    if (parsingType == StateCodegen::ParsingMode::SIMD_CMPESTRM) {
      ss << fmt::format("constexpr uint8_t {}[16] = ",
                        getSIMDCmpestrmSetName(start));

      std::vector<uint8_t> symbols{};
      symbols.reserve(16);
      for (const auto &[symbol, dest] : start->transitions) {
        if (symbol != 255 || !dest->transitions.empty()) {
          symbols.push_back(symbol);
        }
      }
      while (symbols.size() < 16) {
        symbols.push_back(0);
      }

      ss << "{";
      for (int i = 0; i < 16; ++i) {
        std::string next = i == 15 ? "};\n" : ", ";
        ss << fmt::format("{}{}", symbols[i], next);
      }
      ss << fmt::format("__m128i {} = _mm_loadu_si128(reinterpret_cast<const "
                        "__m128i*>({}));\n",
                        getSIMDCmpestrmVectorName(start),
                        getSIMDCmpestrmSetName(start));
    } else if (parsingType == StateCodegen::ParsingMode::SIMD_CMPEQEPI8) {
      for (const auto &[symbol, dest] : start->transitions) {
        if (symbol != 255 || !dest->transitions.empty()) {
          ss << fmt::format("constexpr uint8_t {} = {};\n",
                            getSIMDCmpeqepi8SymbolName(start, symbol), symbol);
          ss << fmt::format(
              "__m128i {} = _mm_set1_epi8(*reinterpret_cast<const "
              "char*>(&{}));\n",
              getSIMDCmpeqepi8VectorName(start, symbol),
              getSIMDCmpeqepi8SymbolName(start, symbol));
        }
      }
    }
  }
  ss << "\n";
}

void automata::codegen::cpp::CppCompiler::generatePrefixVariables(
    const std::optional<parsing::LikePatternAutomaton::AutomatonParams>
        &params) {
  const std::basic_string<uint8_t> &prefix = params->deterministicPath;
  size_t offset = 0;
  while (offset < prefix.size()) {
    if (prefix.size() - offset >= 16 && enableSIMD) {
      ss << fmt::format("constexpr uint8_t {}arr[16] = {{",
                        getPrefixName(offset));
      for (uint8_t i = 0; i < 16; ++i) {
        ss << fmt::format("{}", prefix[offset + i]);
        if (i == 15) {
          ss << "};\n";
        } else {
          ss << ", ";
        }
      }
      ss << fmt::format("__m128i {} = _mm_loadu_si128(reinterpret_cast<const "
                        "__m128i*>({}arr));\n",
                        getPrefixName(offset), getPrefixName(offset));
      offset += 16;
    } else {
      if (prefix.size() - offset >= 8) {
        ss << fmt::format("constexpr uint64_t {} = {}ull;\n",
                          getPrefixName(offset),
                          loadUnaligned<uint64_t>(prefix.data() + offset));
        offset += 8;
      } else if (prefix.size() - offset >= 4) {
        ss << fmt::format("constexpr uint32_t {} = {};\n",
                          getPrefixName(offset),
                          loadUnaligned<uint32_t>(prefix.data() + offset));
        offset += 4;
      } else if (prefix.size() - offset >= 2) {
        ss << fmt::format("constexpr uint16_t {} = {};\n",
                          getPrefixName(offset),
                          loadUnaligned<uint16_t>(prefix.data() + offset));
        offset += 2;
      } else if (prefix.size() - offset >= 1) {
        ss << fmt::format("constexpr uint8_t {} = {};\n", getPrefixName(offset),
                          prefix[offset]);
        offset += 1;
      }
    }
  }
}

void automata::codegen::cpp::CppCompiler::generateSuffixVariables(
    const std::optional<parsing::LikePatternAutomaton::AutomatonParams>
        &params) {
  const std::basic_string<uint8_t> &suffix = params->deterministicPath;
  size_t offset = suffix.size();
  while (offset > 0) {
    if (offset >= 16 && enableSIMD) {
      ss << fmt::format("constexpr uint8_t {}arr[16] = {{",
                        getSuffixName(offset));
      for (uint8_t i = 0; i < 16; ++i) {
        ss << fmt::format("{}", suffix[offset - 16 + i]);
        if (i == 15) {
          ss << "};\n";
        } else {
          ss << ", ";
        }
      }
      ss << fmt::format("__m128i {} = _mm_loadu_si128(reinterpret_cast<const "
                        "__m128i*>({}arr));\n",
                        getSuffixName(offset), getSuffixName(offset));
      offset -= 16;
    } else {
      if (offset >= 8) {
        ss << fmt::format(
            "uint64_t {} = {}ull;\n", getSuffixName(offset),
            loadUnaligned<uint64_t>(suffix.data() + offset - sizeof(uint64_t)));
        offset -= 8;
      } else if (offset >= 4) {
        ss << fmt::format(
            "uint32_t {} = {};\n", getSuffixName(offset),
            loadUnaligned<uint32_t>(suffix.data() + offset - sizeof(uint32_t)));
        offset -= 4;
      } else if (offset >= 2) {
        ss << fmt::format(
            "uint16_t {} = {};\n", getSuffixName(offset),
            loadUnaligned<uint16_t>(suffix.data() + offset - sizeof(uint16_t)));
        offset -= 2;
      } else if (offset >= 1) {
        ss << fmt::format("uint8_t {} = {};\n", getSuffixName(offset),
                          suffix[offset - 1]);
        offset -= 1;
      }
    }
  }
}

void automata::codegen::cpp::CppCompiler::generateBackwards(
    const std::optional<parsing::LikePatternAutomaton::AutomatonParams>
        &params) {
  stateCompiler = std::make_unique<CppStateCodegen>(ss, enableSIMD);
  std::string type = "BwdState";
  int8_t direction = -1;
  ss << fmt::format("enum class {} {{\n", type);
  for (const State *state : params->states) {
    ss << fmt::format("\tq{},\n", reinterpret_cast<uintptr_t>(state));
  }
  ss << "};\n";

  ss << fmt::format("\n{}{{\n", generateCppFunctionSignature(direction));
  if (enableSIMD && params->deterministicPath.size() >= 16) {
    ss << "\t__m128i suffix;\n";
  }
  generateSuffixCheck(params->deterministicPath.data() +
                          params->deterministicPath.size(),
                      params->deterministicPath.size(), 0);
  ss << fmt::format("\tauto q = {};\n",
                    getEnumStateName(params->start, direction));
  ss << fmt::format("\tuint64_t level = {};\n", params->start->level);
  std::string condition = "*strIdx + 1 >= level";
  ss << fmt::format("\twhile ({}) {{\n", condition);
  ss << fmt::format("\t\tswitch(q) {{\n");

  std::vector<const State *> pseudoEnds{};
  for (const State *state : params->states) {
    if (state->endIdx.has_value()) {
      pseudoEnds.push_back(state);
    }
    stateCompiler->generate(
        state, direction, params->error, params->acceptStates->data(),
        params->acceptStates->data() + params->acceptStates->size());
  }
  ss << "\t\t}\n";
  ss << fmt::format("\t\t*strIdx += {};\n", direction);
  ss << "\t}\n";
  ss << "\tswitch(q){\n";
  for (size_t idx = 0; idx < params->acceptStates->size(); ++idx) {
    ss << fmt::format(
        "\t\tcase {}:\n",
        getEnumStateName(&params->acceptStates->data()[idx], direction));
    ss << fmt::format("\t\t\t*symIdx = {};\n", idx);
    ss << fmt::format("\t\t\t\t*strIdx -= {};\n", direction - 1);
    ss << fmt::format("\t\t\treturn true;\n");
  }
  for (const State *pseudoEnd : pseudoEnds) {
    ss << fmt::format("\t\tcase {}:\n", getEnumStateName(pseudoEnd, direction));
    ss << fmt::format("\t\t\t*symIdx = {};\n", pseudoEnd->endIdx.value());
    ss << fmt::format("\t\t\t*strIdx -= {};\n", direction);
    ss << fmt::format("\t\t\treturn true;\n");
  }
  ss << "\t\tdefault:\n";
  ss << "\t\t\treturn false;\n";
  ss << "\t}\n";
  ss << "}\n\n";
}

void automata::codegen::cpp::CppCompiler::generateForwards(
    const std::optional<parsing::LikePatternAutomaton::AutomatonParams>
        &params) {
  stateCompiler = std::make_unique<CppStateCodegen>(ss, enableSIMD);
  std::string type = "FwdState";
  int8_t direction = 1;
  ss << fmt::format("enum class {} {{\n", type);
  for (const State *state : params->states) {
    ss << fmt::format("\tq{},\n", reinterpret_cast<uintptr_t>(state));
  }
  ss << "};\n";

  ss << fmt::format("\n{}{{\n", generateCppFunctionSignature(direction));
  if (enableSIMD && params->deterministicPath.size() >= 16) {
    ss << fmt::format("\t__m128i prefix;\n");
  }
  generatePrefixCheck(params->deterministicPath.data(),
                      params->deterministicPath.size(), 0);
  ss << fmt::format("\tauto q = {};\n",
                    getEnumStateName(params->start, direction));
  ss << fmt::format("\tuint64_t level = {};\n", params->start->level);
  std::string condition = "*strIdx + level <= len";
  ss << fmt::format("\twhile ({}) {{\n", condition);
  ss << fmt::format("\t\tswitch(q) {{\n");

  for (const State *state : params->states) {
    stateCompiler->generate(
        state, direction, params->error, params->acceptStates->data(),
        params->acceptStates->data() + params->acceptStates->size());
  }
  ss << "\t\t}\n";
  ss << fmt::format("\t\t*strIdx += {};\n", direction);
  ss << "\t}\n";
  ss << "\tswitch(q){\n";
  for (size_t idx = 0; idx < params->acceptStates->size(); ++idx) {
    ss << fmt::format(
        "\t\tcase {}:\n",
        getEnumStateName(&params->acceptStates->data()[idx], direction));
    ss << fmt::format("\t\t\t*symIdx = {};\n", idx);
    ss << fmt::format("\t\t\t*strIdx -= {};\n", direction);
    ss << fmt::format("\t\t\treturn true;\n");
  }
  ss << "\t\tdefault:\n";
  ss << "\t\t\treturn false;\n";
  ss << "\t}\n";
  ss << "}\n\n";
}

void automata::codegen::cpp::CppCompiler::generateFullParse(
    const std::optional<size_t> &backwardsLevel,
    const std::optional<size_t> &forwardLevel, const ParsingType &type) {
  ss << "extern \"C\" bool parse(const uint8_t* compressed, size_t len) {\n";
  switch (type) {
  case ParsingType::NO_DIRECTION:
    ss << "\treturn false;";
    break;
  case ParsingType::ONLY_FORWARD:
    ss << fmt::format("\tif (len < {})\n", forwardLevel.value());
    ss << "\t\treturn false;\n";
    ss << "\tint64_t strIdx = 0;\n";
    ss << "\tuint8_t symIdx;\n";
    ss << fmt::format("\treturn {}(compressed, len, &strIdx, &symIdx);\n",
                      CppCompiler::getForwardParseFunctionName());
    break;
  case ParsingType::ONLY_BACKWARDS:
    ss << fmt::format("\tif (len < {})\n", backwardsLevel.value());
    ss << "\t\treturn false;\n";
    ss << "\tint64_t strIdx = static_cast<int64_t>(len) - 1;\n";
    ss << "\tuint8_t symIdx;\n";
    ss << fmt::format("\treturn {}(compressed, len, &strIdx, &symIdx);\n",
                      CppCompiler::getBackwardsParseFunctionName());
    break;
  case ParsingType::BOTH_DIRECTIONS:
    ss << fmt::format("\tif (len < {})\n",
                      forwardLevel.value() + backwardsLevel.value() - 1);
    ss << "\t\treturn false;\n";
    ss << "\tint64_t strIdx = static_cast<int64_t>(len) - 1;\n";
    ss << "\tuint8_t endSymIdx;\n";
    ss << fmt::format(
        "\tbool canParse = {}(compressed, len, &strIdx, &endSymIdx);\n",
        CppCompiler::getBackwardsParseFunctionName());
    ss << fmt::format("\tif (!canParse || strIdx + 1 < {})\n",
                      forwardLevel.value());
    ss << "\t\treturn false;\n";
    ss << "\tuint8_t startSymIdx;\n";
    ss << "\tint64_t strIdx2 = 0;\n";
    ss << fmt::format(
        "\tcanParse = {}(compressed, strIdx + 1, &strIdx2, &startSymIdx);\n",
        CppCompiler::getForwardParseFunctionName());
    ss << "\treturn canParse && (strIdx2 < strIdx || startSymIdx <= "
          "endSymIdx);\n";
    break;
  }
  ss << "}";
}

void automata::codegen::cpp::CppCompiler::generatePrefixCheck(
    const uint8_t *prefix, size_t size, size_t offset) {
  switch (size) {
  case 7:
  case 6:
  case 5:
  case 4:
    // case II: prefix length in [4, 7]
    ss << fmt::format(
        "\tif (*reinterpret_cast<const uint32_t*>(compressed + {}) != {})\n",
        offset, getPrefixName(offset));
    ss << fmt::format("\t\treturn false;\n");
    generatePrefixCheck(prefix + 4, size - 4, offset + 4);
    break;
  case 3:
  case 2:
    // case III: prefix length in [2, 3]
    ss << fmt::format(
        "\tif (*reinterpret_cast<const uint16_t*>(compressed + {}) != {})\n",
        offset, getPrefixName(offset));
    ss << fmt::format("\t\treturn false;\n");
    generatePrefixCheck(prefix + 2, size - 2, offset + 2);
    break;
  case 1:
    // case IV: prefix length is 1
    ss << fmt::format(
        "\tif (*reinterpret_cast<const uint8_t*>(compressed + {}) != {})\n",
        offset, getPrefixName(offset));
    ss << fmt::format("\t\treturn false;\n");
    generatePrefixCheck(prefix + 1, size - 1, offset + 1);
    break;
  case 0:
    ss << fmt::format("\t*strIdx = {};\n", offset);
    break;
  default:
    // case I: prefix length >= 8
    if (size >= 16 && enableSIMD) {
      ss << fmt::format("\tprefix = _mm_loadu_si128(reinterpret_cast<const "
                        "__m128i*>(compressed + {}));\n",
                        offset);
      ss << fmt::format(
          "\tif (_mm_movemask_epi8(_mm_cmpeq_epi64(prefix, {})) != 0xFFFF)\n",
          getPrefixName(offset));
      ss << fmt::format("\t\treturn false;\n");
      generatePrefixCheck(prefix + 16, size - 16, offset + 16);
    } else {
      ss << fmt::format(
          "\tif (*reinterpret_cast<const uint64_t*>(compressed + {}) != {})\n",
          offset, getPrefixName(offset));
      ss << fmt::format("\t\treturn false;\n");
      generatePrefixCheck(prefix + 8, size - 8, offset + 8);
    }
    break;
  }
}

void automata::codegen::cpp::CppCompiler::generateSuffixCheck(
    const uint8_t *suffix, size_t size, size_t offset) {
  switch (size) {
  case 7:
  case 6:
  case 5:
  case 4:
    // case II: suffix length in [4, 7]
    ss << fmt::format("\tif (*reinterpret_cast<const uint32_t*>(compressed + "
                      "len - {} - 4) != {})\n",
                      offset, getSuffixName(size));
    ss << fmt::format("\t\treturn false;\n");
    generateSuffixCheck(suffix - 4, size - 4, offset + 4);
    break;
  case 3:
  case 2:
    // case III: suffix length in [2, 3]
    ss << fmt::format("\tif (*reinterpret_cast<const uint16_t*>(compressed + "
                      "len - {} - 2) != {})\n",
                      offset, getSuffixName(size));
    ss << fmt::format("\t\treturn false;\n");
    generateSuffixCheck(suffix - 2, size - 2, offset + 2);
    break;
  case 1:
    // case IV: prefix length is 1
    ss << fmt::format("\tif (*reinterpret_cast<const uint8_t*>(compressed + "
                      "len - {} - 1) != {})\n",
                      offset, getSuffixName(size));
    ss << fmt::format("\t\treturn false;\n");
    generateSuffixCheck(suffix - 1, size - 1, offset + 1);
    break;
  case 0:
    ss << fmt::format("\t*strIdx = len - 1 - {};\n", offset);
    break;
  default:
    // case I: prefix length >= 8
    if (size >= 16 && enableSIMD) {
      ss << fmt::format("\tsuffix = _mm_loadu_si128(reinterpret_cast<const "
                        "__m128i*>(compressed + len - {} - 16));\n",
                        offset);
      ss << fmt::format(
          "\tif (_mm_movemask_epi8(_mm_cmpeq_epi64(suffix, {})) != 0xFFFF)\n",
          getSuffixName(size));
      ss << fmt::format("\t\treturn false;\n");
      generateSuffixCheck(suffix - 16, size - 16, offset + 16);
    } else {
      ss << fmt::format("\tif (*reinterpret_cast<const uint64_t*>(compressed + "
                        "len - {} - 8) != {})\n",
                        offset, getSuffixName(size));
      ss << fmt::format("\t\treturn false;\n");
      generateSuffixCheck(suffix - 8, size - 8, offset + 8);
    }

    break;
  }
}

automata::codegen::cpp::CppStateCodegen::CppStateCodegen(std::stringstream &ss,
                                                         bool &enableSIMD)
    : ss(ss), enableSIMD(enableSIMD) {}

void automata::codegen::cpp::CppStateCodegen::generateEnd(
    const State *state, int direction, std::ptrdiff_t endIdx) {
  ss << fmt::format("\t\t\tcase {}:\n",
                    CppCompiler::getEnumStateName(state, direction));
  ss << fmt::format("\t\t\t\t*symIdx = {};\n", endIdx);
  int offset = direction == 1 ? 1 : -2;
  ss << fmt::format("\t\t\t\t*strIdx -= {};\n", offset);
  ss << fmt::format("\t\t\t\treturn true;\n");
}

void automata::codegen::cpp::CppStateCodegen::generateMiddleStart(
    const State *state, const State *error, int direction) {
  ss << fmt::format("\t\t\tcase {}:\n",
                    CppCompiler::getEnumStateName(state, direction));

  ss << "\t\t\t{\n";
  ss << "\t\t\t\tint64_t maxIdx = len - level;\n";
  ss << "\t\t\t\tuint8_t prevByte;\n";
  ss << "\t\t\t\tint64_t currentStrIdx = *strIdx;\n";
  // case I: no SIMD allowed
  auto parsingType = StateCodegen::doesParsingUseSIMD(state, enableSIMD);
  if (parsingType != ParsingMode::NO_SIMD) {
    ss << "\t\t\t\twhile (currentStrIdx + 15 <= maxIdx) {\n";
    ss << "\t\t\t\t\t__m128i compressedVec = "
          "_mm_loadu_si128(reinterpret_cast<const __m128i*>(compressed + "
          "currentStrIdx));\n";
    if (parsingType == ParsingMode::SIMD_CMPESTRM) {
      ss << fmt::format(
          "\t\t\t\t\t__m128i mask128i = _mm_cmpestrm({}, {}, compressedVec, "
          "16, _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY | _SIDD_BIT_MASK);\n",
          CppCompiler::getSIMDCmpestrmVectorName(state),
          getNumRealTransitions(state));
      ss << "\t\t\t\t\tuint16_t mask = _mm_extract_epi16(mask128i, 0);\n";
    } else {
      ss << "\t\t\t\t\tuint16_t mask = ";
      size_t numTransitions = getNumRealTransitions(state);
      if (numTransitions == 0) {
        ss << "0;\n";
      }
      size_t currentCount = 0;
      for (const auto &[symbol, dest] : state->transitions) {
        if (symbol == 255 && dest->transitions.empty()) {
          continue;
        }
        ss << fmt::format(
            "static_cast<uint16_t>(_mm_movemask_epi8(_mm_cmpeq_epi8({}, "
            "compressedVec)))",
            CppCompiler::getSIMDCmpeqepi8VectorName(state, symbol));
        if (currentCount == numTransitions - 1) {
          ss << ";\n";
        } else {
          ss << " | ";
        }
        ++currentCount;
      }
    }
    ss << "\t\t\t\t\twhile (mask != 0) {\n";
    ss << "\t\t\t\t\t\tint index = std::countr_zero(mask);\n";
    ss << "\t\t\t\t\t\tsize_t currentIndex = currentStrIdx + index;\n";
    ss << "\t\t\t\t\t\tif (currentIndex == 0 || compressed[currentIndex - 1] "
          "!= 255) {\n";
    ss << "\t\t\t\t\t\t\tcurrentStrIdx = currentIndex;\n";
    ss << fmt::format("\t\t\t\t\t\t\tgoto {};\n", CppCompiler::getLabel(state));
    ss << "\t\t\t\t\t\t}\n";
    ss << "\t\t\t\t\t\tmask &= mask - 1;\n";
    ss << "\t\t\t\t\t}\n";
    ss << "\t\t\t\t\tcurrentStrIdx += 16;\n";
    ss << "\t\t\t\t}\n";
  }

  ss << "\t\t\t\tif (currentStrIdx > 0) {\n";
  ss << "\t\t\t\t\tprevByte = compressed[currentStrIdx - 1];\n";
  ss << "\t\t\t\t} else {\n";
  ss << "\t\t\t\t\tprevByte = 0;\n";
  ss << "\t\t\t\t}\n";
  ss << fmt::format("\t\t\t\twhile (currentStrIdx <= maxIdx && "
                    "(!{}[compressed[currentStrIdx]] || prevByte == 255)) {{\n",
                    CppCompiler::getTransitionArrayName(state));
  ss << fmt::format("\t\t\t\t\tprevByte = compressed[currentStrIdx];\n");
  ss << fmt::format("\t\t\t\t\t++currentStrIdx;\n");
  ss << "\t\t\t\t}\n";

  ss << fmt::format("\t\t\t\tif (currentStrIdx > maxIdx)\n");
  ss << fmt::format("\t\t\t\t\treturn false;\n");
  ss << fmt::format("\t\t\t\t{}:\n", CppCompiler::getLabel(state));
  ss << "\t\t\t\t\t*strIdx = currentStrIdx;\n";
  ss << "\t\t\t\t\tswitch(compressed[currentStrIdx]){\n";

  std::unordered_map<State *, std::vector<uint8_t>> destinationToSymbols{};
  for (auto &[symbol, next] : state->transitions) {
    if (symbol == 255 && next->transitions.empty()) {
      continue;
    }
    if (next == state->defaultTransition) {
      continue;
    }
    destinationToSymbols[next].push_back(symbol);
  }

  for (auto &[next, symbols] : destinationToSymbols) {
    for (uint8_t symbol : symbols) {
      ss << fmt::format("\t\t\t\t\t\tcase {}:\n", symbol);
    }
    ss << fmt::format("\t\t\t\t\t\t\tq = {};\n",
                      CppCompiler::getEnumStateName(next, direction));
    ss << fmt::format("\t\t\t\t\t\t\tlevel = {};\n", next->level);
    ss << fmt::format("\t\t\t\t\t\t\tbreak;\n");
  }
  ss << "\t\t\t\t\t}\n";
  ss << "\t\t\t\tbreak;\n";
  ss << "\t\t\t}\n";
}

void automata::codegen::cpp::CppStateCodegen::generateError(const State *state,
                                                            int direction) {
  ss << fmt::format("\t\t\tcase {}:\n",
                    CppCompiler::getEnumStateName(state, direction));
  ss << "\t\t\t\treturn false;\n";
}

void automata::codegen::cpp::CppStateCodegen::generateOther(const State *state,
                                                            const State *error,
                                                            int direction) {
  ss << fmt::format("\t\t\tcase {}:\n",
                    CppCompiler::getEnumStateName(state, direction));
  ss << "\t\t\t\tswitch(compressed[*strIdx]){\n";
  std::unordered_map<State *, std::vector<uint8_t>> destinationToSymbols{};
  for (auto &[symbol, next] : state->transitions) {
    if (next == state->defaultTransition) {
      continue;
    }
    destinationToSymbols[next].push_back(symbol);
  }
  for (auto &[destination, symbols] : destinationToSymbols) {
    for (uint8_t symbol : symbols) {
      ss << fmt::format("\t\t\t\t\tcase {}:\n", symbol);
    }
    ss << fmt::format("\t\t\t\t\t\tq = {};\n",
                      CppCompiler::getEnumStateName(destination, direction));
    if (destination != error) {
      ss << fmt::format("\t\t\t\t\t\tlevel = {};\n", destination->level);
    }
    ss << fmt::format("\t\t\t\t\t\tbreak;\n");
  }
  ss << "\t\t\t\t\tdefault:\n";
  ss << fmt::format(
      "\t\t\t\t\t\tq = {};\n",
      CppCompiler::getEnumStateName(state->defaultTransition, direction));
  if (state->defaultTransition != error) {
    ss << fmt::format("\t\t\t\t\t\tlevel = {};\n",
                      state->defaultTransition->level);
  }
  ss << fmt::format("\t\t\t\t\t\tbreak;\n");
  ss << fmt::format("\t\t\t\t}}\n\t\t\t\tbreak;\n");
}

std::string automata::codegen::cpp::CppCompiler::generateCppFunctionSignature(
    int8_t direction) {
  if (direction == 1) {
    return fmt::format("bool {}(const uint8_t* compressed, size_t len, "
                       "int64_t* strIdx, uint8_t* symIdx)",
                       Compiler::getForwardParseFunctionName());
  } else {
    return fmt::format("bool {}(const uint8_t* compressed, size_t len, "
                       "int64_t* strIdx, uint8_t* symIdx)",
                       Compiler::getBackwardsParseFunctionName());
  }
}

automata::codegen::cpp::CppCompiler::CppCompiler(const std::string &cppFile,
                                                 const std::string &destination,
                                                 bool enableSIMD,
                                                 bool generateLLVM)
    : cppFile(cppFile), destination(destination), generateLLVM(generateLLVM),
      ss(), enableSIMD(enableSIMD) {}

std::unique_ptr<automata::codegen::Parser>
automata::codegen::cpp::CppCompiler::compile(
    const std::unique_ptr<parsing::LikePatternAutomaton> &automaton) {
  std::ofstream file(cppFile);
  addIncludes();
  generateTransitionArrays(automaton);
  std::optional<size_t> backwardsLevel;
  std::optional<size_t> forwardLevel;
  ParsingType type;
  if (!automaton) {
    type = ParsingType::NO_DIRECTION;
  } else {
    auto backwardParams = automaton->gatherBackwardsParams();
    if (backwardParams.has_value()) {
      backwardsLevel = backwardParams->minLength;
      generateSuffixVariables(backwardParams);
      generateBackwards(backwardParams);
    }
    auto forwardParams = automaton->gatherForwardParams();
    if (forwardParams.has_value()) {
      forwardLevel = forwardParams->minLength;
      generatePrefixVariables(forwardParams);
      generateForwards(forwardParams);
    }
    if (forwardParams.has_value() && backwardParams.has_value()) {
      type = ParsingType::BOTH_DIRECTIONS;
    } else {
      if (forwardParams.has_value()) {
        type = ParsingType::ONLY_FORWARD;
      } else {
        type = ParsingType::ONLY_BACKWARDS;
      }
    }
  }
  generateFullParse(backwardsLevel, forwardLevel, type);
  file << ss.str();
  ss.clear();
  file.close();

  // if (generateLLVM) {
  //     std::string generate_llvm_cmd = fmt::format("clang++ -march=native -O3
  //     -std=c++20 -S -emit-llvm -o optimized_cpp.ll -I../ {}", cppFile);
  //     system(generate_llvm_cmd.c_str());
  // }
  std::string compile_cmd = fmt::format(
      "clang++ -march=native -O3 -g -std=c++20 -shared -o {} -fPIC -I../ {}",
      destination, cppFile);
  system(compile_cmd.c_str());

  void *libraryHandle =
      dlopen(fmt::format("./{}", destination).data(), RTLD_LAZY);

  if (!libraryHandle) {
    throw std::runtime_error(
        fmt::format("Failed to load library: {}\n", dlerror()));
  }
  return std::make_unique<CppParser>(libraryHandle);
}

automata::codegen::cpp::CppCompiler::~CppCompiler() {
  std::filesystem::remove(cppFile);
  std::filesystem::remove(destination);
}

automata::codegen::cpp::CppParser::Function
automata::codegen::cpp::CppParser::getFunctionPointer(
    const char *functionName) {
  dlerror();
  auto fn = reinterpret_cast<Function>(dlsym(libraryHandle, functionName));
  const char *dlsymError = dlerror();
  if (dlsymError) {
    dlclose(libraryHandle);
    throw std::runtime_error(fmt::format("Failed to load symbol {}: {}\n",
                                         functionName, dlsymError));
  }
  return fn;
}

automata::codegen::cpp::CppParser::CppParser(void *libraryHandle)
    : automata::codegen::Parser(), libraryHandle(libraryHandle) {
  parseFunction = getFunctionPointer(CppCompiler::getParseFunctionName());
}

bool automata::codegen::cpp::CppParser::parse(const uint8_t *pattern,
                                              size_t len) {
  return parseFunction(pattern, len);
}

automata::codegen::cpp::CppParser::~CppParser() { dlclose(libraryHandle); }
