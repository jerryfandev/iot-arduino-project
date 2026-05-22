// Finalized Active-High Lonely Binary Switch Test
// Wiring: Black->GND, Red->3V3, White->GPIO 6, Yellow->NC (Disconnected)

constexpr int kButtonPin = 6;
static int lastState = -1;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[Button] Active-High GPIO 6 Checker Online");

  // Configured as a standard INPUT. 
  // The "Active High" hardware on the PCB prevents pin floating.
  pinMode(kButtonPin, INPUT);
}

void loop() {
  // Active High Logic:
  // Idle state    -> Reads LOW (0) natively via board layout
  // Pressed state -> Reads HIGH (1) driven by button compression
  int rawState = digitalRead(kButtonPin);
  int logicalState = (rawState == HIGH) ? 1 : 0;

  if (logicalState != lastState) {
    lastState = logicalState;
    Serial.print("[Button] Trigger Detected - Logical State: ");
    Serial.println(logicalState);
  }
}