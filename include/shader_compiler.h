#ifndef SHADER_COMPILER_H
#define SHADER_COMPILER_H

#include <string>
#include <vector>

namespace owengine {

class ShaderCompiler {
public:
    static std::vector<char> compileShader(const std::string& sourceFile, const std::string& stage);
    static bool compileToSpirV(const std::string& sourceFile, const std::string& outputFile, const std::string& stage);
};

} // namespace owengine

#endif // SHADER_COMPILER_H