
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>

// ===================================
// TEAM 2-BIT ESP32 EVM
// Wokwi Virtual Fingerprint Version
// ===================================

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// GPIO connections
const int PARTY_A = 25;
const int PARTY_B = 26;
const int PARTY_C = 27;
const int CONFIRM_PIN = 32;
const int TAMPER_PIN = 33;
const int ADMIN_PIN = 14;
const int GREEN_LED = 18;
const int RED_LED = 19;
const int BUZZER = 23;

// Configuration
const int MAX_VOTERS = 20;
const unsigned long VOTE_TIMEOUT = 30000;
const unsigned long ADMIN_HOLD = 3000;
const unsigned long DEBOUNCE = 150;

// Database
Preferences prefs;

bool registered[MAX_VOTERS + 1] = {};
bool voted[MAX_VOTERS + 1] = {};

uint32_t votesA = 0;
uint32_t votesB = 0;
uint32_t votesC = 0;

bool pollsClosed = false;
bool tamperLocked = false;

int activeVoter = 0;
int selectedParty = 0;

unsigned long voteStart = 0;
unsigned long adminStart = 0;
unsigned long messageUntil = 0;
unsigned long redUntil = 0;
unsigned long resultTime = 0;

bool adminHandled = false;

// Button state
const int buttonPins[4] = {
  PARTY_A, PARTY_B, PARTY_C, CONFIRM_PIN
};

bool previousButton[4] = {
  false, false, false, false
};

unsigned long lastPress[4] = {};

// Serial input buffer
String serialBuffer = "";
bool escapeSequence = false;
bool csiSequence = false;

// ===================================
// LCD FUNCTIONS
// ===================================

void showLCD(String line1, String line2) {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(line1.substring(0, 16));

  lcd.setCursor(0, 1);
  lcd.print(line2.substring(0, 16));
}

void showHome() {
  if (tamperLocked) {
    showLCD("TAMPER ALERT", "Voting disabled");
  }
  else if (pollsClosed) {
    showLCD("Polls Closed", "See Results");
  }
  else if (activeVoter != 0) {
    if (selectedParty == 0) {
      showLCD("Voter Verified", "Choose A/B/C");
    }
    else {
      char party = 'A' + selectedParty - 1;
      showLCD("Selected Party", String(party) + " - CONFIRM");
    }
  }
  else {
    showLCD("EVM Booth Ready", "Scan Finger ID");
  }
}

// ===================================
// BUZZER AND ERROR
// ===================================

void successBeep() {
  tone(BUZZER, 1200, 150);
}

void errorBeep() {
  tone(BUZZER, 400, 350);
}

void showError(String message) {
  Serial.println("ERROR: " + message);

  showLCD("Access / Error", message);

  redUntil = millis() + 1400;
  messageUntil = millis() + 1800;

  errorBeep();
}

// ===================================
// STORAGE
// ===================================

void loadData() {
  prefs.begin("evm-demo", false);

  votesA = prefs.getUInt("a", 0);
  votesB = prefs.getUInt("b", 0);
  votesC = prefs.getUInt("c", 0);

  pollsClosed = prefs.getBool("closed", false);
  tamperLocked = prefs.getBool("tamper", false);

  for (int i = 1; i <= MAX_VOTERS; i++) {
    String enrollmentKey = "e" + String(i);
    String votedKey = "v" + String(i);

    registered[i] = prefs.getBool(
      enrollmentKey.c_str(), false
    );

    voted[i] = prefs.getBool(
      votedKey.c_str(), false
    );
  }
}

// ===================================
// RESULTS AND STATUS
// ===================================

void showResults() {
  uint32_t total = votesA + votesB + votesC;

  Serial.println();
  Serial.println("====== EVM RESULTS ======");

  Serial.printf("Party A: %lu\n",
                (unsigned long)votesA);
  Serial.printf("Party B: %lu\n",
                (unsigned long)votesB);
  Serial.printf("Party C: %lu\n",
                (unsigned long)votesC);

  Serial.printf("Total Votes: %lu\n",
                (unsigned long)total);

  Serial.printf("Polls: %s\n",
                pollsClosed ? "CLOSED" : "OPEN");

  Serial.printf("Tamper: %s\n",
                tamperLocked ? "LOCKED" : "OK");

  Serial.println("=========================");
}

void showStatus() {
  Serial.println();
  Serial.println("===== VOTER STATUS =====");

  for (int i = 1; i <= MAX_VOTERS; i++) {
    if (registered[i]) {
      Serial.printf(
        "ID %d: %s\n",
        i,
        voted[i] ? "VOTED" : "ELIGIBLE"
      );
    }
  }

  Serial.println("========================");
}

void showHelp() {
  Serial.println();
  Serial.println("====== EVM COMMANDS ======");
  Serial.println("enroll 1    Register voter");
  Serial.println("scan 1      Authenticate voter");
  Serial.println("results     Display results");
  Serial.println("status      Display voter status");
  Serial.println("help        List commands");
  Serial.println("reset DEMO  Reset all demo data");
  Serial.println("==========================");
  Serial.println();
}

