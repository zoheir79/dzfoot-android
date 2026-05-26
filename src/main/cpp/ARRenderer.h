#pragma once
#include "ARManager.h"
#include "Mesh.h"
#include <GLES3/gl3.h>

class ARRenderer {
public:
    void init();
    void destroy();

    void drawCameraBackground(ARManager& ar);
    void renderGameOnMarker(ARManager& ar, const float* playerPositions, int numPlayers);

private:
    GLuint cameraShader_ = 0;
    GLuint gameShader_   = 0;
    GLuint quadVbo_      = 0;
    GLuint uvVbo_        = 0;

    Mesh playerMesh_;
    Mesh ballMesh_;
    Mesh pitchMesh_;

    static constexpr float quadPositions_[8] = {
        -1.0f, -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,  1.0f,
    };
};
