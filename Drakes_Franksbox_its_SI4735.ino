/* Drake's Franks Box

Dennis Liang
https://www.pasadenahosthunters.org

Public domain. See LICENSE and README.md.

What it does
  An SI4735 AM/FM receiver that keeps sweeping with the audio left open.
  That is the same idea as a shack-hack spirit box: do not stop on a
  station, and do not mute the noise between stations.

Hardware
  Arduino Uno and a SparkFun SI4735 shield.
  NEC FIPC8367 character VFD, 9600 baud, 20 columns, two lines.
  Audio amp enable on pin 3. High turns the amp on.
  Pin 13 is the radio SPI clock. This sketch does not use it as an LED.

  Two momentary (ON)-OFF-(ON) switches, center off, wired to ground.
  The pin mode is INPUT_PULLUP, so a throw reads LOW.
  Volume: A5 up, center, A4 down. The two throws cannot be on together.
  Band:   A3 AM, center, A2 FM. AM and FM cannot be on together.

How to use it
  Boot shows "ROM:" plus the compiler date (__DATE__), then "Hold VOL".
  Hold the volume switch either way during that splash to set scan delays.
  In the menu, volume changes the delay and the band switch picks FM or AM.
  Hold the band switch to save. Leave the menu alone and it cancels.
  While scanning, volume changes the level and a flick of the band switch
  restarts that band at 97.3 MHz or 520 kHz.

What is remembered
  EEPROM stores volume, band, and one scan delay per band.
  The live frequency is not stored. A sweep restarts at the bottom of the band.
*/

#include <Si4735.h>
#include <EEPROM.h>

// ---------------------------------------------------------------------------
// Pins
// ---------------------------------------------------------------------------

const int AMP_PIN = 3;
const int PIN_VOL_UP = A5;
const int PIN_VOL_DN = A4;
const int PIN_AM = A3;
const int PIN_FM = A2;

// ---------------------------------------------------------------------------
// VFD
//
// 0x0C clears and homes the cursor. 0x14 turns the cursor off.
// Brightness is 0xFF then 0x04, which this glass treats as about 25%.
// "520Khz" is 6 characters and leaves 14, so a line is 20 columns.
// The spare columns hold a bouncing "---->" / "<----" sweep.
// ---------------------------------------------------------------------------

const int LCD_COLS = 20;
const uint8_t VFD_CLEAR = 0x0C;
const uint8_t VFD_CURSOR_OFF = 0x14;

const unsigned long BUILD_DATE_MS = 2000;
const unsigned long SPLASH_MS = 3500;
const unsigned long STATUS_HOLD_MS = 900;
const unsigned long BOOT_STATUS_MS = 1200;

// ---------------------------------------------------------------------------
// Switches and menu
// ---------------------------------------------------------------------------

const unsigned long DEBOUNCE_MS = 30;
const unsigned long REPEAT_START_MS = 450;
const unsigned long REPEAT_MS = 150;
const unsigned long SAVE_HINT_MS = 700;
const unsigned long SAVE_HOLD_MS = 1500;
const unsigned long MENU_IDLE_MS = 30000;
const unsigned long RELEASE_WAIT_MS = 8000;
const unsigned long MENU_MESSAGE_MS = 700;
const unsigned long VOLUME_SAVE_MS = 500;

// ---------------------------------------------------------------------------
// Radio
//
// The SparkFun library tunes FM in steps of 10 kHz, so 9730 is 97.30 MHz
// and a step of 10 is 100 kHz. AM is tuned in kHz, so a step of 10 is one
// US AM channel. FM wraps 97.3-108.0 MHz. AM wraps 520-1710 kHz.
// Volume is property 0x4000 and runs from 0 to 63. radio.begin() always
// puts the library's private counter back at 5, so the sketch writes the
// chip volume itself after every begin().
// ---------------------------------------------------------------------------

