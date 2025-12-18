//
// plume
//
// Copyright (c) 2024 renderbag and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file for details.
//
// Generates a Metal Shader Converter root signature JSON from DXC disassembly output.
// Parses the "Resource Bindings:" section to extract shader resource bindings.
//

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

struct Resource {
    std::string name;
    std::string type;      // cbuffer, texture, UAV, sampler
    std::string dim;       // NA, 2d, ras (raytracing acceleration structure), etc.
    std::string bindType;  // cb, t, u, s
    int reg = 0;
    int space = 0;
    int count = 1;
};

// Trim whitespace from both ends
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Split string by whitespace
std::vector<std::string> splitWhitespace(const std::string& s) {
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

// Parse HLSL bind like "cb0", "t0", "u0", "s0"
bool parseHlslBind(const std::string& bind, std::string& bindType, int& reg, int& space) {
    std::string bindPart = bind;
    std::string spacePart;
    const size_t comma = bind.find(',');
    if (comma != std::string::npos) {
        bindPart = bind.substr(0, comma);
        spacePart = bind.substr(comma + 1);
    }

    size_t i = 0;
    while (i < bindPart.size() && std::isalpha(static_cast<unsigned char>(bindPart[i]))) {
        i++;
    }
    if (i == 0 || i >= bindPart.size()) return false;

    bindType = bindPart.substr(0, i);
    reg = std::atoi(bindPart.substr(i).c_str());

    if (!spacePart.empty()) {
        spacePart = trim(spacePart);
        if (spacePart.rfind("space", 0) == 0) {
            space = std::atoi(spacePart.substr(5).c_str());
        }
    }
    return true;
}

std::vector<Resource> parseReflection(const std::string& content) {
    std::vector<Resource> resources;
    std::istringstream stream(content);
    std::string line;
    bool inBindings = false;
    
    while (std::getline(stream, line)) {
        line = trim(line);
        
        if (line.find("Resource Bindings:") != std::string::npos) {
            inBindings = true;
            continue;
        }
        
        if (!inBindings) continue;
        
        // Skip header lines
        if (line.find("; Name") != std::string::npos || 
            line.find("; ----") != std::string::npos) {
            continue;
        }
        
        // End of bindings section (non-comment line or empty)
        if (line.empty() || line[0] != ';') {
            break;
        }
        
        // Remove leading semicolon and trim
        line = trim(line.substr(1));
        if (line.empty()) continue;
        
        // Parse: Name Type Format Dim ID HLSLBind [Space] Count
        auto parts = splitWhitespace(line);
        if (parts.size() < 7) continue;
        
        Resource res;
        res.name = parts[0];
        res.type = parts[1];
        res.dim = parts[3];
        res.count = std::atoi(parts.back().c_str());
        size_t bindIndex = parts.size() - 2;
        if (bindIndex > 0 && parts[bindIndex].rfind("space", 0) == 0) {
            res.space = std::atoi(parts[bindIndex].substr(5).c_str());
            bindIndex--;
        }
        std::string hlslBind = parts[bindIndex];

        if (!parseHlslBind(hlslBind, res.bindType, res.reg, res.space)) {
            continue;
        }
        
        resources.push_back(res);
    }
    
    return resources;
}

// Sort by type priority: UAV (u), SRV (t), CBV (cb/b), Sampler (s)
int typePriority(const std::string& bindType) {
    if (bindType == "u") return 0;
    if (bindType == "t") return 1;
    if (bindType == "cb" || bindType == "b") return 2;
    if (bindType == "s") return 3;
    return 99;
}

void writeRootSignature(std::ofstream& out, const std::vector<Resource>& resources) {
    // Sort resources by type priority
    std::vector<Resource> sorted = resources;
    std::sort(sorted.begin(), sorted.end(), [](const Resource& a, const Resource& b) {
        int pa = typePriority(a.bindType);
        int pb = typePriority(b.bindType);
        if (pa != pb) return pa < pb;
        return a.reg < b.reg;
    });
    
    out << "{\n";
    out << "  \"version\": \"IRRootSignatureVersion_1_1\",\n";
    out << "  \"RootSignature\": {\n";
    out << "    \"Flags\": \"IRRootSignatureFlagNone\",\n";
    out << "    \"NumParameters\": " << sorted.size() << ",\n";
    out << "    \"Parameters\": [\n";
    
    for (size_t i = 0; i < sorted.size(); i++) {
        const Resource& res = sorted[i];
        
        if (res.bindType == "u") {
            // UAV -> descriptor table
            out << "      {\n";
            out << "        \"ParameterType\": \"IRRootParameterTypeDescriptorTable\",\n";
            out << "        \"ShaderVisibility\": \"IRShaderVisibilityAll\",\n";
            out << "        \"DescriptorTable\": {\n";
            out << "          \"NumDescriptorRanges\": 1,\n";
            out << "          \"DescriptorRanges\": [\n";
            out << "            {\n";
            out << "              \"RangeType\": \"IRDescriptorRangeTypeUAV\",\n";
            out << "              \"NumDescriptors\": " << res.count << ",\n";
            out << "              \"BaseShaderRegister\": " << res.reg << ",\n";
            out << "              \"RegisterSpace\": " << res.space << ",\n";
            out << "              \"OffsetInDescriptorsFromTableStart\": 0,\n";
            out << "              \"Flags\": \"IRDescriptorRangeFlagNone\"\n";
            out << "            }\n";
            out << "          ]\n";
            out << "        }\n";
            out << "      }";
        } else if (res.bindType == "t") {
            if (res.dim == "ras") {
                // Acceleration structure -> root SRV
                out << "      {\n";
                out << "        \"ParameterType\": \"IRRootParameterTypeSRV\",\n";
                out << "        \"ShaderVisibility\": \"IRShaderVisibilityAll\",\n";
                out << "        \"Descriptor\": {\n";
                out << "          \"ShaderRegister\": " << res.reg << ",\n";
                out << "          \"RegisterSpace\": " << res.space << ",\n";
                out << "          \"Flags\": \"IRRootDescriptorFlagNone\"\n";
                out << "        }\n";
                out << "      }";
            } else {
                // Regular texture -> descriptor table
                out << "      {\n";
                out << "        \"ParameterType\": \"IRRootParameterTypeDescriptorTable\",\n";
                out << "        \"ShaderVisibility\": \"IRShaderVisibilityAll\",\n";
                out << "        \"DescriptorTable\": {\n";
                out << "          \"NumDescriptorRanges\": 1,\n";
                out << "          \"DescriptorRanges\": [\n";
                out << "            {\n";
                out << "              \"RangeType\": \"IRDescriptorRangeTypeSRV\",\n";
                out << "              \"NumDescriptors\": " << res.count << ",\n";
                out << "              \"BaseShaderRegister\": " << res.reg << ",\n";
                out << "              \"RegisterSpace\": " << res.space << ",\n";
                out << "              \"OffsetInDescriptorsFromTableStart\": 0,\n";
                out << "              \"Flags\": \"IRDescriptorRangeFlagNone\"\n";
                out << "            }\n";
                out << "          ]\n";
                out << "        }\n";
                out << "      }";
            }
        } else if (res.bindType == "cb" || res.bindType == "b") {
            // Constant buffer -> root CBV
            out << "      {\n";
            out << "        \"ParameterType\": \"IRRootParameterTypeCBV\",\n";
            out << "        \"ShaderVisibility\": \"IRShaderVisibilityAll\",\n";
            out << "        \"Descriptor\": {\n";
            out << "          \"ShaderRegister\": " << res.reg << ",\n";
            out << "          \"RegisterSpace\": " << res.space << ",\n";
            out << "          \"Flags\": \"IRRootDescriptorFlagNone\"\n";
            out << "        }\n";
            out << "      }";
        } else if (res.bindType == "s") {
            // Sampler -> descriptor table
            out << "      {\n";
            out << "        \"ParameterType\": \"IRRootParameterTypeDescriptorTable\",\n";
            out << "        \"ShaderVisibility\": \"IRShaderVisibilityAll\",\n";
            out << "        \"DescriptorTable\": {\n";
            out << "          \"NumDescriptorRanges\": 1,\n";
            out << "          \"DescriptorRanges\": [\n";
            out << "            {\n";
            out << "              \"RangeType\": \"IRDescriptorRangeTypeSampler\",\n";
            out << "              \"NumDescriptors\": " << res.count << ",\n";
            out << "              \"BaseShaderRegister\": " << res.reg << ",\n";
            out << "              \"RegisterSpace\": " << res.space << ",\n";
            out << "              \"OffsetInDescriptorsFromTableStart\": 0,\n";
            out << "              \"Flags\": \"IRDescriptorRangeFlagNone\"\n";
            out << "            }\n";
            out << "          ]\n";
            out << "        }\n";
            out << "      }";
        }
        
        if (i < sorted.size() - 1) {
            out << ",";
        }
        out << "\n";
    }
    
    out << "    ],\n";
    out << "    \"NumStaticSamplers\": 0,\n";
    out << "    \"StaticSamplers\": []\n";
    out << "  }\n";
    out << "}\n";
}

int main(int argc, const char** argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input_reflection.txt> <output_root_signature.json>\n", argv[0]);
        fprintf(stderr, "\nGenerates Metal Shader Converter root signature JSON from DXC disassembly.\n");
        return EXIT_FAILURE;
    }
    
    const char* inputPath = argv[1];
    const char* outputPath = argv[2];
    
    // Read input file
    std::ifstream inputFile(inputPath);
    if (!inputFile) {
        fprintf(stderr, "Error: Cannot open input file: %s\n", inputPath);
        return EXIT_FAILURE;
    }
    
    std::stringstream buffer;
    buffer << inputFile.rdbuf();
    std::string content = buffer.str();
    inputFile.close();
    
    // Parse resources
    std::vector<Resource> resources = parseReflection(content);
    
    if (resources.empty()) {
        fprintf(stderr, "Error: No resource bindings found in input\n");
        return EXIT_FAILURE;
    }
    
    // Write output
    std::ofstream outputFile(outputPath);
    if (!outputFile) {
        fprintf(stderr, "Error: Cannot open output file: %s\n", outputPath);
        return EXIT_FAILURE;
    }
    
    writeRootSignature(outputFile, resources);
    outputFile.close();
    
    return EXIT_SUCCESS;
}
