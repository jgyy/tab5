#pragma once
#include "framebuffer.h"
#include "math3d.h"
#include "mesh.h"

struct RasterParams {
    Mat4 transform;       // model transform (e.g. rotationY for auto-spin)
    float cameraDistance; // added to every vertex's local Z to push the mesh in front of the camera
    float focalLength;
    Vec3 lightDir;        // normalized; direction FROM a surface TOWARD the light
    Vec3 viewDir;         // normalized; direction FROM a surface TOWARD the camera (rim term)
    RGB baseColor;
    RGB rimColor;
};

// Pipeline: rotate -> transform to camera space -> perspective-project -> backface cull
// -> depth-sort back-to-front (painter's algorithm) -> flat/Lambert + rim shade -> fill.
// Does NOT clear `fb` first — the caller clears with the desired background color.
void rasterizeMesh(const Mesh& mesh, const RasterParams& params, Framebuffer& fb);