const uint8_t VOLUME_MAX = 63;
const uint8_t VOLUME_DEFAULT = 5;
const int FM_START = 9730;
const int FM_TOP = 10800;
const int AM_START = 520;
const int AM_TOP = 1710;
const int TUNE_STEP = 10;

const uint16_t DELAY_MIN_MS = 100;
const uint16_t DELAY_MAX_MS = 2000;
const uint16_t DELAY_STEP_MS = 50;
const uint16_t DELAY_DEFAULT_MS = 200;

// ---------------------------------------------------------------------------
// EEPROM record. A bad magic byte or checksum falls back to defaults.
// ---------------------------------------------------------------------------

const uint8_t EEPROM_MAGIC = 0xD5;
const uint8_t EEPROM_VERSION = 1;

struct RadioSettings {
  uint8_t magic;
  uint8_t version;
  uint8_t volume;
  uint8_t band;
  uint16_t fmDelayMs;
  uint16_t amDelayMs;
  uint8_t checksum;
};

// One throw of a center-off switch. "down" means the throw is held.
struct Key {
  uint8_t pin;
  bool down;
  bool prevRead;
  unsigned long changeMs;
  unsigned long lastRepeatMs;
};

// Sweep comet drawn in the columns left after the frequency.
// Five characters when there is room ("---->"), three when the frequency
// is long ("-->"). It bounces off each end of that gap.
const int COMET_LONG = 5;
const int COMET_SHORT = 3;

Si4735 radio;
RadioSettings settings;
Key keyVolUp;
Key keyVolDn;
Key keyAm;
Key keyFm;

int tuneFreq = FM_START;
int sweepPos = 0;
int sweepDir = 1;
unsigned long lastTuneMs = 0;
unsigned long holdDisplayUntil = 0;
unsigned long lastVolumeMs = 0;
bool volumeNeedsSave = false;

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

uint8_t checksumOf(const RadioSettings &stored) {
  uint8_t sum = 0xA5;
  sum += stored.magic;
  sum += stored.version;
  sum += stored.volume;
  sum += stored.band;
  sum += (uint8_t)stored.fmDelayMs;
  sum += (uint8_t)(stored.fmDelayMs >> 8);
  sum += (uint8_t)stored.amDelayMs;
  sum += (uint8_t)(stored.amDelayMs >> 8);
  return sum;
}

void setDefaults() {
  settings.magic = EEPROM_MAGIC;
  settings.version = EEPROM_VERSION;
  settings.volume = VOLUME_DEFAULT;
  settings.band = FM;
  settings.fmDelayMs = DELAY_DEFAULT_MS;
  settings.amDelayMs = DELAY_DEFAULT_MS;
}

void saveSettings() {
  settings.magic = EEPROM_MAGIC;
  settings.version = EEPROM_VERSION;
  settings.checksum = checksumOf(settings);
  EEPROM.put(0, settings);
}

bool settingsValid() {
  if (settings.magic != EEPROM_MAGIC) return false;
  if (settings.version != EEPROM_VERSION) return false;
  if (settings.checksum != checksumOf(settings)) return false;
  if (settings.volume > VOLUME_MAX) return false;
  if (settings.band != AM && settings.band != FM) return false;
  if (settings.fmDelayMs < DELAY_MIN_MS || settings.fmDelayMs > DELAY_MAX_MS) return false;
  if (settings.amDelayMs < DELAY_MIN_MS || settings.amDelayMs > DELAY_MAX_MS) return false;
  return true;
}

void loadSettings() {
  EEPROM.get(0, settings);
  if (!settingsValid()) {
    setDefaults();
    saveSettings();
  }
}

unsigned long scanDelayMs() {
  if (settings.band == FM) return settings.fmDelayMs;
  return settings.amDelayMs;
}

// ---------------------------------------------------------------------------
// VFD text
// ---------------------------------------------------------------------------

void lcdClear() {
  Serial.write(VFD_CLEAR);
}

void showStatus() {
  lcdClear();
  if (settings.band == FM) Serial.print("FM vol ");
  else Serial.print("AM vol ");
  Serial.print(settings.volume);
}

