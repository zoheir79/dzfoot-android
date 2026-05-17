#pragma once
#include <GLES3/gl3.h>
#include <string>

class Shader {
public:
    static GLuint compile(const char* vertSrc, const char* fragSrc);
    static void use(GLuint program);
    static void setMat4(GLuint program, const char* name, const float* mat);
    static void setInt(GLuint program, const char* name, int val);
    static void setVec3(GLuint program, const char* name, float x, float y, float z);
    static void destroy(GLuint program);
};
