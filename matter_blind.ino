#include <Matter.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <index_html.h>
//#include <AsyncWebSocket.h>

#define RELAY_UP_PIN     33
#define RELAY_DOWN_PIN   32
#define PULSE_PIN        4   // input-only pin, safe for interrupts

// List of Matter Endpoints for this Node
// Window Covering Endpoint
MatterWindowCovering WindowBlinds;

const char* ssid = "Hungster2";
const char* password = "hungster";
const char *liftPercentPrefKey = "LiftPercent";

const uint32_t PULSE_DEBOUNCE_US = 400;
//const uint32_t POSITION_SAVE_INTERVAL_MS = 10;

enum BlindState {
  STOPPED,
  MOVING_UP,
  MOVING_DOWN
};

struct BlindData {
  int current;
  int max;
};

volatile int pulseCount = 0;
volatile uint32_t lastPulseMillis = 0;
volatile int8_t pulseDirection = 0;

BlindState blindState = STOPPED;
BlindData dablind;

int targetPosition = -1;
uint32_t lastSaveMillis = 0;
int lastSavedCurrent = -1;
int32_t lastSavedMax     = -1;
uint currentLiftPercent = 0;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences bprefs;


void IRAM_ATTR pulseISR() {
  uint32_t now = millis();
  if ((now - lastPulseMillis) > PULSE_DEBOUNCE_US) {
    if (pulseDirection != 0) {
      pulseCount += pulseDirection;
    }
    lastPulseMillis = now;
  }
}

void loadPosition() {
  bprefs.begin("urblind", false);
  dablind.current = bprefs.getInt("current", 1);
  dablind.max     = bprefs.getInt("max", 11);
  bprefs.end();

  pulseCount = dablind.current;

  lastSavedCurrent = dablind.current;
  lastSavedMax     = dablind.max;
  Serial.print("load"); Serial.print(dablind.current); Serial.print(","); Serial.println(dablind.max);
//  Serial.println("load", blind.current, " ", blind.max);
}

void savePositionIfChanged(bool force = false) {
  bool changed =
    (pulseCount != lastSavedCurrent) ||
    (dablind.max  != lastSavedMax);

  if (!changed && !force) return;

  bprefs.begin("urblind", false);
  bprefs.putInt("current", pulseCount);
  bprefs.putInt("max", dablind.max);
  int current2 = bprefs.getInt("current");
  int max2     = bprefs.getInt("max");
  bprefs.end();

  Serial.print("save"); Serial.print(current2); Serial.print(","); Serial.println(max2);
//  Serial.println("save", pulseCount, " ", blind.max);
  lastSavedCurrent = pulseCount;
  lastSavedMax     = dablind.max;
}

bool motorStop() {
  digitalWrite(RELAY_UP_PIN, HIGH);
  digitalWrite(RELAY_DOWN_PIN, HIGH);
  pulseDirection = 0;
  blindState = STOPPED;
  WindowBlinds.setOperationalState(MatterWindowCovering::LIFT, MatterWindowCovering::STALL);

  uint currentPercent = pulseCount / dablind.max;
//  matterPref.putUChar(liftPercentPrefKey, currentPercent);

  savePositionIfChanged(true);
  return true;
}

void motorUp() {
  motorStop();
  Serial.print("motorUp");
  WindowBlinds.setOperationalState(MatterWindowCovering::LIFT, MatterWindowCovering::MOVING_UP_OR_OPEN);
  digitalWrite(RELAY_UP_PIN, LOW);
  pulseDirection = -1;
  blindState = MOVING_UP;
}

void motorDown() {
  motorStop();
  Serial.print("motorDown");
  WindowBlinds.setOperationalState(MatterWindowCovering::LIFT, MatterWindowCovering::MOVING_DOWN_OR_CLOSE);
  digitalWrite(RELAY_DOWN_PIN, LOW);
  pulseDirection = +1;
  blindState = MOVING_DOWN;
}

void moveTo(int target) {
//  if (blind.max <= 0) return;

  //if (target < 0) target = 0;
  //if (target > blind.max) target = blind.max;

  targetPosition = target;

  if (pulseCount < targetPosition) {
    motorDown();
  } else if (pulseCount > targetPosition) {
    motorUp();
  } else {
    motorStop();
  }
}

void stepMove(int32_t delta) {
  int32_t target = pulseCount + delta;
//  Serial.print(" tryMove ");
  moveTo(target);
}

void updateMotion() {
  if (blindState == STOPPED || targetPosition < 0) return;

 
  if (blindState == MOVING_DOWN && pulseCount >= targetPosition) {
    motorStop();
  }

  if (blindState == MOVING_UP && pulseCount <= targetPosition) {
    motorStop();
  }

  currentLiftPercent = pulseCount / dablind.max;
  WindowBlinds.setLiftPercentage(currentLiftPercent);


}

void updatePersistence() {
/*  if ((millis() - lastSaveMillis) < POSITION_SAVE_INTERVAL_MS)
    return;
  lastSaveMillis = millis(); */
  savePositionIfChanged(false);
}


// Window Covering Callbacks
bool fullOpen() {
  moveTo(0);
  return true;
}

