// =============================================================================
//  SecEvents.h — the on-air vocabulary: events, commands, modes, node kinds
// =============================================================================
#pragma once
#include <stdint.h>

// ========== [Node -> hub events] ==========
enum SecEvent : uint8_t {
  EV_NONE          = 0,
  EV_PAIR_REQUEST  = 1,
  EV_HEARTBEAT     = 2,
  EV_TAMPER_OPEN   = 3,
  EV_TAMPER_CLOSED = 4,
  EV_LOW_SUPPLY    = 5,
  EV_BOOT          = 6,

  EV_PRETOUCH      = 10,
  EV_TOUCH         = 11,
  EV_IMPACT        = 12,
  EV_AGITATION     = 13,
  EV_DOOR_OPEN     = 14,
  EV_DOOR_CLOSED   = 15,
  EV_GLASS         = 16,
  EV_LASER_CUT     = 17,
  EV_MOTION        = 18,
  EV_TILT          = 19,

  EV_RF_PROBE      = 20,
  EV_RF_DEAUTH     = 21,
  EV_RF_JAM        = 22,
  EV_NODE_LOST     = 23,

  EV_PANIC          = 30,
  EV_CODE_ATTACK    = 32,
  EV_ACCESS_OK      = 33,
  EV_ACCESS_DENIED  = 34,
  EV_ARMED          = 35,
  EV_DISARMED       = 36,
  EV_LOCKDOWN       = 37,
  EV_UNLOCKED       = 38,
  EV_PIN_CHANGED    = 39,
  EV_FINGER_ADDED   = 40,
  EV_FINGER_REMOVED = 41,
  EV_APPROVED       = 42,
  EV_DENIED         = 43,
  EV_FINGER_RENAMED = 44,
};

// ========== [Hub -> node commands] ==========
enum SecCommand : uint8_t {
  CMD_NONE        = 0,
  CMD_PAIR_ACCEPT = 1,
  CMD_STATE       = 2,
  CMD_PING        = 3,
  CMD_IDENTIFY    = 4,
};

// ========== [Arm modes] ==========
enum SecMode : uint8_t {
  MODE_DISARMED = 0,
  MODE_HOME     = 1,
  MODE_AWAY     = 2,
  MODE_ENTRY    = 3,
  MODE_ALARM    = 4,
  MODE_LOCKDOWN = 6,
};

// ========== [Node kinds] ==========
enum NodeKind : uint8_t {
  KIND_HUB    = 0,
  KIND_DOOR   = 1,
  KIND_WINDOW = 2,
  KIND_LASER  = 3,
  KIND_DECOY  = 4,
  KIND_PANEL  = 5,
};

// ========== [Names for the UI and the journal] ==========
inline const char *secEventName(uint8_t e) {
  switch (e) {
    case EV_PAIR_REQUEST:  return "pair request";
    case EV_HEARTBEAT:     return "heartbeat";
    case EV_TAMPER_OPEN:   return "CASE OPENED";
    case EV_TAMPER_CLOSED: return "case closed";
    case EV_LOW_SUPPLY:    return "low supply";
    case EV_BOOT:          return "node booted";
    case EV_PRETOUCH:      return "hand approaching";
    case EV_TOUCH:         return "handle touched";
    case EV_IMPACT:        return "IMPACT";
    case EV_AGITATION:     return "DOOR FORCED";
    case EV_DOOR_OPEN:     return "opened";
    case EV_DOOR_CLOSED:   return "closed";
    case EV_GLASS:         return "GLASS BREAK";
    case EV_LASER_CUT:     return "beam cut";
    case EV_MOTION:        return "motion";
    case EV_TILT:          return "DOOR MOVEMENT";
    case EV_RF_PROBE:      return "decoy probed";
    case EV_RF_DEAUTH:     return "DEAUTH FLOOD";
    case EV_RF_JAM:        return "RF JAMMING";
    case EV_NODE_LOST:     return "MODULE LOST";
    case EV_PANIC:          return "PANIC";
    case EV_CODE_ATTACK:    return "CODE ATTACK";
    case EV_ACCESS_OK:      return "unlocked";
    case EV_ACCESS_DENIED:  return "denied";
    case EV_ARMED:          return "armed";
    case EV_DISARMED:       return "disarmed";
    case EV_LOCKDOWN:       return "LOCKDOWN";
    case EV_UNLOCKED:       return "lockdown cleared";
    case EV_PIN_CHANGED:    return "code changed";
    case EV_FINGER_ADDED:   return "finger added";
    case EV_FINGER_REMOVED: return "finger removed";
    case EV_FINGER_RENAMED: return "finger renamed";
    case EV_APPROVED:       return "approved";
    case EV_DENIED:         return "refused";
    default:                return "event";
  }
}

inline const char *secKindName(uint8_t k) {
  switch (k) {
    case KIND_HUB:    return "PANEL";
    case KIND_DOOR:   return "DOOR";
    case KIND_WINDOW: return "WINDOW";
    case KIND_LASER:  return "BEAM";
    case KIND_DECOY:  return "DECOY";
    case KIND_PANEL:  return "REMOTE";
    default:          return "NODE";
  }
}

inline const char *secModeName(uint8_t m) {
  switch (m) {
    case MODE_DISARMED: return "DISARMED";
    case MODE_HOME:     return "HOME";
    case MODE_AWAY:     return "AWAY";
    case MODE_ENTRY:    return "ENTRY";
    case MODE_ALARM:    return "ALARM";
    case MODE_LOCKDOWN: return "LOCKED";
    default:            return "?";
  }
}
