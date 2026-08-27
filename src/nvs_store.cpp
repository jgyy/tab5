#include "nvs_store.h"
#include <Preferences.h>

namespace {
const char* kNamespace = "xianxia";
const char* kKey = "save";
}

SaveData nvsLoadSave() {
    Preferences prefs;
    prefs.begin(kNamespace, /*readOnly=*/true);
    uint8_t buffer[SAVE_BUFFER_SIZE];
    size_t got = prefs.getBytes(kKey, buffer, sizeof(buffer));
    prefs.end();

    // Deliberately NOT gated on got == SAVE_BUFFER_SIZE: a save written before schema v2
    // (brightness/volume) is a few bytes shorter than the current SAVE_BUFFER_SIZE, and
    // deserializeSave() itself is version-aware (it falls back to the older layout and
    // migrates). Gating on an exact size match here would reject that shorter-but-valid
    // legacy blob before deserializeSave ever gets a chance to migrate it, silently
    // resetting all progress instead.
    SaveData data;
    if (got > 0 && deserializeSave(buffer, got, data)) {
        return data;
    }
    return defaultSaveData();
}

void nvsWriteSave(const SaveData& data) {
    uint8_t buffer[SAVE_BUFFER_SIZE];
    size_t written = serializeSave(data, buffer, sizeof(buffer));

    Preferences prefs;
    prefs.begin(kNamespace, /*readOnly=*/false);
    prefs.putBytes(kKey, buffer, written);
    prefs.end();
}