void showVolume() {
  lcdClear();
  Serial.print("Vol ");
  Serial.print(settings.volume);
}

void showDelay(bool editFm) {
  lcdClear();
  if (editFm) Serial.print("FM ");
  else Serial.print("AM ");
  Serial.print(editFm ? settings.fmDelayMs : settings.amDelayMs);
  Serial.print("ms");
}

// Prints the tuned frequency and returns how many columns that used.
int printFrequency() {
  if (settings.band == FM) {
    Serial.print(tuneFreq / 100.0, 1);
    Serial.print("Mhz");
    return (tuneFreq >= 10000 ? 5 : 4) + 3;
  }
  Serial.print(tuneFreq);
  Serial.print("Khz");
  return (tuneFreq >= 1000 ? 4 : 3) + 3;
}

// ---------------------------------------------------------------------------
// Radio commands
// ---------------------------------------------------------------------------

// SET_PROPERTY for RX_VOLUME (0x4000). The last byte is the level, 0-63.
void writeVolume(uint8_t level) {
  char command[13];
  if (level > VOLUME_MAX) level = VOLUME_MAX;
  sprintf(command, "1200400000%02X", (unsigned)level);
  radio.sendCommand(command);
}

// The chip normally mutes a weak signal. These property writes leave the
// audio open, so the sweep stays audible between stations.
void disableSoftMute() {
  radio.sendCommand("120013020000");
  radio.sendCommand("1200130300FF");
  radio.sendCommand("120033020000");
  radio.sendCommand("120033030000");
  radio.sendCommand("12001106007F");
}

void startRadio(uint8_t band) {
  settings.band = band;
  tuneFreq = (band == FM) ? FM_START : AM_START;
  radio.begin(band);
  writeVolume(settings.volume);
  disableSoftMute();
  radio.tuneFrequency(tuneFreq);
}

void changeVolume(int direction) {
  int next = (int)settings.volume + direction;
  if (next < 0 || next > VOLUME_MAX) return;

  settings.volume = (uint8_t)next;
  writeVolume(settings.volume);
  volumeNeedsSave = true;
  lastVolumeMs = millis();
  showVolume();
  holdDisplayUntil = millis() + STATUS_HOLD_MS;
}

// ---------------------------------------------------------------------------
// Switches
// ---------------------------------------------------------------------------

void initKey(Key &key, uint8_t pin) {
  key.pin = pin;
  key.down = false;
  key.prevRead = false;
  key.changeMs = 0;
  key.lastRepeatMs = 0;
  pinMode(pin, INPUT_PULLUP);
}

// True once, when a throw is first held.
bool updateKey(Key &key) {
  bool reading = digitalRead(key.pin) == LOW;
  unsigned long now = millis();

  if (reading != key.prevRead) {
    key.prevRead = reading;
    key.changeMs = now;
  }

  if ((now - key.changeMs) >= DEBOUNCE_MS && key.down != reading) {
    key.down = reading;
    if (key.down) {
      key.lastRepeatMs = now;
      return true;
    }
  }
  return false;
}

// First repeat after the throw has been held, then a faster repeat.
bool keyRepeat(Key &key) {
  if (!key.down) return false;

  unsigned long now = millis();
  unsigned long held = now - key.changeMs;
  unsigned long interval = (held < REPEAT_START_MS) ? REPEAT_START_MS : REPEAT_MS;
  if (now - key.lastRepeatMs >= interval) {
    key.lastRepeatMs = now;
    return true;
  }
  return false;
}

void pollKeys() {
  updateKey(keyVolUp);
  updateKey(keyVolDn);
  updateKey(keyAm);
  updateKey(keyFm);
}

// Exactly one side of the band switch. Both pins low is not a real throw.
bool bandHeldOneSide() {
  return (keyAm.down || keyFm.down) && !(keyAm.down && keyFm.down);
}

