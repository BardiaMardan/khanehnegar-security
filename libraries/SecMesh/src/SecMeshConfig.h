// =============================================================================
//  SecMeshConfig.h — site-wide constants: protocol identity, keys, timing
// =============================================================================
#pragma once

// ========== [Protocol identity] ==========
#define SECMESH_MAGIC       0x4B4E
#define SECMESH_VERSION     1
#define SECMESH_CHANNEL     1

// ========== [Keys] ==========
#define SECMESH_PMK         "KhanehNegarPMK01"
#define SECMESH_LMK         "KhanehNegarLMK01"

// ========== [Liveness timing] ==========
#define SECMESH_HEARTBEAT_MS   10000UL
#define SECMESH_DEAD_MS        30000UL

// ========== [Roster size] ==========
#define SECMESH_MAX_NODES      6

// ========== [Alarm burst] ==========
#define SECMESH_ALARM_REPEAT   3
#define SECMESH_ALARM_GAP_MS   40
