#include "trial_map.h"

namespace {
// 10 wide x 8 tall. '#' = outer boundary wall (type 1), 'X' = inner block wall (type 2),
// '.' = open floor (0). Matches the design spec's illustrative sketch, simplified to a single
// ring corridor around a solid inner block for easy hand-verification.
const char* kRows[8] = {
    "##########",
    "#........#",
    "#.XXXXXX.#",
    "#.XXXXXX.#",
    "#.XXXXXX.#",
    "#.XXXXXX.#",
    "#........#",
    "##########",
};

RaycastMap buildGrid() {
    RaycastMap grid;
    grid.width = 10;
    grid.height = 8;
    grid.cells.resize(80);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 10; ++x) {
            char c = kRows[y][x];
            int value = 0;
            if (c == '#') value = 1;
            else if (c == 'X') value = 2;
            grid.cells[static_cast<size_t>(y) * 10 + x] = value;
        }
    }
    return grid;
}
} // namespace

TrialMap makeSecretRealmMap() {
    TrialMap m;
    m.grid = buildGrid();

    // Clockwise loop: entrance -> across the top -> down the right column -> across the
    // bottom to the goal. Cell-center coordinates (e.g. x=1.5 is the center of column 1).
    m.route = {
        {1.5f, 1.5f}, // start / entrance
        {4.5f, 1.5f}, // enemy 1
        {8.5f, 1.5f}, // top-right corner
        {8.5f, 3.5f}, // enemy 2
        {8.5f, 6.5f}, // bottom-right corner
        {4.5f, 6.5f}, // enemy 3
        {1.5f, 6.5f}, // goal
    };

    m.enemies = {
        {4.5f, 1.5f, 30, 8},
        {8.5f, 3.5f, 50, 14},
        {4.5f, 6.5f, 80, 22},
    };

    return m;
}
