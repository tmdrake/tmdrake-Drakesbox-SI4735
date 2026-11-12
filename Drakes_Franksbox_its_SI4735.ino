/* Drakes Franks Box

Dennis Liang
https://www.pasadenahosthunters.org

This code is in the public domain.

Two switch sets, active low, internal pull-ups:
  Volume set: A5 up, A4 down
  Band set:   A3 AM, A2 FM

Hold both band buttons during the startup message to set the
scan delays. Volume up/down changes the delay. Press one band
button to pick FM or AM. Hold both band buttons to save, or
both volume buttons to cancel.

Volume, band, and both scan delays are stored in EEPROM.
The scan always restarts at 97.3 MHz or 520 kHz.

The boot line "ROM:" is the compiler build date (__DATE__).
It changes on every compile, from the IDE or from build.ps1.
*/

const unsigned long BUILD_DATE_MS = 2000;
const unsigned long SPLASH_MS = 3500;

#include <Si4735.h>
#include <EEPROM.h>

const int ampPin = 3;
const int pinVolUp = A5;
const int pinVolDn = A4;
const int pinAm = A3;
const int pinFm = A2;

const uint8_t EEPROM_MAGIC = 0xD5;
const uint8_t EEPROM_VERSION = 1;
const uint8_t VOLUME_MAX = 63;
const uint16_t DELAY_MIN_MS = 100;
const uint16_t DELAY_MAX_MS = 2000;
const uint16_t DELAY_STEP_MS = 50;
const unsigned long DEBOUNCE_MS = 30;
const unsigned long REPEAT_START_MS = 450;
const unsigned long REPEAT_MS = 150;
const unsigned long CHORD_MS = 800;
const unsigned long MENU_IDLE_MS = 30000;
const int FM_START = 9730;
const int FM_TOP = 10800;
const int AM_START = 520;
const int AM_TOP = 1710;

struct RadioSettings {
  uint8_t magic;
  uint8_t version;
  uint8_t volume;
  uint8_t band;
  uint16_t fmDelayMs;
  uint16_t amDelayMs;
  uint8_t checksum;
};

struct Key {
  uint8_t pin;
  bool down;
  bool prevRead;
  unsigned long changeMs;
  unsigned long lastRepeatMs;
};

Si4735 radio;
RadioSettings settings;
Key keyVolUp;
Key keyVolDn;
Key keyAm;
Key keyFm;

int tunefreq = FM_START;
unsigned long previoustuneMillis = 0;
unsigned long quietUntil = 0;
unsigned long lastVolumeMs = 0;
bool volumeDirty = false;

uint8_t checksumOf(const RadioSettings &s) {
  uint8_t sum = 0xA5;
  sum += s.magic;
  sum += s.version;
  sum += s.volume;
  sum += s.band;
  sum += (uint8_t)s.fmDelayMs;
  sum += (uint8_t)(s.fmDelayMs >> 8);
  sum += (uint8_t)s.amDelayMs;
  sum += (uint8_t)(s.amDelayMs >> 8);
  return sum;
}

