#pragma once
#include <GLES3/gl3.h>
#include <vector>

struct Vertex {
    float pos[3];
    float normal[3];
    float uv[2];
};

struct SkinnedVertex {
    float pos[3];
    float normal[3];
    float uv[2];
    uint8_t boneIndices[4];
    float boneWeights[4];
};

class Mesh {
public:
    void loadCube(float size);
    void loadSphere(float radius, int stacks, int slices);
    void upload(const std::vector<Vertex>& verts, const std::vector<uint16_t>& indices);
    void draw() const;
    void destroy();
    bool hasData() const { return vao_ != 0; }

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ibo_ = 0;
    GLsizei count_ = 0;
};

class SkinnedMesh {
public:
    void upload(const std::vector<SkinnedVertex>& verts, const std::vector<uint16_t>& indices);
    void draw() const;
    void destroy();
    bool hasData() const { return vao_ != 0; }

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ibo_ = 0;
    GLsizei count_ = 0;
};
