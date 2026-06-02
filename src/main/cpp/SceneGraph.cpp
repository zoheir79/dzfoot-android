#include "SceneGraph.h"
#include <cstring>
#include <cmath>

void Transform::quatToMat4(const float* q, float* m) {
    // COLUMN-MAJOR layout (index = col*4 + row), consistent with mat4Mul and
    // composeTRS. Previously this was ROW-major, which transposed (=inverted)
    // every rotation and silently broke the player body yaw (modelRot).
    float xx = q[0]*q[0], yy = q[1]*q[1], zz = q[2]*q[2];
    float xy = q[0]*q[1], xz = q[0]*q[2], yz = q[1]*q[2];
    float wx = q[3]*q[0], wy = q[3]*q[1], wz = q[3]*q[2];
    m[0] = 1 - 2*(yy+zz); m[1] = 2*(xy+wz);   m[2] = 2*(xz-wy);   m[3] = 0;
    m[4] = 2*(xy-wz);   m[5] = 1 - 2*(xx+zz); m[6] = 2*(yz+wx);   m[7] = 0;
    m[8] = 2*(xz+wy);   m[9] = 2*(yz-wx);   m[10]= 1 - 2*(xx+yy); m[11]= 0;
    m[12]= 0;           m[13]= 0;           m[14]= 0;           m[15]= 1;
}

void Transform::mat4Mul(const float* a, const float* b, float* out) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            out[col*4+row] = a[0*4+row]*b[col*4+0]
                           + a[1*4+row]*b[col*4+1]
                           + a[2*4+row]*b[col*4+2]
                           + a[3*4+row]*b[col*4+3];
        }
    }
}

void Transform::toMatrix(float* out) const {
    float rM[16];
    quatToMat4(rotation, rM);
    for (int i = 0; i < 3; ++i) {
        rM[i*4+0] *= scale[0];
        rM[i*4+1] *= scale[1];
        rM[i*4+2] *= scale[2];
    }
    rM[12] = position[0];
    rM[13] = position[1];
    rM[14] = position[2];
    std::memcpy(out, rM, 16*sizeof(float));
}

void SceneNode::updateWorld(const float* parentWorld) {
    float localM[16];
    local.toMatrix(localM);
    if (parentWorld) {
        Transform::mat4Mul(parentWorld, localM, worldMatrix);
    } else {
        std::memcpy(worldMatrix, localM, 16*sizeof(float));
    }
}

int SceneGraph::findNode(const std::string& name) const {
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].name == name) return (int)i;
    }
    return -1;
}

int SceneGraph::addNode(const std::string& name, int parent) {
    SceneNode node;
    node.name = name;
    node.parentIndex = parent;
    int idx = (int)nodes.size();
    nodes.push_back(node);
    if (parent >= 0 && parent < (int)nodes.size()-1) {
        nodes[parent].children.push_back(idx);
    }
    return idx;
}

void SceneGraph::update() {
    for (auto& node : nodes) {
        if (node.parentIndex < 0) {
            node.updateWorld(nullptr);
        }
    }
    for (auto& node : nodes) {
        if (node.parentIndex >= 0) {
            node.updateWorld(nodes[node.parentIndex].worldMatrix);
        }
    }
}

void SceneGraph::drawAll(GLuint shader, const float* viewProj,
                         const float* boneMatrices, int numBones) const {
    GLint mvpLoc = glGetUniformLocation(shader, "u_ModelViewProj");
    GLint colLoc = glGetUniformLocation(shader, "u_Color");
    GLint boneLoc = glGetUniformLocation(shader, "u_BoneMatrices");

    for (const auto& node : nodes) {
        if (!node.visible) continue;

        float mvp[16];
        Transform::mat4Mul(viewProj, node.worldMatrix, mvp);
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);
        glUniform3f(colLoc, 1.0f, 1.0f, 1.0f); // white, or per-node later

        if (node.useSkinning && boneMatrices && numBones > 0) {
            glUniformMatrix4fv(boneLoc, numBones, GL_FALSE, boneMatrices);
            node.skinnedMesh.draw();
        } else if (node.staticMesh.hasData()) {
            node.staticMesh.draw();
        }
    }
}
