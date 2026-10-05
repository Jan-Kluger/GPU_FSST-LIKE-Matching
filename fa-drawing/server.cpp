//
// Created by popca on 02-May-25.
//
#include <vector>
#include <fmt/format.h>
#include "automata.hpp"
#include "like_pattern_automaton.hpp"
#include "serializer.hpp"
#include "pattern.hpp"
#include <filesystem>
#include <sstream>
#include <netinet/in.h>
#include "utils.hpp"
#include "codegen/cppcodegen.hpp"
#include "codegen/llvmcodegen.hpp"
#include <nlohmann/json.hpp>
#include <unistd.h>

std::unique_ptr<automata::parsing::LikePatternAutomaton> generateFullAutomaton(const Encoder& encoder, const std::string &patternString) {
    std::span<const uint8_t> pattern(reinterpret_cast<const uint8_t *>(patternString.data()), patternString.size());
    return automata::parsing::LikePatternAutomaton::build(pattern, encoder);
}

std::basic_string<uint8_t> serializeFullPattern(const std::string &symbolTablePath, const std::string &patternString) {
    Encoder encoder(SymbolTable::readFromFile(symbolTablePath.c_str()));
    auto automaton = generateFullAutomaton(encoder, patternString);
    auto symbolTable = encoder.getSymbolTable();
    auto serializer = automata::serializer::LikePatternAutomatonSerializer{automaton, symbolTable};

    return serializer.serialize();
}

std::basic_string<uint8_t> serializeMultipleStartsFiniteAutomaton(const std::string &symbolTablePath, const std::string &patternString) {
    Encoder encoder(SymbolTable::readFromFile(symbolTablePath.c_str()));
    std::span<const uint8_t> patternSpan = std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(patternString.data()), patternString.size());
    std::unique_ptr<Pattern> pattern = Pattern::createPattern(patternSpan);

    std::vector<automata::State> endNodes(9);
    std::vector<automata::State *> precomputedEnds(9, nullptr);
    automata::State errorNode;
    for (uint8_t idx = 0; idx < 9; ++idx) {
        precomputedEnds[idx] = &endNodes[idx];
        endNodes[idx].defaultTransition = &endNodes[idx];
        endNodes[idx].level = 0;
    }

    std::array<automata::State, 8> startNodes{};
    automata::MultipleStartsFiniteAutomaton automaton = pattern->createMiddleAutomaton(encoder, precomputedEnds,&errorNode, startNodes);
    auto symbolTable = encoder.getSymbolTable();
    automata::serializer::MultipleStartsFiniteAutomataSerializer serializer{automaton, endNodes, symbolTable};
    return serializer.serialize();
}

std::basic_string<uint8_t> serializeSingleStartFiniteAutomata(const std::string &symbolTablePath, const std::string &patternString, bool start) {
    Encoder encoder(SymbolTable::readFromFile(symbolTablePath.c_str()));
    std::span<const uint8_t> patternSpan = std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(patternString.data()), patternString.size());
    std::unique_ptr<Pattern> patternObject = Pattern::createPattern(patternSpan);

    std::vector<automata::State> endNodes(9);
    std::vector<automata::State *> precomputedEnds(9, nullptr);
    automata::State errorNode;
    for (uint8_t idx = 0; idx < 9; ++idx) {
        precomputedEnds[idx] = &endNodes[idx];
        endNodes[idx].defaultTransition = &endNodes[idx];
        endNodes[idx].level = 0;
    }

    automata::SingleStartFiniteAutomaton automaton = start ? patternObject->createStartAutomaton(encoder, precomputedEnds, &errorNode)
                                                           : patternObject->createEndAutomaton(encoder, precomputedEnds, &errorNode);

    auto symbolTable = encoder.getSymbolTable();
    automata::serializer::SingleStartFiniteAutomataSerializer serializer{automaton, endNodes, symbolTable};
    return serializer.serialize();
}

std::string removeExtension(const std::string& filePath) {
    std::filesystem::path p(filePath);

    if (p.has_extension()) {
        return p.replace_extension("").string();
    }

    return filePath;
}


