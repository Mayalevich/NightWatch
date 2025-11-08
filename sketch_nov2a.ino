// UNO R4 Minima — Sleep Monitor (raw stream for backend processing)
// Pins: Head=A3, Body=A4, Leg=A5, Sound=A0, Light=A1, Temp=A2, Button=D2 (to GND)

const uint8_t PIN_PIEZO[3] = {A3, A4, A5};
const uint8_t PIN_SOUND    = A0;
const uint8_t PIN_LIGHT    = A1;
const uint8_t PIN_TEMP     = A2;
const uint8_t PIN_BUTTON   = 2;

const int RAW_OVERSAMPLE_PIEZO = 32;   // light smoothing without complex DSP
const int RAW_OVERSAMPLE_OTHER = 16;
const unsigned long SAMPLE_INTERVAL_MS = 100; // ~10 Hz stream for backend

unsigned long lastSampleMs = 0;

int readOversampled(uint8_t pin, int count) {
  long acc = 0;
  for (int i = 0; i < count; i++) {
    acc += analogRead(pin);
    delayMicroseconds(150);
  }
  return (int)(acc / count);
}

void printHeader() {
  Serial.println(F("time_ms,head_raw,body_raw,leg_raw,sound_raw,light_raw,temp_raw,button_raw"));
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  delay(80);
  lastSampleMs = millis();
  printHeader();
}

void loop() {
  unsigned long now = millis();
  if (now - lastSampleMs < SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleMs += SAMPLE_INTERVAL_MS;

  int head = readOversampled(PIN_PIEZO[0], RAW_OVERSAMPLE_PIEZO);
  int body = readOversampled(PIN_PIEZO[1], RAW_OVERSAMPLE_PIEZO);
  int leg  = readOversampled(PIN_PIEZO[2], RAW_OVERSAMPLE_PIEZO);
  int sound = readOversampled(PIN_SOUND, RAW_OVERSAMPLE_PIEZO);
  int light = readOversampled(PIN_LIGHT, RAW_OVERSAMPLE_OTHER);
  int temp  = readOversampled(PIN_TEMP, RAW_OVERSAMPLE_OTHER);
  int button = digitalRead(PIN_BUTTON); // HIGH = released, LOW = pressed
 
  char line[96];
  int n = snprintf(line, sizeof(line), "%lu,%d,%d,%d,%d,%d,%d,%d",
                   now, head, body, leg, sound, light, temp, button);
  if (n > 0) {
    if (n >= (int)sizeof(line)) {
      line[sizeof(line) - 1] = '\0';
    }
    Serial.println(line);
  }
}
