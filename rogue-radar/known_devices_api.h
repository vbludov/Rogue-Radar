#pragma once
#include "known_device_types.h"
#include <lvgl.h>

static void createSavedDevices();
static void processKnownDevices();
static bool knownDevicesActive();
static void createNearbySignals();
static void createLearnKnownDevice();
static void createNearbySignalsForAssociation(rogue_radar::KnownRadio radio);
static void processNearbySignals();
static bool nearbySignalsActive();
static void knownUiSaveCandidate(const rogue_radar::KnownAddress &, rogue_radar::KnownRadio);
static void knownUiAssociateCandidate(const rogue_radar::KnownAddress &, rogue_radar::KnownRadio);
static void knownUiTrackCandidate(const rogue_radar::KnownAddress &, rogue_radar::KnownRadio);
static bool knownUiIsSaved(rogue_radar::KnownRadio, const rogue_radar::KnownAddress &, char *, size_t);
static void knownInstallSaveButton(lv_obj_t *, lv_group_t *, lv_obj_t *, lv_obj_t *,
                                  rogue_radar::KnownRadio, const char *, const char *, int8_t, uint8_t,
                                  uint32_t lastSeen = 0, uint8_t addressType = 255);
