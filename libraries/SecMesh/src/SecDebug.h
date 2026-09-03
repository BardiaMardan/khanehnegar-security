// =============================================================================
//  SecDebug.h — temporary serial tracing. Delete this file and grep for DBG(
// =============================================================================
#pragma once
#include <Arduino.h>

#define SEC_DEBUG 1

#if SEC_DEBUG
  #define DBG(fmt, ...) Serial.printf("====DEBUG==== " fmt "\n", ##__VA_ARGS__)
#else
  #define DBG(fmt, ...) do {} while (0)
#endif
