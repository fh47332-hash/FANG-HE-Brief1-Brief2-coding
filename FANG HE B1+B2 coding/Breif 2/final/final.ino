// Heart_GSR_A0_A1.ino
// Heart sensor -> A0, GSR -> A1
// Serial outputs:
//   ECG each sample: "RAW <int> BPM <int> Beat <0/1>"
//   GSR each update: "Value2 <int> Value4 <int> Contact <0/1>"

const int HR_PIN = A0;
const int GSR_PIN = A1;

// ---------- Heart (ECG / Pulse) settings ----------
const unsigned long SAMPLE_PERIOD_US = 4000UL; // 4 ms -> ~250 Hz
const unsigned long PEAK_MIN_GAP_MS = 600;     // min ms between beats (debounce)
const unsigned long PULSE_MS = 120;            // beat pulse width (ms)
int offset = 36;                               // threshold offset (adjust to sensor)
const int WINDOW_BEATS = 6;                    // smoothing window for BPM
float baseline_hr = 512.0;
const float BASELINE_ALPHA = 0.001;            // baseline tracking rate

unsigned long nextSampleMicros = 0;
unsigned long lastPeakTime = 0;
unsigned long lastPulseEnd = 0;
bool pulseOn = false;
int beatOut = 0;
float bpm = 0.0;
unsigned long intervals[WINDOW_BEATS];
int intervalIdx = 0;
int intervalCount = 0;

// ---------- GSR settings ----------
const float smoothFactor = 0.18; // smoothing for filtered value
const int OVERSAMPLE = 16;       // oversample reads for stability
const int VAR_WINDOW = 40;       // window for variance/stddev
const float STDDEV_THRESHOLD = 2.0;
const float DIFF_THRESHOLD = 6.0;
const int DELAY_MS_GSR = 100;    // GSR update rate ~10Hz

float filtered_gsr = 0.0;
float baseline_gsr = 600.0;

int rawBuffer[VAR_WINDOW];
int varIdx = 0;
int varCount = 0;
unsigned long lastGsrMillis = 0;

void setup() {
  Serial.begin(115200);
  delay(200);

  // --- init heart baseline (quick average) ---
  long sum = 0;
  for (int i = 0; i < 200; i++) {
    sum += analogRead(HR_PIN);
    delay(2);
  }
  baseline_hr = sum / 200.0;
  nextSampleMicros = micros();

  // --- init GSR baseline (oversampled average) ---
  long ssum = 0;
  const int INIT_SAMPLES = 200;
  for (int i = 0; i < INIT_SAMPLES; i++) {
    long s = 0;
    for (int k = 0; k < OVERSAMPLE; k++) {
      s += analogRead(GSR_PIN);
      delayMicroseconds(200);
    }
    int avg = s / OVERSAMPLE;
    ssum += avg;
    delay(3);
  }
  filtered_gsr = ssum / (float)INIT_SAMPLES;
  baseline_gsr = filtered_gsr;
  for (int i = 0; i < VAR_WINDOW; i++) rawBuffer[i] = (int)filtered_gsr;
  varIdx = 0;
  varCount = VAR_WINDOW;

  lastGsrMillis = millis();
}

void loop() {
  // ---------------- Heart sampling (~250Hz) ----------------
  unsigned long nowMicros = micros();
  if (nowMicros >= nextSampleMicros) {
    nextSampleMicros += SAMPLE_PERIOD_US;

    unsigned long nowMs = millis();
    int raw = analogRead(HR_PIN); // 0..1023

    // update slow baseline
    baseline_hr = baseline_hr * (1.0 - BASELINE_ALPHA) + raw * BASELINE_ALPHA;
    int threshold = int(baseline_hr) + offset;

    // rising edge detection for peak
    if (!pulseOn && raw > threshold && (nowMs - lastPeakTime) > PEAK_MIN_GAP_MS) {
      if (lastPeakTime != 0) {
        unsigned long interval = nowMs - lastPeakTime;
        intervals[intervalIdx] = interval;
        intervalIdx = (intervalIdx + 1) % WINDOW_BEATS;
        if (intervalCount < WINDOW_BEATS) intervalCount++;
        // compute smoothed BPM (avg of intervals)
        unsigned long sumI = 0;
        for (int i = 0; i < intervalCount; i++) sumI += intervals[i];
        if (sumI > 0) {
          float avgInterval = sumI / (float)intervalCount;
          bpm = 60000.0f / avgInterval;
        }
      }
      lastPeakTime = nowMs;
      pulseOn = true;
      lastPulseEnd = nowMs + PULSE_MS;
      beatOut = 1;
    }

    // end pulse
    if (pulseOn && nowMs >= lastPulseEnd) {
      pulseOn = false;
      beatOut = 0;
    }

    // long gap -> reset bpm to avoid stale value
    if ((nowMs - lastPeakTime) > 5000) {
      bpm = 0.0;
      intervalCount = 0;
      intervalIdx = 0;
    }

    // output ECG line (per-sample)
    Serial.print("RAW ");
    Serial.print(raw);
    Serial.print(" BPM ");
    Serial.print((int)round(bpm));
    Serial.print(" Beat ");
    Serial.println(beatOut);
  }

  // ---------------- GSR sampling (~10Hz) ----------------
  unsigned long nowMillis = millis();
  if (nowMillis - lastGsrMillis >= (unsigned long)DELAY_MS_GSR) {
    lastGsrMillis = nowMillis;

    // oversampled read
    long s = 0;
    for (int i = 0; i < OVERSAMPLE; i++) {
      s += analogRead(GSR_PIN);
      delayMicroseconds(200);
    }
    int raw = s / OVERSAMPLE;

    // push into variance buffer
    rawBuffer[varIdx] = raw;
    varIdx = (varIdx + 1) % VAR_WINDOW;
    if (varCount < VAR_WINDOW) varCount++;

    // smoothing
    filtered_gsr = filtered_gsr * (1.0 - smoothFactor) + raw * smoothFactor;

    // very slow baseline
    baseline_gsr = baseline_gsr * 0.997 + filtered_gsr * 0.003;

    float signal = filtered_gsr - baseline_gsr;

    // compute stddev
    float mean = 0.0;
    for (int i = 0; i < varCount; i++) mean += rawBuffer[i];
    mean /= max(1, varCount);

    float varv = 0.0;
    for (int i = 0; i < varCount; i++) {
      float d = rawBuffer[i] - mean;
      varv += d * d;
    }
    varv /= max(1, varCount);
    float stddev = sqrt(varv);

    // contact detection
    int contact = 1;
    if (stddev < STDDEV_THRESHOLD && fabs(filtered_gsr - baseline_gsr) < DIFF_THRESHOLD) {
      contact = 0;
    }

    int outValue2 = (int)round(filtered_gsr);
    int outValue4 = (int)round(signal);

    Serial.print("Value2 ");
    Serial.print(outValue2);
    Serial.print(" Value4 ");
    Serial.print(outValue4);
    Serial.print(" Contact ");
    Serial.println(contact);
  }

  // loop non-blocking; small micro-delays are inside GSR oversampling only
}
    