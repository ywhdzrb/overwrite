// 着色器编译器实现
// 负责将GLSL着色器编译为SPIR-V字节码
#include "core/shader_compiler.h"
#include <fstream>
#include <iostream>
#include <cstdlib>

namespace owengine {

// 编译着色器
// 读取GLSL源码并编译为SPIR-V
std::vector<char> ShaderCompiler::compileShader(const std::string& sourceFile, const std::string& stage) {
    std::string outputFile = sourceFile + ".spv";
    
    if (!compileToSpirV(sourceFile, outputFile, stage)) {
        throw std::runtime_error("Failed to compile shader: " + sourceFile);
    }
    
    std::ifstream file(outputFile, std::ios::ate | std::ios::binary);
    
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open compiled shader file: " + outputFile);
    }
    
    size_t fileSize = (size_t) file.tellg();
    std::vector<char> buffer(fileSize);
    
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    
    return buffer;
}

bool ShaderCompiler::compileToSpirV(const std::string& sourceFile, const std::string& outputFile, const std::string& stage) {
    std::string command = "glslc " + sourceFile + " -o " + outputFile + " -fshader-stage=" + stage;
    
    int result = std::system(command.c_str());
    
    if (result != 0) {
        std::cerr << "Shader compilation failed with code: " << result << std::endl;
        return false;
    }
    
    return true;
}

} // namespace owengine