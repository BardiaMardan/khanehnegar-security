// =============================================================================
//  net.h — the hub's side of the link to the local server
// =============================================================================

#pragma once
#include <Arduino.h>

namespace Net {
// begin() starts the network task; tick() runs on the main loop and only applies
// whatever that task has already fetched. No call here performs HTTP inline.
void begin();
void tick();
bool online();
int  channel();

// ========== [Outbound, queued] ==========
// These enqueue and return immediately. A full queue drops the message rather than
// blocking the caller, because the alarm path must never wait on the network.
void alert(bool silent, const char *kind, const char *event, uint16_t value);
void decide(uint32_t id, bool allowed);
void publishRoster();
}
