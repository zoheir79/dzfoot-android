#include "Shader.h"
#include <vector>
#include <cstring>
#include <android/log.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Shader", __VA_ARGS__)

GLuint Shader::compile(const char* vertSrc, const char* fragSrc) {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertSrc, nullptr);
    glCompileShader(vs);
    GLint ok;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(vs, 512, nullptr, log);
        LOGE("VS: %s", log);
        glDeleteShader(vs);
        return 0;
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragSrc, nullptr);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(fs, 512, nullptr, log);
        LOGE("FS: %s", log);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, 512, nullptr, log);
        LOGE("Link: %s", log);
        glDeleteProgram(prog);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

void Shader::use(GLuint program) { glUseProgram(program); }

void Shader::setMat4(GLuint program, const char* name, const float* mat) {
    glUniformMatrix4fv(glGetUniformLocation(program, name), 1, GL_FALSE, mat);
}

void Shader::setInt(GLuint program, const char* name, int val) {
    glUniform1i(glGetUniformLocation(program, name), val);
}

void Shader::setVec3(GLuint program, const char* name, float x, float y, float z) {
    glUniform3f(glGetUniformLocation(program, name), x, y, z);
}

void Shader::destroy(GLuint program) {
    if (program > 0) {
        glDeleteProgram(program);
    }
}

static bgfx::ShaderHandle createShaderFromString(const char* src, bool isFragment) {
    uint32_t srcLen = static_cast<uint32_t>(std::strlen(src));
    std::vector<uint8_t> mem;
    mem.push_back('S');
    mem.push_back('H');
    mem.push_back('D');
    mem.push_back(11); // standard bgfx shader version for GLES (11 on master branch)
    
    uint32_t hash = 0;
    mem.insert(mem.end(), reinterpret_cast<uint8_t*>(&hash), reinterpret_cast<uint8_t*>(&hash) + 4);
    
    uint16_t uniformCount = 0;
    mem.insert(mem.end(), reinterpret_cast<uint8_t*>(&uniformCount), reinterpret_cast<uint8_t*>(&uniformCount) + 2);
    
    mem.insert(mem.end(), reinterpret_cast<uint8_t*>(&srcLen), reinterpret_cast<uint8_t*>(&srcLen) + 4);
    mem.insert(mem.end(), src, src + srcLen);
    mem.push_back(0);
    
    const bgfx::Memory* bgfxMem = bgfx::copy(mem.data(), static_cast<uint32_t>(mem.size()));
    return bgfx::createShader(bgfxMem);
}

bgfx::ProgramHandle Shader::compileBgfx(const char* vertSrc, const char* fragSrc) {
    bgfx::ShaderHandle vsh = createShaderFromString(vertSrc, false);
    bgfx::ShaderHandle fsh = createShaderFromString(fragSrc, true);
    if (!bgfx::isValid(vsh) || !bgfx::isValid(fsh)) {
        LOGE("Failed to create bgfx vertex or fragment shader!");
        union { bgfx_program_handle_t c; bgfx::ProgramHandle cpp; } invalid = { BGFX_INVALID_HANDLE };
        return invalid.cpp;
    }
    bgfx::ProgramHandle prog = bgfx::createProgram(vsh, fsh, true);
    if (!bgfx::isValid(prog)) {
        LOGE("Failed to link bgfx program!");
    }
    return prog;
}

void Shader::destroyBgfx(bgfx::ProgramHandle program) {
    if (bgfx::isValid(program)) {
        bgfx::destroy(program);
    }
}
 