void waitForRelease() {
  unsigned long start = millis();
  while (keyVolUp.down || keyVolDn.down || keyAm.down || keyFm.down) {
    pollKeys();
    if (millis() - start > RELEASE_WAIT_MS) break;
  }
}

// ---------------------------------------------------------------------------
// Scan-delay menu
//
// Entered by holding volume during the boot splash.
// Volume up/down changes the number. A flick of FM or AM picks which delay.
// Holding the band switch saves. Thirty seconds without a touch cancels.
// ---------------------------------------------------------------------------

void nudgeDelay(bool editFm, int step) {
  uint16_t value = editFm ? settings.fmDelayMs : settings.amDelayMs;
  int next = (int)value + step;
  if (next < (int)DELAY_MIN_MS) next = DELAY_MIN_MS;
  if (next > (int)DELAY_MAX_MS) next = DELAY_MAX_MS;
  if (editFm) settings.fmDelayMs = (uint16_t)next;
  else settings.amDelayMs = (uint16_t)next;
}

void finishMenu(bool save, uint16_t keepFm, uint16_t keepAm) {
  if (save) {
    saveSettings();
    lcdClear();
    Serial.print("Saved");
  } else {
    settings.fmDelayMs = keepFm;
    settings.amDelayMs = keepAm;
    lcdClear();
    Serial.print("Cancel");
  }
  delay(MENU_MESSAGE_MS);
}

