// =============================================================================
//  nodes.h — the roster of adopted modules, as seen from outside
// =============================================================================

#pragma once
#include <Arduino.h>
#include <SecMesh.h>
#include "config.h"

struct NodeRec {
  uint8_t  mac[6];
  uint8_t  kind;
  bool     used;
  bool     encrypted;
  uint32_t lastSeen;
  uint32_t lastCounter;
  uint16_t supplyMv;
  uint8_t  flags;
  bool     dead;
  uint8_t  lastEvent;
};

namespace Nodes {
void     begin();
NodeRec *find(const uint8_t mac[6]);
NodeRec *adopt(const uint8_t mac[6], uint8_t kind, bool encrypted, bool &fresh);
bool     acceptCounter(NodeRec *n, uint32_t counter);
void     resetCounter(NodeRec *n);
void     touchSeen(NodeRec *n, const SecFrame &f);
void     tick();
int      capacity();
NodeRec *at(int i);
int      liveCount();
int      deadCount();
void     setPairingMode(bool on);
bool     pairingMode();
void     save();
void     load();
void     forgetAll();

extern NodeRec *justDied;
}