std::unique_ptr<size_t[]> getLenIn(const std::unique_ptr<file::FileData>& fileData) {
    size_t n = fileData->getNumElements();
    std::unique_ptr<size_t[]> lengths = std::make_unique<size_t[]>(n);
    for (size_t idx = 0; idx < n - 1; ++idx) {
        lengths[idx] = fileData->getIndexes()[idx + 1] - fileData->getIndexes()[idx];
    }
    lengths[n - 1] = fileData->getTotalLength() - fileData->getIndexes()[n - 1];
    return lengths;
}

std::unique_ptr<const uint8_t*[]> getStrIn(const std::unique_ptr<file::FileData>& fileData) {
    size_t n = fileData->getNumElements();
    std::unique_ptr<const uint8_t*[]> strings = std::make_unique<const uint8_t*[]>(n);
    for (size_t idx = 0; idx < n; ++idx) {
        strings[idx] = fileData->getBuffer() + fileData->getIndexes()[idx];
    }
    return strings;
}

std::tuple<std::string, std::string, std::string> compressFile(const std::string& filePath) {
    std::string removedExtensionPath = removeExtension(filePath);
    std::string symTablePath = fmt::format("{}_symTable.bin", removedExtensionPath);
    std::string uncompressedPath = fmt::format("{}_uncompressed.bin", removedExtensionPath);
    std::string compressedPath = fmt::format("{}_compressed.bin", removedExtensionPath);
    auto fileData = file::readASCIIFileData(filePath.c_str());
    std::unique_ptr<size_t[]> lenIn = getLenIn(fileData);
    std::unique_ptr<const uint8_t*[]> strIn = getStrIn(fileData);
    fsst_encoder_t* fsst_encoder = fsst_create(fileData->getNumElements(), lenIn.get(), strIn.get(), 0);
    std::shared_ptr<libfsst::SymbolTable> symTable = reinterpret_cast<libfsst::Encoder*>(fsst_encoder)->symbolTable;
    file::dumpBinary(uncompressedPath.c_str(), fileData->getNumElements(), fileData->getTotalLength(), lenIn.get(), fileData->getBuffer());

    size_t maxBufferSize = 2 * fileData->getTotalLength();
    std::unique_ptr<unsigned char[]> buffer = std::make_unique<unsigned char[]>(maxBufferSize);
    std::unique_ptr<size_t[]> lenOut = std::make_unique<size_t[]>(fileData->getNumElements());
    std::unique_ptr<unsigned char*[]> strOut = std::make_unique<unsigned char*[]>(fileData->getNumElements());
    fsst_compress(fsst_encoder, fileData->getNumElements(), lenIn.get(), strIn.get(), maxBufferSize, buffer.get(), lenOut.get(), strOut.get());

    {
        bool bitmap[256] = {false};
        for (size_t idx = 0; idx < fileData->getNumElements(); ++idx) {
            size_t strIdx = 0;
            unsigned char* start = strOut[idx];
            while (strIdx < lenOut[idx]) {
                if (start[strIdx] == 255) {
                    ++strIdx;

                    uint8_t escaped = start[strIdx];
                    bitmap[escaped] = true;
                }
                ++strIdx;
            }
        }
        void* dest = &symTable->symbols[255];
        memcpy(dest, bitmap, 256 * sizeof(bool));
    }

    size_t totalLen = 0;
    for (size_t idx = 0; idx < fileData->getNumElements(); ++idx) {
        totalLen += lenOut[idx];
    }
    file::dumpBinary(compressedPath.c_str(), fileData->getNumElements(), totalLen, lenOut.get(), buffer.get());
    {
        int fd = open(symTablePath.c_str(), O_WRONLY | O_CREAT, 0644);
        if (fd == -1) {
            throw std::runtime_error(fmt::format("Could not open symbol table path: {}", symTablePath));
        }
        write(fd, symTable.get(), sizeof(libfsst::SymbolTable));
        close(fd);
    }
    return {symTablePath, uncompressedPath, compressedPath};
}