void runSetupMenu() {
  uint16_t keepFm = settings.fmDelayMs;
  uint16_t keepAm = settings.amDelayMs;
  bool editFm = (settings.band == FM);
  unsigned long lastAction = millis();
  unsigned long bandHoldMs = millis();
  bool saving = false;

  showDelay(editFm);

  while (true) {
    unsigned long now = millis();
    bool upEdge = updateKey(keyVolUp);
    bool dnEdge = updateKey(keyVolDn);
    bool amEdge = updateKey(keyAm);
    bool fmEdge = updateKey(keyFm);
    bool bandHeld = bandHeldOneSide();

    if (fmEdge || amEdge) {
      editFm = fmEdge;
      bandHoldMs = now;
      saving = false;
      showDelay(editFm);
      lastAction = now;
    }

    if (bandHeld && (now - bandHoldMs >= SAVE_HOLD_MS)) {
      finishMenu(true, keepFm, keepAm);
      return;
    }

    if (bandHeld && !saving && (now - bandHoldMs >= SAVE_HINT_MS)) {
      lcdClear();
      Serial.print("Saving");
      saving = true;
    }

    if (!bandHeld && saving) {
      saving = false;
      showDelay(editFm);
    }

    if (!bandHeld) {
      if ((upEdge || keyRepeat(keyVolUp)) && !keyVolDn.down) {
        nudgeDelay(editFm, DELAY_STEP_MS);
        showDelay(editFm);
        lastAction = now;
      }
      if ((dnEdge || keyRepeat(keyVolDn)) && !keyVolUp.down) {
        nudgeDelay(editFm, -(int)DELAY_STEP_MS);
        showDelay(editFm);
        lastAction = now;
      }
    }

    if (now - lastAction >= MENU_IDLE_MS) {
      finishMenu(false, keepFm, keepAm);
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// Sweep comet
//
// Drawn with space, '-', '<', and '>'. Those are the plain ASCII glyphs
// this VFD already shows. One step of the comet per station.
// ---------------------------------------------------------------------------

void printSweep(int cols) {
  if (cols <= 0) return;

  int comet = (cols >= 10) ? COMET_LONG : COMET_SHORT;
  if (comet > cols) comet = cols;
  int maxPos = cols - comet;
  if (maxPos < 0) maxPos = 0;
  if (sweepPos > maxPos) sweepPos = maxPos;
  if (sweepPos < 0) sweepPos = 0;

  for (int i = 0; i < cols; i++) {
    bool inComet = i >= sweepPos && i < sweepPos + comet;
    if (!inComet) Serial.print(' ');
    else if (sweepDir > 0 && i == sweepPos + comet - 1) Serial.print('>');
    else if (sweepDir < 0 && i == sweepPos) Serial.print('<');
    else Serial.print('-');
  }

  sweepPos += sweepDir;
  if (sweepPos >= maxPos) {
    sweepPos = maxPos;
    sweepDir = -1;
  } else if (sweepPos <= 0) {
    sweepPos = 0;
    sweepDir = 1;
  }
}

// ---------------------------------------------------------------------------
// Sweep
// ---------------------------------------------------------------------------

void stepScan() {
  lcdClear();

  if (settings.band == FM) {
    tuneFreq += TUNE_STEP;
    if (tuneFreq > FM_TOP) tuneFreq = FM_START;
    radio.tuneFrequency(tuneFreq);
  } else {
    tuneFreq += TUNE_STEP;
    if (tuneFreq > AM_TOP) tuneFreq = AM_START;
    disableSoftMute();
    radio.tuneFrequency(tuneFreq);
  }

  int used = printFrequency();
  printSweep(LCD_COLS - used);
}

void showBandChange(uint8_t band) {
  startRadio(band);
  saveSettings();
  showStatus();
  holdDisplayUntil = millis() + STATUS_HOLD_MS;
  lastTuneMs = millis();
  volumeNeedsSave = false;
}

// ---------------------------------------------------------------------------
// Arduino entry
// ---------------------------------------------------------------------------

void setup() {
  pinMode(AMP_PIN, OUTPUT);
  digitalWrite(AMP_PIN, LOW);

  Serial.begin(9600);
  delay(10);
  Serial.write(0xFF);
  Serial.write(0x04);
  Serial.println();
  Serial.print("ROM:");
  Serial.print(__DATE__);

  initKey(keyVolUp, PIN_VOL_UP);
  initKey(keyVolDn, PIN_VOL_DN);
  initKey(keyAm, PIN_AM);
  initKey(keyFm, PIN_FM);
  loadSettings();

  unsigned long splashStart = millis();
  bool enterSetup = false;
  bool hinted = false;
  while (millis() - splashStart < SPLASH_MS) {
    pollKeys();
    if (keyVolUp.down || keyVolDn.down) enterSetup = true;
    if (!hinted && millis() - splashStart >= BUILD_DATE_MS) {
      lcdClear();
      Serial.print("Hold VOL");
      hinted = true;
    }
  }

  lcdClear();
  Serial.write(VFD_CURSOR_OFF);

  if (enterSetup) {
    waitForRelease();
    runSetupMenu();
    lcdClear();
  }

  startRadio(settings.band);
  digitalWrite(AMP_PIN, HIGH);
  showStatus();
  holdDisplayUntil = millis() + BOOT_STATUS_MS;
  lastTuneMs = millis();
}

void loop() {
  bool upEdge = updateKey(keyVolUp);
  bool dnEdge = updateKey(keyVolDn);
  bool amEdge = updateKey(keyAm);
  bool fmEdge = updateKey(keyFm);

  // Ignore the impossible case of both volume throws reading held.
  if (!(keyVolUp.down && keyVolDn.down)) {
    if ((upEdge || keyRepeat(keyVolUp)) && !keyVolDn.down) changeVolume(1);
    if ((dnEdge || keyRepeat(keyVolDn)) && !keyVolUp.down) changeVolume(-1);
  }

  // begin() blocks, so the "FM vol" message is timed after it returns.
  // Both band pins low is ignored. That is not a gesture this switch can make.
  if (!(keyAm.down && keyFm.down)) {
    if (amEdge && !keyFm.down) showBandChange(AM);
    else if (fmEdge && !keyAm.down) showBandChange(FM);
  }

  if (volumeNeedsSave && (millis() - lastVolumeMs) > VOLUME_SAVE_MS) {
    saveSettings();
    volumeNeedsSave = false;
  }

  unsigned long now = millis();
  if (now >= holdDisplayUntil && (now - lastTuneMs) >= scanDelayMs()) {
    lastTuneMs = now;
    stepScan();
  }
}
