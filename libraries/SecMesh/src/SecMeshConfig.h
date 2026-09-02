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
// Dead at two and a half missed heartbeats. This is a security system: a node going quiet
// may mean it was destroyed, so the detection window is short at the cost of more traffic.
#define SECMESH_HEARTBEAT_MS    5000UL
#define SECMESH_DEAD_MS        13000UL
// While orphaned, retry the hub this often before hopping to the next channel.
#define SECMESH_SILENT_MS      20000UL

// ========== [Roster size] ==========
#define SECMESH_MAX_NODES      6

// ========== [Alarm burst] ==========
#define SECMESH_ALARM_REPEAT   3
#define SECMESH_ALARM_GAP_MS   40