std::unordered_map<std::string, std::string> parseGETParams(const std::string &url) {
    std::unordered_map<std::string, std::string> params;
    size_t pos = url.find('?');
    if (pos == std::string::npos) return params;

    std::string query = url.substr(pos + 1);
    std::stringstream ss(query);
    std::string item;
    while (std::getline(ss, item, '&')) {
        size_t eq = item.find('=');
        if (eq != std::string::npos) {
            params[item.substr(0, eq)] = item.substr(eq + 1);
        }
    }
    params["endpoint"] = url.substr(1, pos - 1);
    return params;
}

// Function to Decode URI Parameters
std::string decodeURI(const std::string &value) {
    std::string result;
    result.reserve(value.length());

    auto hexToValue = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };

    for (size_t i = 0; i < value.length(); ++i) {
        // Check for a % followed by at least two characters
        if (value[i] == '%' && i + 2 < value.length()) {
            char hi = value[i + 1];
            char lo = value[i + 2];

            // Verify that both characters are valid hexadecimal digits
            if (std::isxdigit(static_cast<unsigned char>(hi)) &&
                std::isxdigit(static_cast<unsigned char>(lo))) {

                char decodedChar = static_cast<char>((hexToValue(hi) << 4) | hexToValue(lo));
                result += decodedChar;
                i += 2; // Skip the two hex digits
                continue;
            }
        }
        else if (value[i] == '+') {
            result += ' ';
            continue;
        }
        result += value[i];
    }

    return result;
}

void sendOKResponse(int socket, const char* response, size_t size) {
    std::string header =
            fmt::format("HTTP/1.1 200 OK\r\n"
                        "Content-Type: application/octet-stream\r\n"
                        "Access-Control-Allow-Origin: *\r\n"
                        "Content-Length: {}\r\n"
                        "\r\n", size);

    send(socket, header.c_str(), header.length(), 0);
    send(socket, response, size, 0);
}

void handleGenerateEndpoint(int socket, const std::unordered_map<std::string, std::string>& params) {
    std::string pattern = decodeURI(params.find("pattern")->second);
    filesystem::path relativePath = decodeURI(params.find("symTablePath")->second);
    filesystem::path absolutePath = filesystem::absolute(relativePath);
    std::string symbolTablePath = relativePath.string();
    std::string type = decodeURI(params.find("type")->second);
    fmt::println("Called generate endpoint with pattern {}, symbol table path {} and pattern type {}", pattern, absolutePath.string(), type);
    try {
        std::basic_string<uint8_t> response;
        if (type == "full")
            response = serializeFullPattern(symbolTablePath, pattern);
        else if (type == "start")
            response = serializeSingleStartFiniteAutomata(symbolTablePath, pattern, true);
        else if (type == "end")
            response = serializeSingleStartFiniteAutomata(symbolTablePath, pattern, false);
        else if (type == "middle")
            response = serializeMultipleStartsFiniteAutomaton(symbolTablePath, pattern);
        sendOKResponse(socket, reinterpret_cast<const char*>(response.data()), response.size());
    } catch (const std::exception &e) {
        std::string errorMsg = "HTTP/1.1 500 Internal Server Error\r\n\r\n" + std::string(e.what());
        send(socket, errorMsg.c_str(), errorMsg.length(), 0);
    }
}

void handleCompressEndpoint(int socket, const std::unordered_map<std::string, std::string>& params) {
    std::string filepath = (filesystem::path("..") / decodeURI(params.find("filepath")->second));
    fmt::println("Called compress endpoint with filepath {}", filepath);
    auto [symTablePath, uncompressedPath, compressedPath] = compressFile(filepath);

    nlohmann::json j;
    j["symbolTablePath"] = symTablePath;
    j["uncompressedPath"] = uncompressedPath;
    j["compressedPath"] = compressedPath;
    std::string response = j.dump();
    sendOKResponse(socket, response.c_str(), response.size());
}

