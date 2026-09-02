// =============================================================================
//  fingers.h — the fingerprint roster
//
//  Which template ids belong to enrolled people, and what to call them. Backed by NVS
//  and mirrored to the card; a missing card changes none of these answers. Implemented
//  in fingers.cpp.
// =============================================================================

#pragma once
#include <Arduino.h>

namespace Fingers {
void begin();

// ========== [Queries] ==========
// known() is the security-relevant one: false means "not a credential". idAt/labelAt are
// positional, for callers that iterate 0..count()-1.
int      count();
bool     known(uint16_t id);
String   label(uint16_t id);
uint16_t idAt(int i);
String   labelAt(int i);

// ========== [Mutation] ==========
// Each of these persists before returning. None of them touch the sensor's templates.
bool     add(uint16_t id, const String &label);
bool     rename(uint16_t id, const String &label);
bool     remove(uint16_t id);
void     clear();

// ========== [Ids and export] ==========
// nextFreeId() returns 0 when the roster is full. asIni() is the card mirror's format,
// and the same format begin() can import from on a hub with empty NVS.
uint16_t nextFreeId();
String   asIni();
}
