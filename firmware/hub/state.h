#pragma once
#include <Arduino.h>
#include "nodes.h"

struct LogEntry {
  uint32_t at;
  uint8_t  kind;
  uint8_t  code;
  uint16_t value;
  bool     silent;
};

namespace Sec {
void begin();
void tick();

uint8_t  mode();
uint8_t  displayMode();
bool     silentLatched();
int      risk();
uint32_t countdownMs();

void onNodeEvent(NodeRec *n, const SecFrame &f);
void onHubTamper(bool open);
void onDecoyProbe();
void onCard(const String &uid);

bool submitPin(const String &pin);
void requestArm(uint8_t m);
void panic();

void enrollCard(const String &uid, bool asDuress);
bool cardKnown(const String &uid);
int  enrolledCards();

int             logCount();
const LogEntry &logAt(int i);
const char     *message();
}
