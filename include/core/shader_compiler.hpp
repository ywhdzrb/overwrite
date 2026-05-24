#pragma once

#include <string>
#include <vector>

namespace owengine {

class ShaderCompiler {
public:
    static std::vector<char> compileShader(const std::string& sourceFile, const std::string& stage);
    static bool compileToSpirV(const std::string& sourceFile, const std::string& outputFile, const std::string& stage);
};

} // namespace owengine
