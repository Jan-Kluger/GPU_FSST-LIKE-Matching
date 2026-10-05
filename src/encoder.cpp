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

#include "encoder.hpp"
#include <fmt/format.h>
#include <unistd.h>

Bitmap Encoder::createAnywhereBitmap(uint8_t byte) const {
    Bitmap bitmap{};
    for (size_t i = 0; i < encoder->symbolTable->nSymbols; ++i) {
        libfsst::Symbol& symbol = encoder->symbolTable->symbols[i];
        bool present = false;
        for (uint32_t idx = 0; idx < symbol.length(); ++idx) {
            const uint8_t* symBytes = reinterpret_cast<const uint8_t*>(symbol.val.str);
            present |= (symBytes[idx] == byte);
        }
        if (present) {
            bitmap.bitmap.flipBit(i);
        }
    }
    return bitmap;
}

SymbolTable::SymbolTable(std::shared_ptr<libfsst::SymbolTable> &symTable): symTable(symTable) {}

SymbolTable SymbolTable::readFromFile(const char *path) {
    int fd = open(path, O_RDONLY);
    std::shared_ptr<libfsst::SymbolTable> symTable = std::make_shared<libfsst::SymbolTable>();
    if (fd == -1) {
        throw std::runtime_error(fmt::format("Cannot open {}", path));
    }
    ssize_t bytesRead = read(fd, symTable.get(), sizeof(libfsst::SymbolTable));

    if (bytesRead == -1) {
        int err = errno;
        close(fd);
        throw std::runtime_error(fmt::format("Error reading {}: {}", path, strerror(err)));
    }

    if (static_cast<size_t>(bytesRead) < sizeof(libfsst::SymbolTable)) {
        close(fd);
        throw std::runtime_error(fmt::format(
            "Incomplete read for {}: expected {} bytes, got {}",
            path, sizeof(libfsst::SymbolTable), bytesRead
        ));
    }
    close(fd);
    return SymbolTable(symTable);
}

std::shared_ptr<libfsst::SymbolTable> SymbolTable::getSymTable() const {
    return symTable;
}

Bitmap Encoder::createPrefixBitmap(uint8_t byte) const {
    Bitmap bitmap{};
    for (size_t i = 0; i < encoder->symbolTable->nSymbols; ++i) {
        libfsst::Symbol& symbol = encoder->symbolTable->symbols[i];
        const uint8_t* symBytes = reinterpret_cast<const uint8_t*>(symbol.val.str);
        if (symbol.length() > 0 && symBytes[0] == byte) {
            bitmap.bitmap.flipBit(i);
        }
    }
    return bitmap;
}

bool Encoder::isEncodingValid(const libfsst::u8 *strOut, size_t lenOut) const {
    size_t idx = 0;
    while (idx < lenOut) {
        if (strOut[idx] != 255) {
            ++idx;
            continue;
        }

        ++idx;
        if (!isEscapable(strOut[idx])) {
            return false;
        }
        ++idx;
    }
    return true;
}

std::vector<std::vector<uint8_t>> Encoder::findAllSymbolsWithSuffix(const std::span<const uint8_t> &match, uint8_t max_n) const {
    Bitmap bitmap = this->createAnywhereBitmap(match[0]);
    std::vector<std::vector<uint8_t>> starts{};
    starts.resize(max_n);

    // TODO: find all symbols suffixed by our string
    // TODO: make this more efficient
    for (uint8_t idx: bitmap) {
        libfsst::Symbol& symbol = encoder->symbolTable->symbols[idx];
        uint8_t current_max_n = (symbol.length() - 1) > max_n ? max_n : symbol.length() - 1;
        for (uint8_t prefix_size = 1; prefix_size <= current_max_n; ++prefix_size) {
            bool prefixes = true;
            for (uint8_t i = 0; i < prefix_size; ++i) {
                const uint8_t* symBytes = reinterpret_cast<const uint8_t*>(symbol.val.str);
                if (symBytes[symbol.length() - prefix_size + i] != match[i]) {
                    prefixes = false;
                    break;
                }
            }
            if (prefixes) {
                starts[prefix_size - 1].push_back(idx);
            }
        }
    }
    return starts;
}

bool Encoder::isEscapable(uint8_t byte) const {
    return reinterpret_cast<bool*>(&encoder->symbolTable->symbols[255])[byte];
}

std::array<std::optional<uint8_t>, 3> Bitmap::getExactPrefixMatches(const std::span<const uint8_t> &string, size_t current, const Encoder &encoder) const {
    std::array<std::optional<uint8_t>, 3> exact_match{};
    for (uint8_t i: *this) {
        const libfsst::Symbol& symbol = encoder.symbols()[i];
        const uint8_t* symBytes;
        switch (symbol.length()) {
            case 0:
                break;
            case 1:
                exact_match[0] = i;
                break;
            case 2:
                symBytes = reinterpret_cast<const uint8_t*>(symbol.val.str);
                if (current + 1 < string.size() && symBytes[1] == string[current + 1]) {
                    exact_match[1] = i;
                }
                break;
            default:
                symBytes = reinterpret_cast<const uint8_t*>(symbol.val.str);
                if (current + 2 < string.size() && symBytes[1] == string[current + 1] && symBytes[2] == string[current + 2]) {
                    exact_match[2] = i;
                }
                break;
        }
    }
    return exact_match;
}

void Bitmap::BitmapIterator::advance() {
    uint16_t index = bitmap.countl_zero();
    bitmap.flipBit(index);
}

Bitmap::BitmapIterator Bitmap::BitmapIterator::begin(const Bitmap& bitmap) {
    return BitmapIterator(bitmap);
}

Bitmap::BitmapIterator Bitmap::BitmapIterator::end() {
    return BitmapIterator(Bitmap{});
}