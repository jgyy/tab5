#pragma once
#include <algorithm>
#include <vector>
#include "color.h"

struct Framebuffer {
    int width;
    int height;
    std::vector<RGB> pixels;

    Framebuffer(int w, int h) : width(w), height(h), pixels(static_cast<size_t>(w) * h) {}

    void clear(RGB color) {
        std::fill(pixels.begin(), pixels.end(), color);
    }

    void setPixel(int x, int y, RGB color) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        pixels[static_cast<size_t>(y) * width + x] = color;
    }

    RGB getPixel(int x, int y) const {
        return pixels[static_cast<size_t>(y) * width + x];
    }
};
