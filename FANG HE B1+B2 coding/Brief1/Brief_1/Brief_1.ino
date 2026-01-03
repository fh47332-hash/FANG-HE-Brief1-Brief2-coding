// === Air pump + fabric switch control program
//     (Exhale time and inhale time can be set separately) ===

// Pin definitions
#define E1 10
#define E2 11
#define VALVE 4
#define TOUCH_PIN 2   // One side of fabric connects to D2, the other to GND (using internal pull-up)

// === Time control parameters (adjustable if needed) ===
const unsigned long exhaleTime = 7000UL; // Exhale time, example: 7000 ms = 7 seconds (longer)
const unsigned long inhaleTime = 3000UL; // Inhale time, example: 3000 ms = 3 seconds (shorter)

// Touch debounce time
const unsigned long debounceDelay = 60UL;   // Touch debounce delay (ms)

// State variables
bool running = false;           // Whether a full breathing cycle is currently running
int phase = 0;                  // 0 = exhale phase (E1), 1 = inhale phase (E2)
unsigned long phaseStartMs = 0; // Start time of the current phase

// Touch debounce / raw value tracking
bool lastRawTouch = HIGH;       // Last raw reading
unsigned long lastChangeMs = 0; // Last time the raw value changed
bool stableTouch = HIGH;        // Stable (debounced) touch value
bool lastStablePrev = HIGH;     // Previous stable state (for edge detection)

void setup() {
  pinMode(E1, OUTPUT);
  pinMode(E2, OUTPUT);
  pinMode(VALVE, OUTPUT);
  pinMode(TOUCH_PIN, INPUT_PULLUP); // Use internal pull-up: not touched = HIGH, touched = LOW

  stopAll(); // Stop everything at startup
}

void loop() {
  // ---------- 1) Read raw touch value and apply debounce ----------
  bool raw = digitalRead(TOUCH_PIN); // LOW = touched, HIGH = not touched
  if (raw != lastRawTouch) {
    lastChangeMs = millis();
    lastRawTouch = raw;
  }
  if (millis() - lastChangeMs > debounceDelay) {
    stableTouch = raw;
  }

  // ---------- 2) If not running, detect falling edge to trigger one full cycle ----------
  if (!running) {
    if (lastStablePrev == HIGH && stableTouch == LOW) {
      // Touch triggers one full breathing cycle
      running = true;
      phase = 0;
      phaseStartMs = millis();
    }
  }
  lastStablePrev = stableTouch;

  // ---------- 3) While running, execute phases based on time (non-blocking) ----------
  if (running) {
    unsigned long now = millis();

    if (phase == 0) {
      // Exhale phase (inflate)
      digitalWrite(VALVE, HIGH);
      analogWrite(E1, 200); // E1 PWM duty cycle (adjustable 0–255)
      analogWrite(E2, 0);
      // Check if it's time to switch to inhale phase
      if (now - phaseStartMs >= exhaleTime) {
        phase = 1;
        phaseStartMs = now;
      }
    } else if (phase == 1) {
      // Inhale phase (deflate)
      digitalWrite(VALVE, LOW);
      analogWrite(E1, 0);
      analogWrite(E2, 200); // E2 PWM duty cycle (adjustable)
      // Check if the full cycle should end
      if (now - phaseStartMs >= inhaleTime) {
        running = false;
        stopAll();
      }
    }
  } else {
    // Ensure everything is stopped when not running
    stopAll();
  }

  delay(10); // Small delay to reduce CPU usage (does not affect responsiveness)
}

void stopAll() {
  digitalWrite(VALVE, LOW);
  analogWrite(E1, 0);
  analogWrite(E2, 0);
}
