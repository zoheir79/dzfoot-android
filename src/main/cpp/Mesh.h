#pragma once
#include <GLES3/gl3.h>
#include <vector>

struct Vertex {
    float pos[3];
    float normal[3];
    float uv[2];
};

class Mesh {
public:
    void loadCube(float size);
    void loadSphere(float radius, int stacks, int slices);
    void draw() const;
    void destroy();

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLsizei count_ = 0;
};