// ===================================
// VIRTUAL FINGERPRINT ENROLLMENT
// ===================================

void enrollVoter(int id) {
  if (pollsClosed || tamperLocked) {
    showError("Booth Locked");
    return;
  }

  if (id < 1 || id > MAX_VOTERS) {
    showError("Invalid Voter ID");
    return;
  }

  if (registered[id]) {
    showError("Already Enrolled");
    return;
  }

  registered[id] = true;

  String key = "e" + String(id);
  prefs.putBool(key.c_str(), true);

  Serial.printf(
    "SUCCESS: Voter %d enrolled!\n", id
  );

  showLCD("Enrollment OK", "Voter ID " + String(id));
  messageUntil = millis() + 1800;

  successBeep();
}

// ===================================
// VOTER AUTHENTICATION
// ===================================

void scanVoter(int id) {
  if (pollsClosed || tamperLocked) {
    showError("Voting Disabled");
    return;
  }

  if (activeVoter != 0) {
    showError("Session Active");
    return;
  }

  if (id < 1 || id > MAX_VOTERS ||
      !registered[id]) {
    showError("Not Enrolled");
    return;
  }

  if (voted[id]) {
    showError("Already Voted");
    return;
  }

  activeVoter = id;
  selectedParty = 0;
  voteStart = millis();

  Serial.printf(
    "Voter %d authenticated successfully.\n", id
  );

  Serial.println(
    "Select Party A/B/C and press CONFIRM."
  );

  successBeep();
  showHome();
}

// ===================================
// VOTING
// ===================================

void cancelSession(String reason) {
  activeVoter = 0;
  selectedParty = 0;
  voteStart = 0;

  Serial.println(reason);
  showHome();
}

void selectParty(int party) {
  if (pollsClosed || tamperLocked) return;

  if (activeVoter == 0) {
    showError("Scan Finger First");
    return;
  }

  selectedParty = party;

  char partyName = 'A' + party - 1;

  Serial.printf(
    "Party %c selected. Press CONFIRM.\n",
    partyName
  );

  showHome();
}

void confirmVote() {
  if (pollsClosed || tamperLocked) return;

  if (activeVoter == 0) {
    showError("Scan Finger First");
    return;
  }

  if (selectedParty == 0) {
    showError("Choose Party");
    return;
  }

  if (millis() - voteStart >= VOTE_TIMEOUT) {
    cancelSession("Voting timed out.");
    return;
  }

  int voter = activeVoter;

  if (voted[voter]) {
    showError("Already Voted");
    activeVoter = 0;
    selectedParty = 0;
    return;
  }

  // Demo counters are stored independently
  // from voter participation records.
  if (selectedParty == 1) {
    votesA++;
    prefs.putUInt("a", votesA);
  }
  else if (selectedParty == 2) {
    votesB++;
    prefs.putUInt("b", votesB);
  }
  else if (selectedParty == 3) {
    votesC++;
    prefs.putUInt("c", votesC);
  }

  voted[voter] = true;

  String key = "v" + String(voter);
  prefs.putBool(key.c_str(), true);

  activeVoter = 0;
  selectedParty = 0;
  voteStart = 0;

  Serial.println("SUCCESS: Vote Recorded!");
  showResults();

  showLCD("Vote Recorded", "Thank You!");

  messageUntil = millis() + 1800;
  successBeep();
}

// ===================================
// TAMPER AND ADMIN
// ===================================

void triggerTamper() {
  if (tamperLocked) return;

  tamperLocked = true;
  prefs.putBool("tamper", true);

  activeVoter = 0;
  selectedParty = 0;
  voteStart = 0;

  Serial.println("TAMPER ALERT: Voting Locked!");

  showLCD("TAMPER ALERT", "Voting Disabled");
  errorBeep();
}

void closePolls() {
  if (pollsClosed) return;

  pollsClosed = true;
  prefs.putBool("closed", true);

  activeVoter = 0;
  selectedParty = 0;
  voteStart = 0;

  Serial.println("ADMIN: Polls Closed!");
  showResults();

  showHome();
  successBeep();
}

// ===================================
// RESET DEMO
// ===================================

void resetDemo() {
  prefs.clear();

  for (int i = 1; i <= MAX_VOTERS; i++) {
    registered[i] = false;
    voted[i] = false;
  }

  votesA = 0;
  votesB = 0;
  votesC = 0;

  pollsClosed = false;
  tamperLocked = false;

  activeVoter = 0;
  selectedParty = 0;
  voteStart = 0;

  redUntil = 0;
  messageUntil = 0;

  Serial.println("DEMO DATA RESET SUCCESSFULLY");

  showHome();
}

// ===================================
// SERIAL COMMAND PROCESSING
// ===================================

