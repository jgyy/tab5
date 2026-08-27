#include <M5Unified.h>
#include "math3d.h"
#include "mesh.h"
#include "framebuffer.h"
#include "rasterizer.h"

namespace {
constexpr int kRenderSize = 240; // offscreen 3D viewport, square; tune based on measured FPS below

Mesh gBaseMesh;
RealmVisual gRealmVisual;
Framebuffer* gFramebuffer = nullptr;
M5Canvas* gCanvas = nullptr;
float gRotation = 0.0f;
uint32_t gFrameCount = 0;
uint32_t gFpsWindowStartMs = 0;
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    delay(200);
    Serial.println("[BOOT] Rendering spike starting");

    M5.Display.fillScreen(TFT_BLACK);

    gBaseMesh = makeIcosahedron();
    gRealmVisual = growForRealm(gBaseMesh, 0);

    gFramebuffer = new Framebuffer(kRenderSize, kRenderSize);
    gCanvas = new M5Canvas(&M5.Display);
    gCanvas->createSprite(kRenderSize, kRenderSize);

    gFpsWindowStartMs = millis();
    Serial.println("[BOOT] Rendering spike ready");
}

void loop() {
    M5.update();

    RasterParams params;
    params.transform = Mat4::rotationY(gRotation);
    params.cameraDistance = 4.0f;
    params.focalLength = 1.6f;
    params.lightDir = Vec3{0.4f, 0.6f, -1.0f}.normalized();
    params.viewDir = Vec3{0, 0, -1};
    params.baseColor = gRealmVisual.baseColor;
    params.rimColor = gRealmVisual.rimColor;

    gFramebuffer->clear(RGB{15, 15, 25});
    rasterizeMesh(gRealmVisual.mesh, params, *gFramebuffer);

    for (int y = 0; y < kRenderSize; ++y) {
        for (int x = 0; x < kRenderSize; ++x) {
            RGB p = gFramebuffer->getPixel(x, y);
            gCanvas->drawPixel(x, y, gCanvas->color565(p.r, p.g, p.b));
        }
    }
    gCanvas->pushSprite((M5.Display.width() - kRenderSize) / 2,
                         (M5.Display.height() - kRenderSize) / 2);

    gRotation += 0.02f;
    gFrameCount++;

    uint32_t now = millis();
    if (now - gFpsWindowStartMs >= 1000) {
        Serial.printf("[FPS] %u frames/sec\n", gFrameCount);
        gFrameCount = 0;
        gFpsWindowStartMs = now;
    }
}