void setDefaults() {
  settings.magic = EEPROM_MAGIC;
  settings.version = EEPROM_VERSION;
  settings.volume = 5;
  settings.band = FM;
  settings.fmDelayMs = 200;
  settings.amDelayMs = 200;
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

void lcdClear() {
  Serial.write(0x0C);
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

// Absolute volume. begin() always forces the library volume back to 5,
// and that counter is private, so the chip is set directly.
void writeVolume(uint8_t level) {
  char cmd[13];
  if (level > VOLUME_MAX) level = VOLUME_MAX;
  sprintf(cmd, "1200400000%02X", (unsigned)level);
  radio.sendCommand(cmd);
}

void killsoftmute() {
  radio.sendCommand("120013020000");
  radio.sendCommand("1200130300FF");
  radio.sendCommand("120033020000");
  radio.sendCommand("120033030000");
  radio.sendCommand("12001106007F");
}

void initKey(Key &key, uint8_t pin) {
  key.pin = pin;
  key.down = false;
  key.prevRead = false;
  key.changeMs = 0;
  key.lastRepeatMs = 0;
  pinMode(pin, INPUT_PULLUP);
}

// Returns true once when the key becomes pressed.
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

unsigned long scanDelayMs() {
  if (settings.band == FM) return settings.fmDelayMs;
  return settings.amDelayMs;
}

void nudgeDelay(bool editFm, int step) {
  uint16_t value = editFm ? settings.fmDelayMs : settings.amDelayMs;
  int next = (int)value + step;
  if (next < (int)DELAY_MIN_MS) next = DELAY_MIN_MS;
  if (next > (int)DELAY_MAX_MS) next = DELAY_MAX_MS;
  if (editFm) settings.fmDelayMs = (uint16_t)next;
  else settings.amDelayMs = (uint16_t)next;
}

void waitForRelease() {
  unsigned long start = millis();
  while (keyVolUp.down || keyVolDn.down || keyAm.down || keyFm.down) {
    pollKeys();
    if (millis() - start > 8000) break;
  }
}

void runSetupMenu() {
  uint16_t keepFm = settings.fmDelayMs;
  uint16_t keepAm = settings.amDelayMs;
  bool editFm = (settings.band == FM);
  unsigned long lastAction = millis();
  bool bandChord = false;
  bool volChord = false;
  unsigned long chordMs = 0;

  showDelay(editFm);

  while (true) {
    unsigned long now = millis();
    bool upEdge = updateKey(keyVolUp);
    bool dnEdge = updateKey(keyVolDn);
    bool amEdge = updateKey(keyAm);
    bool fmEdge = updateKey(keyFm);
    bool bandsHeld = keyAm.down && keyFm.down;
    bool volsHeld = keyVolUp.down && keyVolDn.down;

    if (bandsHeld) {
      volChord = false;
      if (!bandChord) {
        bandChord = true;
        chordMs = now;
      } else if (now - chordMs >= CHORD_MS) {
        saveSettings();
        lcdClear();
        Serial.print("Saved");
        delay(700);
        return;
      }
    } else if (volsHeld) {
      bandChord = false;
      if (!volChord) {
        volChord = true;
        chordMs = now;
      } else if (now - chordMs >= CHORD_MS) {
        settings.fmDelayMs = keepFm;
        settings.amDelayMs = keepAm;
        lcdClear();
        Serial.print("Cancel");
        delay(700);
        return;
      }
    } else {
      bandChord = false;
      volChord = false;

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
      if (fmEdge && !keyAm.down) {
        editFm = true;
        showDelay(editFm);
        lastAction = now;
      }
      if (amEdge && !keyFm.down) {
        editFm = false;
        showDelay(editFm);
        lastAction = now;
      }
    }

    if (now - lastAction >= MENU_IDLE_MS) {
      saveSettings();
      lcdClear();
      Serial.print("Saved");
      delay(700);
      return;
    }
  }
}

void startRadio(uint8_t band) {
  settings.band = band;
  tunefreq = (band == FM) ? FM_START : AM_START;
  radio.begin(band);
  writeVolume(settings.volume);
  killsoftmute();
  radio.tuneFrequency(tunefreq);
}

void changeVolume(int dir) {
  int next = (int)settings.volume + dir;
  if (next < 0 || next > VOLUME_MAX) return;

  settings.volume = (uint8_t)next;
  writeVolume(settings.volume);
  volumeDirty = true;
  lastVolumeMs = millis();
  showVolume();
  quietUntil = millis() + 900;
}

void stepScan() {
  lcdClear();

  if (settings.band == FM) {
    tunefreq += 10;
    if (tunefreq > FM_TOP) tunefreq = FM_START;
    radio.tuneFrequency(tunefreq);
    Serial.print(tunefreq / 100.0, 1);
    Serial.print("Mhz");
  } else {
    tunefreq += 10;
    if (tunefreq > AM_TOP) tunefreq = AM_START;
    killsoftmute();
    radio.tuneFrequency(tunefreq);
    Serial.print(tunefreq);
    Serial.print("Khz");
  }
}

void setup() {
  pinMode(ampPin, OUTPUT);
  digitalWrite(ampPin, LOW);

  Serial.begin(9600);
  delay(10);
  Serial.write(0xFF);
  Serial.write(0x04); // brightness 25%
  Serial.println();
  Serial.print("ROM:");
  Serial.print(__DATE__);

  initKey(keyVolUp, pinVolUp);
  initKey(keyVolDn, pinVolDn);
  initKey(keyAm, pinAm);
  initKey(keyFm, pinFm);
  loadSettings();

  unsigned long splashStart = millis();
  bool enterSetup = false;
  bool hinted = false;
  while (millis() - splashStart < SPLASH_MS) {
    pollKeys();
    if (keyAm.down && keyFm.down) enterSetup = true;
    if (!hinted && millis() - splashStart >= BUILD_DATE_MS) {
      lcdClear();
      Serial.print("Hold AM+FM");
      hinted = true;
    }
  }

  lcdClear();
  Serial.write(0x14);

  if (enterSetup) {
    waitForRelease();
    runSetupMenu();
    lcdClear();
  }

  startRadio(settings.band);
  digitalWrite(ampPin, HIGH);
  showStatus();
  quietUntil = millis() + 1200;
  previoustuneMillis = millis();
}

void loop() {
  bool upEdge = updateKey(keyVolUp);
  bool dnEdge = updateKey(keyVolDn);
  bool amEdge = updateKey(keyAm);
  bool fmEdge = updateKey(keyFm);

  if (!(keyVolUp.down && keyVolDn.down)) {
    if ((upEdge || keyRepeat(keyVolUp)) && !keyVolDn.down) changeVolume(1);
    if ((dnEdge || keyRepeat(keyVolDn)) && !keyVolUp.down) changeVolume(-1);
  }

  // A band chord is the setup gesture. Ignore it while running.
  // Time the status message after begin(), which blocks for several hundred ms.
  if (!(keyAm.down && keyFm.down)) {
    if (amEdge && !keyFm.down) {
      startRadio(AM);
      saveSettings();
      showStatus();
      quietUntil = millis() + 900;
      previoustuneMillis = millis();
      volumeDirty = false;
    } else if (fmEdge && !keyAm.down) {
      startRadio(FM);
      saveSettings();
      showStatus();
      quietUntil = millis() + 900;
      previoustuneMillis = millis();
      volumeDirty = false;
    }
  }

  if (volumeDirty && (millis() - lastVolumeMs) > 500) {
    saveSettings();
    volumeDirty = false;
  }

  unsigned long now = millis();
  if (now >= quietUntil && (now - previoustuneMillis) >= scanDelayMs()) {
    previoustuneMillis = now;
    stepScan();
  }
}
