#include "Mesh.h"
#include <cmath>

void Mesh::loadCube(float size) {
    destroy(); // Release any previously created buffers/arrays
    float h = size * 0.5f;
    Vertex verts[36];
    int i = 0;
    auto face = [&](float nx, float ny, float nz, float x1, float y1, float z1,
                    float x2, float y2, float z2, float x3, float y3, float z3,
                    float x4, float y4, float z4) {
        verts[i++] = {{x1,y1,z1}, {nx,ny,nz}, {0,0}};
        verts[i++] = {{x2,y2,z2}, {nx,ny,nz}, {1,0}};
        verts[i++] = {{x3,y3,z3}, {nx,ny,nz}, {1,1}};
        verts[i++] = {{x1,y1,z1}, {nx,ny,nz}, {0,0}};
        verts[i++] = {{x3,y3,z3}, {nx,ny,nz}, {1,1}};
        verts[i++] = {{x4,y4,z4}, {nx,ny,nz}, {0,1}};
    };
    // Front, Back, Left, Right, Top, Bottom
    face(0,0,1, -h,-h,h, h,-h,h, h,h,h, -h,h,h);
    face(0,0,-1, h,-h,-h, -h,-h,-h, -h,h,-h, h,h,-h);
    face(-1,0,0, -h,-h,-h, -h,-h,h, -h,h,h, -h,h,-h);
    face(1,0,0, h,-h,h, h,-h,-h, h,h,-h, h,h,h);
    face(0,1,0, -h,h,h, h,h,h, h,h,-h, -h,h,-h);
    face(0,-1,0, -h,-h,-h, h,-h,-h, h,-h,h, -h,-h,h);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    glEnableVertexAttribArray(2);
    count_ = 36;
}

void Mesh::loadSphere(float radius, int stacks, int slices) {
    destroy(); // Release any previously created buffers/arrays
    std::vector<Vertex> verts;
    for (int i = 0; i <= stacks; ++i) {
        float phi = M_PI * float(i) / float(stacks);
        for (int j = 0; j <= slices; ++j) {
            float theta = 2.0f * M_PI * float(j) / float(slices);
            float x = radius * std::sin(phi) * std::cos(theta);
            float y = radius * std::cos(phi);
            float z = radius * std::sin(phi) * std::sin(theta);
            Vertex v = {{x,y,z}, {x/radius,y/radius,z/radius}, {float(j)/slices, float(i)/stacks}};
            verts.push_back(v);
        }
    }
    std::vector<Vertex> triVerts;
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            int a = i * (slices + 1) + j;
            int b = a + slices + 1;
            triVerts.push_back(verts[a]); triVerts.push_back(verts[b]); triVerts.push_back(verts[a + 1]);
            triVerts.push_back(verts[b]); triVerts.push_back(verts[b + 1]); triVerts.push_back(verts[a + 1]);
        }
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, triVerts.size() * sizeof(Vertex), triVerts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    glEnableVertexAttribArray(2);
    count_ = static_cast<GLsizei>(triVerts.size());
}

void Mesh::draw() const {
    if (vao_) {
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, count_);
    }
}

void Mesh::destroy() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    vbo_ = vao_ = 0;
    count_ = 0;
}

// ─── SkinnedMesh ───────────────────────────────────────────────────

void SkinnedMesh::upload(const std::vector<SkinnedVertex>& verts, const std::vector<uint16_t>& indices) {
    destroy();
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ibo_);

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(SkinnedVertex), verts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint16_t), indices.data(), GL_STATIC_DRAW);

    // pos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex), (void*)offsetof(SkinnedVertex, pos));
    glEnableVertexAttribArray(0);
    // normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex), (void*)offsetof(SkinnedVertex, normal));
    glEnableVertexAttribArray(1);
    // uv
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex), (void*)offsetof(SkinnedVertex, uv));
    glEnableVertexAttribArray(2);
    // bone indices
    glVertexAttribIPointer(3, 4, GL_UNSIGNED_BYTE, sizeof(SkinnedVertex), (void*)offsetof(SkinnedVertex, boneIndices));
    glEnableVertexAttribArray(3);
    // bone weights
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(SkinnedVertex), (void*)offsetof(SkinnedVertex, boneWeights));
    glEnableVertexAttribArray(4);

    count_ = static_cast<GLsizei>(indices.size());
}

void SkinnedMesh::draw() const {
    if (vao_ && count_ > 0) {
        glBindVertexArray(vao_);
        glDrawElements(GL_TRIANGLES, count_, GL_UNSIGNED_SHORT, nullptr);
    }
}

void SkinnedMesh::destroy() {
    if (ibo_) glDeleteBuffers(1, &ibo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    ibo_ = vbo_ = vao_ = 0;
    count_ = 0;
}
