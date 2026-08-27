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

    SaveData data;
    if (got == SAVE_BUFFER_SIZE && deserializeSave(buffer, got, data)) {
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
