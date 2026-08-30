#pragma once

#define PIN_RFID_SCK   14
#define PIN_RFID_MOSI  13
#define PIN_RFID_MISO  35
#define PIN_RFID_CS    27
#define PIN_RFID_RST   26

#define PIN_BUZZER     21
#define PIN_TAMPER     25
#define PIN_TOUCHPAD   32

#define GSM_RX         16
#define GSM_TX         17

#define PIN_LENGTH        4
#define DEFAULT_PIN      "1234"
#define DEFAULT_DURESS   "1235"
#define MAX_PIN_ATTEMPTS  5

#define ENTRY_DELAY_MS         20000UL
#define EXIT_DELAY_MS          15000UL
#define ALARM_DURATION_MS     180000UL
#define UNKNOWN_CARD_WINDOW_MS 60000UL
#define UNKNOWN_CARD_LIMIT      3

#define RISK_ALARM_THRESHOLD  70
#define RISK_DECAY_PER_SEC     3
#define RISK_MAX             150

#define LOG_CAPACITY   24
