// =============================================================================
//  config.h — every tunable in the system, in one file
//
//  Pin assignments and behavioural constants for the hub, and nothing else: no
//  logic, no state, no includes. This file exists so that changing a delay, a
//  threshold or a pin never requires reading a state machine. Values fixed by the
//  hardware say so; values that look arbitrary carry the reason. Display and touch
//  pins are deliberately absent — they live in TFT_eSPI/User_Setup.h and must not
//  be duplicated here. Durations carry the UL suffix because they are compared
//  against millis(), which is uint32_t.
// =============================================================================

#pragma once

// ========== [Fingerprint sensor — UART2] ==========
// R308 on the second hardware UART. Logic is 3.3 V in both directions; only the
// module's supply needs 5 V. RX/TX are named from the ESP32's point of view.
#define PIN_FP_RX      16
#define PIN_FP_TX      17
#define PIN_FP_TOUCH   22
#define FP_BAUD        57600
// 0 because the module's finger-detect wire (connector pin 6) is not attached on
// this build. At 0 both `#if FP_TOUCH_GATE` blocks in fp.cpp are removed before
// the compiler sees them, so GPIO22 is never claimed by pinMode and never read —
// a runtime `if` would still do both. Set to 1 only once pin 6 is actually wired.
#define FP_TOUCH_GATE   0
// Idle poll interval. Faster spends UART time on nothing; slower feels laggy to a
// finger already on the platen.
#define FP_POLL_MS     120UL

// ========== [microSD — HSPI] ==========
// The second SPI bus, freed by dropping the MFRC522. Deliberately not the display
// bus: the panel runs at 27 MHz and clock switching made card access unreliable.
#define PIN_SD_SCK     14
#define PIN_SD_MOSI    13
// 32, not the 35 the card reader used. GPIO34–39 are input-only and have no
// internal pull-up, and an SD card's DO line needs one during initialisation;
// on 35 the card mounted intermittently. Input-only is fine for MISO electrically,
// which is why the wrong pin looked right on paper.
#define PIN_SD_MISO    32
#define PIN_SD_CS      27
// A ceiling, not the operating speed. Store::begin walks 400 kHz, 1 MHz, 4 MHz and
// then this value, so a marginal card or long jumpers still mount, just slower.
#define SD_CLOCK_HZ    4000000

// ========== [Discrete peripherals] ==========
// DHT11 is a one-wire digital protocol, not analog — GPIO33 is an ADC1 pin being
// used as plain I/O. Any future *analog* sensor (LDR tripwire, battery divider)
// must land on ADC1, which after this file leaves 34, 36 and 39. ADC2 shares
// hardware with the radio: analogRead on an ADC2 pin returns garbage or blocks for
// as long as WiFi or ESP-NOW is up, and ESP-NOW is always up here.
#define PIN_DHT        33
#define PIN_BUZZER     21
// Normally-closed microswitch to GND, read with an internal pull-up in hub.ino.
// HIGH therefore means the case is open *or* the loop has been cut — both are the
// same event as far as the hub is concerned.
#define PIN_TAMPER     25

// ========== [Keypad code] ==========
// DEFAULT_PIN is written exactly once, on a hub whose NVS holds no digest, and is
// never consulted again — the stored value is a salted hash, not this string.
#define PIN_LENGTH        4
#define DEFAULT_PIN      "1234"
// Wrong codes needed to latch lockdown. The counter lives in RAM and resets on a
// reboot; the latch it produces does not, because that one is written to NVS.
#define MAX_PIN_ATTEMPTS  5

// ========== [Sessions and approvals] ==========
// The manage-tab session is a sliding window, not a fixed one: every management
// action calls Access::keepAlive() and pushes the expiry out again, so a slow
// enrolment cannot time out halfway through.
#define MANAGE_SESSION_MS    60000UL
// An action requested from the web that nobody answers on the terminal is refused,
// not left pending. Sec::tick enforces this.
#define APPROVAL_TIMEOUT_MS  30000UL

// ========== [Fingerprint roster] ==========
// The sensor can store 120 templates, but the hub only tracks 16 labelled slots in
// NVS, so FP_SLOTS is the real limit on enrolments. FP_MAX_TEMPLATES only bounds
// the id search in Fingers::nextFreeId().
#define FP_MAX_TEMPLATES  120
// Confidence floor. The sensor reports a match with a score; anything under this is
// treated as no match rather than a weak yes.
#define FP_MIN_SCORE       50
#define FP_SLOTS           16
// Includes the null terminator, so 15 visible characters.
#define FP_LABEL_LEN       16

// ========== [Countdowns and windows] ==========
// AWAY plus a door opening starts this countdown instead of the siren, so the owner
// can reach the keypad. Expiry raises a loud alarm.
#define ENTRY_DELAY_MS         20000UL
// ARM pressed to actually armed. Risk is zeroed at the start of it, since walking
// out of your own front door produces exactly the evidence an intruder would.
#define EXIT_DELAY_MS          15000UL
// Siren runtime. On expiry the hub returns to AWAY, still armed — it does not
// disarm itself, because the alarm timing out is not an authenticated event.
#define ALARM_DURATION_MS     180000UL
// Unknown fingers are counted inside a sliding window, not forever: a stranger
// trying three times in a minute is an attack, three misreads across a week is not.
#define UNKNOWN_FINGER_WINDOW_MS 60000UL
#define UNKNOWN_FINGER_LIMIT     3

// ========== [Risk fusion] ==========
// See state.cpp. Two accumulators — all evidence and loud evidence only — are
// compared against this same threshold; the second one decides siren versus silent.
#define RISK_ALARM_THRESHOLD  70
// Evidence must expire. Without decay the accumulator is a lifetime total, and
// ordinary life — a touch here, motion there over an afternoon — eventually sums
// past the threshold and alarms on nothing. At 3/s a lone EV_TOUCH (25) is
// forgotten in about nine seconds, while two real events seconds apart still stack.
#define RISK_DECAY_PER_SEC     3
// Ceiling on both accumulators, so a burst of events cannot bank minutes of
// pre-charged risk that keeps re-triggering after it decays below the threshold.
#define RISK_MAX             150

// ========== [Journal] ==========
// Entries in the NVS ring. The whole blob is rewritten as one unit, so this is also
// the write size.
#define JOURNAL_CAPACITY   40
// Coalescing window for those writes. Flushing on every append would put a flash
// erase behind each event, and an alarm burst is exactly when many arrive at once.
#define JOURNAL_FLUSH_MS   3000UL

// ========== [Housekeeping periods] ==========
#define CLIMATE_PERIOD_MS  30000UL
#define TIME_SYNC_MS     3600000UL
#define COMMAND_POLL_MS      5000UL
#define STANDBY_AFTER_MS    60000UL
// Landscape. The stored touch calibration is keyed by this number ("tcal2"), so
// changing it forces a fresh corner calibration on the next boot.
#define UI_ROTATION            2

// ========== [Storage] ==========
// One NVS namespace holds everything persistent: the PIN salt and digest, the lockdown
// latch, the fingerprint roster, the node table, the event ring and the touch
// calibration. Erasing it factory-resets all of them together. Note that access.cpp,
// fingers.cpp and journal.cpp use this macro, while nodes.cpp and ui.cpp spell the same
// string as a literal — changing the value here does not move those two.
#define NVS_NAMESPACE  "khaneh"

// ========== [Credits — shown on the splash screen] ==========
#define CREDIT_NAME  "Bardia Mardan"
#define CREDIT_ORG   "Kibord Hamrahaval"
#define CREDIT_URL1  "kibordevents.com"
#define CREDIT_URL2  "bardiamardan.xyz"