void processCommand(String command) {
  command.trim();

  if (command.length() == 0) return;

  Serial.print("Command received: ");
  Serial.println(command);

  String lower = command;
  lower.toLowerCase();

  if (lower == "help") {
    showHelp();
  }
  else if (lower == "results") {
    showResults();
  }
  else if (lower == "status") {
    showStatus();
  }
  else if (lower.startsWith("enroll ")) {
    int id = lower.substring(7).toInt();
    enrollVoter(id);
  }
  else if (lower.startsWith("scan ")) {
    int id = lower.substring(5).toInt();
    scanVoter(id);
  }
  else if (command == "reset DEMO") {
    resetDemo();
  }
  else {
    Serial.println("Unknown command. Type help.");
  }
}

// ===================================
// IMPROVED SERIAL INPUT HANDLER
// ===================================

void handleSerialInput() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    // Ignore terminal ANSI escape sequences
    if (escapeSequence) {
      if (c == '[' || c == 'O') {
        escapeSequence = false;
        csiSequence = true;
      } else {
        escapeSequence = false;
      }
      continue;
    }

    if (csiSequence) {
      // ANSI CSI ends on a final byte
      if (c >= 0x40 && c <= 0x7E) {
        csiSequence = false;
      }
      continue;
    }

    if (c == 27) {
      escapeSequence = true;
      continue;
    }

    if (c == '\r' || c == '\n') {
      serialBuffer.trim();

      if (serialBuffer.length() > 0) {
        processCommand(serialBuffer);
      }

      serialBuffer = "";
    }
    else if (c == 8 || c == 127) {
      if (serialBuffer.length() > 0) {
        serialBuffer.remove(serialBuffer.length() - 1);
      }
    }
    else if (c >= 32 && c <= 126) {
      if (serialBuffer.length() < 64) {
        serialBuffer += c;
      }
    }
  }
}

// ===================================
// SETUP
// ===================================

void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);

  lcd.init();
  lcd.backlight();

  pinMode(PARTY_A, INPUT_PULLUP);
  pinMode(PARTY_B, INPUT_PULLUP);
  pinMode(PARTY_C, INPUT_PULLUP);

  pinMode(CONFIRM_PIN, INPUT_PULLUP);
  pinMode(TAMPER_PIN, INPUT_PULLUP);
  pinMode(ADMIN_PIN, INPUT_PULLUP);

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);

  loadData();

  showLCD("Team 2-Bit EVM", "Starting...");
  delay(800);

  showHome();

  Serial.println();
  Serial.println("================================");
  Serial.println("TEAM 2-BIT ESP32 SMART EVM");
  Serial.println("WOKWI VIRTUAL FINGERPRINT MODE");
  Serial.println("================================");

  showHelp();
  showResults();
}

// ===================================
// MAIN LOOP
// ===================================

void loop() {
  unsigned long now = millis();

  // Serial commands
  handleSerialInput();

  // Tamper switch
  if (digitalRead(TAMPER_PIN) == LOW) {
    triggerTamper();
  }

  // Admin button: hold for 3 seconds
  if (digitalRead(ADMIN_PIN) == LOW) {
    if (adminStart == 0) {
      adminStart = now;
    }

    if (!adminHandled &&
        now - adminStart >= ADMIN_HOLD) {
      adminHandled = true;
      closePolls();
    }
  }
  else {
    adminStart = 0;
    adminHandled = false;
  }

  // Voting timeout
  if (activeVoter != 0 &&
      now - voteStart >= VOTE_TIMEOUT) {
    cancelSession("30-second timeout. Vote cancelled.");
  }

  // Voting buttons
  for (int i = 0; i < 4; i++) {
    bool pressed = digitalRead(buttonPins[i]) == LOW;

    if (pressed && !previousButton[i] &&
        now - lastPress[i] >= DEBOUNCE) {

      lastPress[i] = now;

      if (i < 3) {
        selectParty(i + 1);
      }
      else {
        confirmVote();
      }
    }

    previousButton[i] = pressed;
  }

  // LED status
  digitalWrite(
    GREEN_LED,
    activeVoter != 0 && !pollsClosed &&
    !tamperLocked ? HIGH : LOW
  );

  digitalWrite(
    RED_LED,
    tamperLocked || now < redUntil ?
    HIGH : LOW
  );

  // Restore LCD after temporary message
  if (messageUntil != 0 &&
      now >= messageUntil) {
    messageUntil = 0;
    showHome();
  }

  // Cycle through final results
  if (pollsClosed && !tamperLocked &&
      now - resultTime >= 3000) {
    resultTime = now;

    static int page = 0;

    if (page == 0) {
      showLCD("Polls Closed",
              "Party A: " + String(votesA));
    }
    else if (page == 1) {
      showLCD("Polls Closed",
              "Party B: " + String(votesB));
    }
    else if (page == 2) {
      showLCD("Polls Closed",
              "Party C: " + String(votesC));
    }
    else {
      showLCD("Polls Closed",
              "Total: " +
              String(votesA + votesB + votesC));
    }

    page = (page + 1) % 4;
  }

  delay(10);
}
