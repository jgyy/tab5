#pragma once
#include "save.h"

// Loads the save from NVS. Returns defaultSaveData() if nothing is stored yet, or if
// what's stored fails deserializeSave()'s checksum/magic/version validation.
SaveData nvsLoadSave();

void nvsWriteSave(const SaveData& data);
