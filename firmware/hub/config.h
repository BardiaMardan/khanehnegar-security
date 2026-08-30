#pragma once

#define PIN_FP_RX      16
#define PIN_FP_TX      17
#define PIN_FP_TOUCH   22
#define FP_BAUD        57600

#define PIN_SD_SCK     14
#define PIN_SD_MOSI    13
#define PIN_SD_MISO    35
#define PIN_SD_CS      27

#define PIN_DHT        33
#define PIN_BUZZER     21
#define PIN_TAMPER     25

#define PIN_LENGTH        4
#define DEFAULT_PIN      "1234"
#define DEFAULT_DURESS   "1235"
#define MAX_PIN_ATTEMPTS  5

#define FP_MAX_TEMPLATES  120
#define FP_MIN_SCORE       50

#define ENTRY_DELAY_MS         20000UL
#define EXIT_DELAY_MS          15000UL
#define ALARM_DURATION_MS     180000UL
#define UNKNOWN_FINGER_WINDOW_MS 60000UL
#define UNKNOWN_FINGER_LIMIT     3

#define RISK_ALARM_THRESHOLD  70
#define RISK_DECAY_PER_SEC     3
#define RISK_MAX             150

#define LOG_CAPACITY   24

#define CLIMATE_PERIOD_MS  30000UL
#define TIME_SYNC_MS     3600000UL
