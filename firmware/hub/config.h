// =============================================================================
//  config.h — every tunable in the system, in one file
// =============================================================================

#pragma once

// ========== [Fingerprint sensor — UART2] ==========
// FP_TOUCH_GATE 0 polls instead of trusting the touch pin: an unwired pin reads low and
// silently disabled the whole sensor.
#define PIN_FP_RX      16
#define PIN_FP_TX      17
#define PIN_FP_TOUCH   22
#define FP_BAUD        57600
#define FP_TOUCH_GATE   0
#define FP_POLL_MS     120UL

// ========== [microSD — HSPI] ==========
// MISO is 32, not 35: pins 34-39 have no internal pull-up and SD MISO needs one.
#define PIN_SD_SCK     14
#define PIN_SD_MOSI    13
#define PIN_SD_MISO    32
#define PIN_SD_CS      27
#define SD_CLOCK_HZ    4000000

// ========== [Discrete peripherals] ==========
#define PIN_DHT        33
#define PIN_BUZZER     21
#define PIN_TAMPER     25

// ========== [Keypad code] ==========
#define PIN_LENGTH        4
#define DEFAULT_PIN      "1234"
#define MAX_PIN_ATTEMPTS  5

// ========== [Sessions and approvals] ==========
#define MANAGE_SESSION_MS    60000UL
#define APPROVAL_TIMEOUT_MS  30000UL

// ========== [Fingerprint roster] ==========
#define FP_MAX_TEMPLATES  120
#define FP_MIN_SCORE       50
#define FP_SLOTS           16
#define FP_LABEL_LEN       16

// ========== [Countdowns and windows] ==========
#define ENTRY_DELAY_MS         20000UL
#define EXIT_DELAY_MS          15000UL
#define ALARM_DURATION_MS     180000UL
#define UNKNOWN_FINGER_WINDOW_MS 60000UL
#define UNKNOWN_FINGER_LIMIT     3

// ========== [Risk fusion] ==========
#define RISK_ALARM_THRESHOLD  70
#define RISK_DECAY_PER_SEC     3
#define RISK_MAX             150
#define ALERT_COOLDOWN_MS  60000UL
#define ENABLE_HUB_TAMPER      0

// ========== [Journal] ==========
#define JOURNAL_CAPACITY   40
#define JOURNAL_FLUSH_MS   3000UL

// ========== [Housekeeping periods] ==========
#define CLIMATE_PERIOD_MS  30000UL
#define TIME_SYNC_MS     3600000UL
#define COMMAND_POLL_MS      5000UL
#define STANDBY_AFTER_MS    60000UL
#define UI_ROTATION            2

// ========== [Storage] ==========
#define NVS_NAMESPACE  "khaneh"

// ========== [Credits — shown on the splash screen] ==========
#define CREDIT_NAME  "Bardia Mardan"
#define CREDIT_ORG   "Kibord Hamrahaval"
#define CREDIT_URL1  "kibordevents.com"
#define CREDIT_URL2  "bardiamardan.xyz"
