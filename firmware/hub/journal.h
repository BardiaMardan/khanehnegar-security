// =============================================================================
//  journal.h — the recent-event ring
//
//  A bounded, newest-first history that survives a reboot with no card in the slot.
//  Implemented in journal.cpp.
// =============================================================================

#pragma once
#include <Arduino.h>

// One event. Two timestamps on purpose: `epoch` is wall-clock and is 0 until the first
// server time sync, `at` is uptime from millis() and is zeroed for entries restored from
// NVS because a previous boot's millis() means nothing. `silent` records that the system
// saw this and deliberately made no sound.
struct LogEntry {
  uint32_t epoch;
  uint32_t at;
  uint8_t  kind;
  uint8_t  code;
  uint16_t value;
  uint8_t  silent;
};

namespace Journal {
void begin();
void tick();

// Appends to the ring and to the card's CSV. The ring reaches flash on the next
// throttled flush; the CSV line is written immediately.
void append(uint8_t kind, uint8_t code, uint16_t value, bool silent);

// at(0) is the newest entry, at(count()-1) the oldest. An out-of-range index returns a
// zeroed entry rather than failing, so callers can draw a fixed number of rows.
int             count();
const LogEntry &at(int i);
void            clear();
}