void handleCodegenEndpoint(int socket, const std::unordered_map<std::string, std::string>& params) {
    std::string pattern = decodeURI(params.find("pattern")->second);
    filesystem::path relativePath = decodeURI(params.find("symTablePath")->second);
    filesystem::path absolutePath = filesystem::absolute(relativePath);
    std::string symbolTablePath = relativePath.string();    std::string type = decodeURI(params.find("type")->second);
    std::string language = decodeURI(params.find("language")->second);
    fmt::println("Called codegen endpoint having pattern {}, symbol table path {}, pattern type {} and language {}", pattern, absolutePath.string(), type, language);
    auto fileToResponse = [&](const std::string& filename) -> std::basic_string<uint8_t> {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);

        if (!file.is_open()) {
            std::string errorMsg = "HTTP/1.1 500 Could Not Open File\r\n\r\n";
            send(socket, errorMsg.c_str(), errorMsg.length(), 0);
        }

        std::basic_string<uint8_t> buffer;
        std::streamsize size = file.tellg();

        if (size > 0) {
            buffer.resize(static_cast<size_t>(size));
            file.seekg(0, std::ios::beg);

            // Stream directly into the string data backing array
            file.read(reinterpret_cast<char*>(buffer.data()), size);
        }

        return buffer;
    };

    try {
        std::basic_string<uint8_t> response;
        std::string fullPattern;
        if (type == "full")
            fullPattern = pattern;
        else if (type == "start")
            fullPattern = fmt::format("{}%", pattern);
        else if (type == "end")
            fullPattern = fmt::format("%{}", pattern);
        else if (type == "middle")
            fullPattern = fmt::format("%{}%", pattern);

        Encoder encoder(SymbolTable::readFromFile(symbolTablePath.c_str()));
        auto automaton = generateFullAutomaton(encoder, fullPattern);
        if (language == "cpp") {
            std::string cppSource = "cppSource.cpp";
            automata::codegen::cpp::CppCompiler compiler{cppSource, "libgenerated.so", true};
            auto parser = compiler.compile(automaton);
            response = fileToResponse(cppSource);
        } else if (language == "llvm") {
            automata::codegen::llvmir::LLVMCompiler compiler{true, true};
            auto parser = compiler.compile(automaton);
            response = fileToResponse("optimized_llvm.ll");
        } else {
            throw std::runtime_error(fmt::format("Invalid language to compile {}", language));
        }
        sendOKResponse(socket, reinterpret_cast<const char*>(response.data()), response.size());
    } catch (const std::exception &e) {
        std::string errorMsg = "HTTP/1.1 500 Internal Server Error\r\n\r\n" + std::string(e.what());
        send(socket, errorMsg.c_str(), errorMsg.length(), 0);
    }
}

int main() {
    int serverFd, newSocket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    int port = 8080;

    if ((serverFd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (::bind(serverFd, (struct sockaddr *) &address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(serverFd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    std::cout << "Server running on port " << port << "..." << std::endl;

    while (true) {
        if ((newSocket = accept(serverFd, (struct sockaddr *) &address, (socklen_t *) &addrlen)) < 0) {
            perror("accept");
            continue;
        }

        char buffer[1024] = {0};
        read(newSocket, buffer, 1024);

        std::string request(buffer);
        // Basic extraction of the URL path from "GET /path?query HTTP/1.1"
        size_t firstSpace = request.find(' ');
        size_t secondSpace = request.find(' ', firstSpace + 1);

        if (firstSpace != std::string::npos && secondSpace != std::string::npos) {
            std::string url = request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
            std::unordered_map<std::string, std::string> params = parseGETParams(url);

            if (params["endpoint"] == "generate") {
                handleGenerateEndpoint(newSocket, params);
            } else if (params["endpoint"] == "compress") {
                handleCompressEndpoint(newSocket, params);
            } else if (params["endpoint"] == "codegen") {
                handleCodegenEndpoint(newSocket, params);
            }

        }
        close(newSocket);
    }
    return 0;
}