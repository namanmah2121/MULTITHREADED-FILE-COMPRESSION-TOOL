#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <zlib.h>

#define BLOCK_SIZE 1024 * 1024  // 1MB blocks

struct CompressedBlock {
    std::vector<char> data;
    uLong originalSize;
};

// Compress a block of data
bool compressBlock(const std::vector<char>& input, std::vector<char>& output) {
    uLong srcLen = input.size();
    uLong destLen = compressBound(srcLen);
    output.resize(destLen);

    int res = compress(reinterpret_cast<Bytef*>(&output[0]), &destLen,
                       reinterpret_cast<const Bytef*>(&input[0]), srcLen);
    if (res != Z_OK) return false;

    output.resize(destLen);  // Trim unused space
    return true;
}

// Decompress a block of data
bool decompressBlock(const std::vector<char>& input, std::vector<char>& output, uLong originalSize) {
    output.resize(originalSize);
    uLongf destLen = originalSize;

    int res = uncompress(reinterpret_cast<Bytef*>(&output[0]), &destLen,
                         reinterpret_cast<const Bytef*>(&input[0]), input.size());
    return res == Z_OK;
}

// Thread worker to compress blocks
void compressWorker(std::ifstream& inFile, std::vector<CompressedBlock>& compressedData,
                    std::mutex& fileMutex, std::mutex& dataMutex) {
    while (true) {
        std::vector<char> block(BLOCK_SIZE);
        uLong bytesRead = 0;

        {
            std::lock_guard<std::mutex> lock(fileMutex);
            if (inFile.eof()) break;
            inFile.read(block.data(), BLOCK_SIZE);
            bytesRead = inFile.gcount();
            if (bytesRead == 0) break;
            block.resize(bytesRead);
        }

        std::vector<char> compressedBlock;
        if (compressBlock(block, compressedBlock)) {
            std::lock_guard<std::mutex> lock(dataMutex);
            compressedData.push_back({compressedBlock, bytesRead});
        }
    }
}

// Decompress all compressed blocks into a file
void decompressToFile(const std::vector<CompressedBlock>& compressedData, const std::string& outputFile) {
    std::ofstream out(outputFile, std::ios::binary);
    for (const auto& block : compressedData) {
        std::vector<char> decompressed;
        if (decompressBlock(block.data, decompressed, block.originalSize)) {
            out.write(decompressed.data(), decompressed.size());
        }
    }
    out.close();
}

int main() {
    std::string inputFile = "test_file.txt";
    std::string outputFile = "decompressed_output.txt";

    std::ifstream in(inputFile, std::ios::binary);
    if (!in) {
        std::cerr << "Error: Cannot open " << inputFile << std::endl;
        return 1;
    }

    std::vector<CompressedBlock> compressedData;
    std::mutex fileMutex, dataMutex;

    auto start = std::chrono::high_resolution_clock::now();

    int threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0) threadCount = 4;

    std::vector<std::thread> threads;
    for (int i = 0; i < threadCount; ++i)
        threads.emplace_back(compressWorker, std::ref(in), std::ref(compressedData), std::ref(fileMutex), std::ref(dataMutex));

    for (auto& t : threads) t.join();
    in.close();

    auto end = std::chrono::high_resolution_clock::now();
    double seconds = std::chrono::duration<double>(end - start).count();

    std::cout << "Compressed using " << threadCount << " threads in " << seconds << " seconds.\n";

    std::cout << "Decompressing to " << outputFile << "...\n";
    decompressToFile(compressedData, outputFile);
    std::cout << "Done.\n";

    return 0;
}
