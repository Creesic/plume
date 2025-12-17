//
// plume
//
// Copyright (c) 2024 renderbag and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file for details.
//

// Combines two Metal RT shader libraries (visible functions + dispatch kernel)
// into a single blob with a header for easy loading.
//
// Format:
//   [4 bytes] Magic: "PLRT" (Plume Ray Tracing)
//   [4 bytes] Version: 1
//   [4 bytes] Functions metallib size (little-endian)
//   [4 bytes] Dispatch metallib size (little-endian)
//   [N bytes] Functions metallib data
//   [M bytes] Dispatch metallib data

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

static const char MAGIC[4] = {'P', 'L', 'R', 'T'};
static const uint32_t VERSION = 1;

std::vector<char> read_file(const char* path) {
    std::ifstream input_file{path, std::ios::binary};
    std::vector<char> ret{};

    if (!input_file.good()) {
        return ret;
    }

    input_file.seekg(0, std::ios::end);
    ret.resize(input_file.tellg());

    input_file.seekg(0, std::ios::beg);
    input_file.read(ret.data(), ret.size());

    return ret;
}

void create_parent_if_needed(const char* path) {
    std::filesystem::path parent_path = std::filesystem::path{path}.parent_path();
    if (!parent_path.empty()) {
        std::filesystem::create_directories(parent_path);
    }
}

void write_uint32_le(std::ofstream& out, uint32_t value) {
    char bytes[4];
    bytes[0] = static_cast<char>(value & 0xFF);
    bytes[1] = static_cast<char>((value >> 8) & 0xFF);
    bytes[2] = static_cast<char>((value >> 16) & 0xFF);
    bytes[3] = static_cast<char>((value >> 24) & 0xFF);
    out.write(bytes, 4);
}

int main(int argc, const char** argv) {
    if (argc != 4) {
        printf("Usage: %s <functions.metallib> <dispatch.metallib> <output.metallib>\n", argv[0]);
        printf("\nCombines two Metal RT shader libraries into a single blob.\n");
        return EXIT_FAILURE;
    }

    const char* functions_path = argv[1];
    const char* dispatch_path = argv[2];
    const char* output_path = argv[3];

    // Read both input files
    std::vector<char> functions_data = read_file(functions_path);
    if (functions_data.empty()) {
        fprintf(stderr, "Failed to read functions metallib: %s\n", functions_path);
        return EXIT_FAILURE;
    }

    std::vector<char> dispatch_data = read_file(dispatch_path);
    if (dispatch_data.empty()) {
        fprintf(stderr, "Failed to read dispatch metallib: %s\n", dispatch_path);
        return EXIT_FAILURE;
    }

    // Create output directory if needed
    create_parent_if_needed(output_path);

    // Write combined output
    std::ofstream output{output_path, std::ios::binary};
    if (!output.good()) {
        fprintf(stderr, "Failed to create output file: %s\n", output_path);
        return EXIT_FAILURE;
    }

    // Write header
    output.write(MAGIC, 4);
    write_uint32_le(output, VERSION);
    write_uint32_le(output, static_cast<uint32_t>(functions_data.size()));
    write_uint32_le(output, static_cast<uint32_t>(dispatch_data.size()));

    // Write data
    output.write(functions_data.data(), functions_data.size());
    output.write(dispatch_data.data(), dispatch_data.size());

    printf("Combined RT metallib: %zu + %zu = %zu bytes\n",
           functions_data.size(), dispatch_data.size(),
           16 + functions_data.size() + dispatch_data.size());

    return EXIT_SUCCESS;
}