bool fullClose() {
  moveTo(dablind.max);
  return true;
}

// Simple callback - handles window Lift change request
bool goToLiftPercentage(uint8_t liftPercent) {
  Serial.printf("goToLiftPercentage: Lift=%d%%\r\n", liftPercent);
  int32_t newTarget = (liftPercent * dablind.max) / 100;
  moveTo(newTarget);  // your function
  // Returning true will store the new Lift value into the Matter Cluster
  return true;
}






void setupWeb() {

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/top", HTTP_GET, [](AsyncWebServerRequest *req){
    Serial.print("top");
    moveTo(0);
    req->send(200, "text/plain", "OK");
  });

  server.on("/bottom", HTTP_GET, [](AsyncWebServerRequest *req){
    moveTo(dablind.max);
    req->send(200, "text/plain", "OK");
  });

  server.on("/stop", HTTP_GET, [](AsyncWebServerRequest *req){
    motorStop();
//    savePosition();
    req->send(200, "text/plain", "OK");
  });

server.on("/stepUp", HTTP_GET, [](AsyncWebServerRequest *req){
  Serial.print("stepUp");
  stepMove(-6);
  req->send(200, "text/plain", "OK");
});

server.on("/stepDown", HTTP_GET, [](AsyncWebServerRequest *req){
  Serial.print("stepDown");
  stepMove(6);
  req->send(200, "text/plain", "OK");
});

  server.on("/setTop", HTTP_GET, [](AsyncWebServerRequest *req){
    dablind.max = dablind.max - pulseCount;
    dablind.max = max<int32_t>(dablind.max, 0);
    pulseCount = 0;
    savePositionIfChanged(false);
    req->send(200, "text/plain", "UP SET");
  });

  server.on("/setBott", HTTP_GET, [](AsyncWebServerRequest *req){
    dablind.max = pulseCount;
    savePositionIfChanged(false);
    req->send(200, "text/plain", "DOWN SET");
  });
  
  server.on("/pos", HTTP_GET, [](AsyncWebServerRequest *req){
    int percent = dablind.max > 0 ? (pulseCount * 100) / dablind.max : 0;
   
    String json = "{";
    json += "\"current\":" + String(pulseCount) + ",";
    json += "\"max\":" + String(dablind.max) + ",";
    json += "\"percent\":" + String(percent) + ",";
    json += "\"state\":\"";

    if (blindState == MOVING_UP) json += "up";
    else if (blindState == MOVING_DOWN) json += "down";
    else json += "stopped";

    json += "\"}";
    req->send(200, "application/json", json);
  });

  server.begin();
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    String json = String("{\"current\":") + pulseCount +
                  ",\"max\":" + dablind.max +
                  ",\"percent\":" +
                  String(dablind.max > 0 ? (pulseCount*100)/dablind.max : 0) +
                  "}";
    client->text(json);
  }
}

void setupWebSocket() {
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
}

void broadcastStatus() {
  String json = String("{\"current\":") + pulseCount +
                ",\"max\":" + dablind.max +
                ",\"percent\":" +
                String(dablind.max > 0 ? (pulseCount*100)/dablind.max : 0) +
                "}";
  ws.textAll(json);
}

void setup() {
  pinMode(RELAY_UP_PIN, OUTPUT);
  pinMode(RELAY_DOWN_PIN, OUTPUT);
  
  pinMode(PULSE_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PULSE_PIN), pulseISR, FALLING);

  Serial.begin(115200);

  loadPosition();
  motorStop();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(100);
  Serial.print("Connected ESP IP: "); Serial.println(WiFi.localIP());

  setupWeb();
  setupWebSocket();

  WindowBlinds.begin(100, 0, MatterWindowCovering::ROLLERSHADE);
  
  // Set up the onGoToLiftPercentage callback - this handles all window covering changes requested by the Matter Controller
  
  WindowBlinds.onOpen(fullOpen);
  WindowBlinds.onClose(fullClose);
  WindowBlinds.onGoToLiftPercentage(goToLiftPercentage);
  WindowBlinds.onStop(motorStop);
  
  // Start Matter
  Matter.begin();
  Serial.println("Matter started");
  Serial.println();

  // Print commissioning information
  Serial.println("========================================");
  Serial.println("Matter Node is not commissioned yet.");
  Serial.println("Initiate the device discovery in your Matter environment.");
  Serial.println("Commission it to your Matter hub with the manual pairing code or QR code");
  Serial.printf("Manual pairing code: %s\r\n", Matter.getManualPairingCode().c_str());
  Serial.printf("QR code URL: %s\r\n", Matter.getOnboardingQRCodeUrl().c_str());
  Serial.println("========================================");
  
}

void loop() {
  updateMotion();
//  updatePersistence();
  ws.cleanupClients();
  if (Matter.isDeviceCommissioned()) {
    Serial.printf("Initial state: Lift=%d%%, Tilt=%d%%\r\n", WindowBlinds.getLiftPercentage());
    // Update visualization based on initial state
//    visualizeWindowBlinds(WindowBlinds.getLiftPercentage());
    Serial.println("Matter Node is commissioned and connected to the network. Ready for use.");
  }


}
