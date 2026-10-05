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

#include "utils.hpp"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <fmt/format.h>
#include <fstream>

file::FileData::FileData(size_t totalLen, size_t n): n(n), totalLen(totalLen) {}

size_t file::FileData::getNumElements() const {
    return n;
}

size_t file::FileData::getTotalLength() const {
    return totalLen;
}

file::ASCIIFileData::ASCIIFileData(size_t totalLength, size_t n, std::unique_ptr<unsigned char[]> &buffer, std::unique_ptr<size_t[]> &indexes): FileData(totalLength, n), buffer(std::move(buffer)), indexes(std::move(indexes)) {}

const size_t* file::ASCIIFileData::getIndexes() const {
    return indexes.get();
}

const uint8_t* file::ASCIIFileData::getBuffer() const {
    return buffer.get();
}

std::span<const uint8_t> file::ASCIIFileData::get(size_t index) {
    uint8_t* loc = buffer.get() + indexes[index];
    size_t len = index == n - 1 ? totalLen - indexes[index] : indexes[index + 1] - indexes[index];
    return std::span<const uint8_t>(loc, len);
}

file::BinaryFileData::BinaryFileData(int handle, uint8_t *data, size_t size): FileData(reinterpret_cast<size_t*>(data)[0], reinterpret_cast<size_t*>(data)[1]), handle(handle), data(data), size(size) {}

const size_t * file::BinaryFileData::getIndexes() const {
    return reinterpret_cast<const size_t*>(data + sizeof(n) + sizeof(totalLen) + totalLen);
}

const uint8_t * file::BinaryFileData::getBuffer() const {
    return data + sizeof(n) + sizeof(totalLen);
}

std::span<const uint8_t> file::BinaryFileData::get(size_t index) {
    size_t startIdx = loadUnaligned<size_t>(reinterpret_cast<const uint8_t*>(&getIndexes()[index]));
    const uint8_t* loc = getBuffer() + startIdx;
    size_t endIdx = loadUnaligned<size_t>(reinterpret_cast<const uint8_t*>(&getIndexes()[index + 1]));
    size_t len = index == n - 1 ? totalLen - startIdx : endIdx - startIdx;
    return std::span<const uint8_t>(loc, len);
}

file::BinaryFileData::~BinaryFileData() {
    munmap(data, size);
    close(handle);
}

void file::writeChunks(int fd, const void* buffer, size_t totalLen) {
    const uint8_t* data = static_cast<const uint8_t*>(buffer);
    size_t CHUNK_SIZE = 0x7ffff000;
    size_t numChunks = (totalLen + CHUNK_SIZE - 1) / CHUNK_SIZE;
    for (size_t chunk = 0; chunk < numChunks; ++chunk) {
        size_t offset = chunk * CHUNK_SIZE;
        size_t lenToWrite = std::min(CHUNK_SIZE, totalLen - offset);
        write(fd, data + offset, lenToWrite);
    }
}

void file::readChunks(int fd, void* buffer, size_t totalLen) {
    uint8_t* data = static_cast<uint8_t*>(buffer);
    size_t CHUNK_SIZE = 0x7ffff000;
    size_t numChunks = (totalLen + CHUNK_SIZE - 1) / CHUNK_SIZE;
    for (size_t chunk = 0; chunk < numChunks; ++chunk) {
        size_t offset = chunk * CHUNK_SIZE;
        size_t lenToRead = std::min(CHUNK_SIZE, totalLen - offset);
        read(fd, data + offset, lenToRead);
    }
}

void file::dumpBinary(const char* path, size_t n, size_t totalLen, const size_t* lenOut, const uint8_t* buffer) {
    int fd = open(path, O_WRONLY | O_CREAT, 0666);
    if (fd == -1) {
        throw std::runtime_error(fmt::format("Could not open {}", path));
    }
    write(fd, &totalLen, sizeof(totalLen));
    write(fd, &n, sizeof(n));
    writeChunks(fd, buffer, totalLen);
    size_t runningIndex = 0;
    for (size_t idx = 0; idx < n; ++idx) {
        write(fd, &runningIndex, sizeof(runningIndex));
        runningIndex += lenOut[idx];
    }
    close(fd);
}

std::unique_ptr<file::FileData> file::readASCIIFileData(const char* path) {
    size_t n = 0;
    size_t totalLen = 0;

    {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error(fmt::format("Could not open {}", path));
        }
        std::string line;
        while (std::getline(file, line)) {
            n += !line.empty();
            totalLen += line.size();
        }
        file.close();
    }

    std::unique_ptr<uint8_t[]> buffer = std::make_unique<uint8_t[]>(totalLen);
    std::unique_ptr<size_t[]> len = std::make_unique<size_t[]>(n);

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error(fmt::format("Could not open {}", path));
    }
    std::string line;

    size_t idx = 0;
    size_t accumulatedLength = 0;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            memcpy(buffer.get() + accumulatedLength, line.data(), line.size());
            len[idx] = accumulatedLength;
            accumulatedLength += line.size();
            ++idx;
        }
    }
    file.close();
    return std::make_unique<ASCIIFileData>(totalLen, n, buffer, len);
}

std::unique_ptr<file::FileData> file::readBinaryFileData(const char* path, int mmapFlags) {
    int handle = open(path, O_RDONLY);
    if (handle < 0) {
        throw std::runtime_error(fmt::format("Could not open file {}", path));
    }
    lseek(handle, 0, SEEK_END);
    size_t size = lseek(handle, 0, SEEK_CUR);
    uint8_t* data = static_cast<uint8_t*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE | mmapFlags, handle, 0));
    return std::make_unique<BinaryFileData>(handle, data, size);
}