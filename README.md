# Drake's Box SI4735

Arduino Frank's box. An SI4735 AM/FM receiver that keeps sweeping with the audio left open.

This is the hardware cousin of the newer [ShackHack](https://github.com/tmdrake/tmdrake-ghostboxSDR) scanner (Drake's Box SDR). Same job as the Radio Shack "shack hack" spirit boxes used in ghost hunting: step through the band, do not stop on a station, and do not mute the noise between stations. ShackHack does that sweep in software on an RTL-SDR. This box does it in a small tuner you can carry.

Frank Sumption's Frank's Box (2002) is the original open-audio sweep radio. Later shack hacks forced a consumer scanner or portable to keep scanning. The P-SB7 and the ShackHack SDR are the newer forms of that same sweep. This firmware is the SI4735 version of it.

## What it shares with a shack-hack scanner

| | Drake's Box SI4735 | Classic shack hack | ShackHack SDR |
| --- | --- | --- | --- |
| Radio | SparkFun SI4735 shield | Modified Radio Shack portable or scanner | RTL-SDR (R820T / E4000) |
| Sweep | Always running. No squelch stop. | Scan hacked so it will not park on a carrier | Software VFOs, no squelch |
| Audio between stations | Soft mute forced off, so the noise stays up | Speaker left open during scan | Demod audio always on |
| Bands | AM broadcast and FM broadcast | Usually one band, often FM | FM plus air, marine, NOAA, ham, and others |
| FM coverage | 97.3–108.0 MHz, 100 kHz steps | Full FM dial of that radio | 88–108 MHz, default 150 kHz steps |
| AM coverage | 520–1710 kHz, 10 kHz steps | Only if that radio was hacked for AM | Needs an HF-capable stick |
| Dwell | 100–2000 ms, separate FM and AM values, default 200 ms | Fixed by the radio | 10–500 ms |
| Controls | Two (ON)-OFF-(ON) switches | The radio's own buttons | On-screen HUD |
| Memory | EEPROM: volume, band, both dwells | None | Settings file on the PC |

The SI4735 can tune shortwave and longwave. This sketch does not. It stays on the AM and FM broadcast sweeps a shack hack uses.

## Hardware

- Arduino Uno, or any ATmega328P board with the Uno pinout. The radio library bit-bangs nothing; it uses the hardware SPI registers.
- [SparkFun SI4735 AM & FM Shield](https://github.com/sparkfun/SI4735_AM_FM_Shield) (DEV-10342). Schematic in this repo: `SI4735 Shield-v12.pdf`. The shield design is SparkFun's, Creative Commons ShareAlike 3.0.
- NEC FIPC8367 character VFD at 9600 baud, 20 columns. `520Khz` leaves 14 columns. Those columns show a bouncing `---->` / `<----` sweep, one step per station. The marks are space, hyphen, and angle brackets.
- Audio amp enabled on pin 3. The pin stays low until the first station is tuned.
- Two momentary (ON)-OFF-(ON) switches, each throw wired to ground, internal pull-ups. Center is off, and the switch springs back. The two throws of one switch cannot be on at the same time.
  - Volume: A5 up, center, A4 down
  - Band: A3 AM, center, A2 FM

The shield antenna switch has to match the band. FM uses the whip jack. AM uses the ferrite loop. The sketch cannot flip that switch.

Leave digital pin 13 alone. On this shield it is the radio SPI clock. The onboard LED will still flicker while the radio is commanded.

Library wiring, from the SparkFun library: power on pin 8, radio reset on pin 9, interrupt on pin 2, SPI on pins 10–13.

## Controls

Normal run:

- Volume up / volume down change the level from 0 to 63. The display shows `Vol 12` and the sweep pauses about a second so the number can be read. The level is saved after you let go.
- Flick AM or FM to restart that band at 520 kHz or 97.3 MHz. The saved volume comes back, and the band is saved. Letting go returns the switch to center.

Boot:

1. `ROM:` plus the build date, for 2 seconds. Example: `ROM:Nov 12 2026`.
2. `Hold VOL` until 3.5 seconds from power-up.
3. The radio starts on the saved band and volume. The display shows `FM vol 12` or `AM vol 12`, then the sweep takes over.

Hold the volume switch either way during those 3.5 seconds to open the dwell menu. Let it spring back to center when the menu appears.

- Volume up / down changes the dwell in 50 ms steps, from 100 ms to 2000 ms. Hold to repeat.
- Flick the band switch to FM or AM to choose which dwell is on screen (`FM 200ms` or `AM 200ms`).
- Hold the band switch either way for about a second and a half. The screen says `Saving`, then `Saved`.
- Let it sit for 30 seconds and it cancels. The old dwells stay.

The chip's own volume starts at 5 inside `radio.begin()`, including every band change. The sketch writes the saved level back with the SI4735 volume property (`0x4000`) after each begin.

## Memory

EEPROM on the AVR holds a small record with a magic byte and a checksum:

- volume (0–63), default 5
- band, default FM
- FM dwell and AM dwell, default 200 ms each

A blank or corrupt EEPROM loads those defaults and saves them. The live frequency is not stored. The sweep starts again at 97.3 MHz or 520 kHz. Writing the frequency on every step would wear the EEPROM.

## Build date

The `ROM:` line is the compiler `__DATE__`. It is not a hand-edited version. Compiling from the Arduino IDE or from `build.ps1` stamps that day's date into the firmware. The LCD shows it for the first 2 seconds.

`build.ps1` compiles for an Uno, then reads the date back out of the ELF and writes `build/BUILD.txt`. The `build/` folder is not part of the repo.

```powershell
./build.ps1
```

Needs [arduino-cli](https://arduino.github.io/arduino-cli/) and the Arduino AVR core (`arduino:avr`). The SI4735 library shipped in `third_party/Si4735` is the one the script compiles against. In the IDE, install that folder as the Si4735 library and do not keep a second copy beside it.

## Disclaimer

Receive only, AM and FM broadcast. Ghost-box / ITC use is the same entertainment practice as a shack-hack scanner. The sweep plays fragments of real broadcasts and the noise between them. This project does not claim those fragments are anything else. Check the rules where you listen.

## Credits

- Firmware: Dennis Liang, public domain. https://www.pasadenahosthunters.org
- SI4735 library: Ryan Owens, SparkFun Electronics, 2011, Beerware. Unmodified copy in `third_party/Si4735`.
- Shield: SparkFun SI4735 AM & FM Shield, CC BY-SA 3.0. https://github.com/sparkfun/SI4735_AM_FM_Shield
- Newer scanner in this family: https://github.com/tmdrake/tmdrake-ghostboxSDR

## License

Public domain. See [LICENSE](LICENSE). The library and the shield keep the licenses named above.
