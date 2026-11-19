# Team 2-Bit — ESP32 Biometric-Gated EVM (Wokwi Demo)

This is a classroom demonstration with virtual fingerprint inputs, not an election-grade device or real biometric simulation.

## Wokwi setup
1. Open https://wokwi.com/projects/new/esp32
2. Replace **all** of `diagram.json` with this folder's `diagram.json`.
3. Replace **all** of `sketch.ino` with this folder's `sketch.ino` (remove default setup()/loop()).
4. Create `libraries.txt`, containing `LiquidCrystal I2C`, or add that library using Library Manager.
5. Start Simulation; use Serial Monitor at 115200 baud (newline line ending).

## Demo
- Enter `enroll 1`, `enroll 2`, `enroll 3` to register virtual voter IDs.
- Enter `scan 1`; press Party A, then CONFIRM. This records a vote.
- Enter `scan 1` again: duplicate rejected.
- Enter `scan 2`; press Party B, then CONFIRM.
- Enter `results` to view totals; enter `status` to view only enrollment/voting flags.
- Try `scan 3` then wait 30 seconds: timed-out session records no vote.
- Hold ADMIN for 3 seconds to close polls and rotate results on LCD.
- TAMPER button causes a latched lockout.
- Enter exact case-sensitive command `reset DEMO` to erase test state (demo only).

## Pins
- LCD SDA 21, SCL 22, VCC 5V, GND GND.
- Party A 25, B 26, C 27; Confirm 32; Tamper 33; Admin 14 (all other button terminals GND; INPUT_PULLUP).
- Green LED 18 via 220 ohm, Red LED 19 via 220 ohm, cathodes to GND.
- Buzzer simulated at GPIO23 to GND.
- R307 actual hardware (not simulated): sensor TX -> GPIO16 (RX2), RX -> GPIO17 (TX2), with verified supply and level-shifting where necessary.

## Physical hardware warning
The pictured SFM-27 active buzzer is **not** the Wokwi piezo part: in the real circuit switch the active buzzer via BC547 + 1k base resistor and confirm load current and flyback protection if applicable. Do not connect a 5V sensor UART TX or 5V I2C pull-up directly to ESP32 GPIO. Confirm R307 sensor supply and logic levels from its exact datasheet.

## Scope/limitations
- This sketch stores enrollment flags, has-voted flags, party totals, poll/tamper locks in ESP32 Preferences NVS.
- It does not connect to Flask or provide live telemetry yet; that is a separate integration step.
- It does not store per-voter party choices. However, vote counts and voter flags are not a transactional database, so power failure mid-write may cause inconsistency.
- Do not use with real elections, real personal biometric data, or public deployment.
