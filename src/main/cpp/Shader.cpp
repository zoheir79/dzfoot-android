#include "Shader.h"
#include <android/log.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Shader", __VA_ARGS__)

GLuint Shader::compile(const char* vertSrc, const char* fragSrc) {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertSrc, nullptr);
    glCompileShader(vs);
    GLint ok;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(vs, 512, nullptr, log); LOGE("VS: %s", log); }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragSrc, nullptr);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(fs, 512, nullptr, log); LOGE("FS: %s", log); }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { char log[512]; glGetProgramInfoLog(prog, 512, nullptr, log); LOGE("Link: %s", log); }

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

void Shader::destroy(GLuint program) { glDeleteProgram(program); }
 
