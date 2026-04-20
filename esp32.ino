#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- CONFIGURATION ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* beszelIp = "YOUR_SERVER_IP"; 
const char* beszelEmail = "YOUR_API_EMAIL";
const char* beszelPassword = "YOUR_API_PASSWORD";
const char* systemName = "YOUR_SYSTEM_NAME"; 

// --- LED PINS ---
const int ledRed = 25;
const int ledYellow = 26;
const int ledGreen = 27;
const int ledBlue = 33;

// --- GLOBAL VARIABLES ---
String authToken = ""; 
unsigned long lastScreenChange = 0;
unsigned long lastDataUpdate = 0;
int currentScreen = 0;
float cpuUsage=0, cpuTemp=0, ramUsage=0, diskUsage=0, netDown=0, netUp=0, gpuUsage=0, gpuTemp=0;
bool gpuActive = false;
bool serverOnline = false;

// --- LED CONTROL ---
void updateLEDs() {
  if (WiFi.status() != WL_CONNECTED || !serverOnline) {
    digitalWrite(ledGreen, LOW);
    digitalWrite(ledYellow, LOW);
    digitalWrite(ledRed, HIGH);
    digitalWrite(ledBlue, LOW); 
    return;
  }

  if (cpuUsage >= 85.0 || ramUsage >= 7.0) {
    digitalWrite(ledGreen, LOW);
    digitalWrite(ledYellow, LOW);
    digitalWrite(ledRed, HIGH); 
  } 
  else if (cpuUsage >= 50.0 || ramUsage >= 5.5) {
    digitalWrite(ledGreen, LOW);
    digitalWrite(ledYellow, HIGH); 
    digitalWrite(ledRed, LOW);
  } 
  else {
    digitalWrite(ledGreen, HIGH); 
    digitalWrite(ledYellow, LOW);
    digitalWrite(ledRed, LOW);
  }

  if (gpuActive) {
    digitalWrite(ledBlue, HIGH);
  } else {
    digitalWrite(ledBlue, LOW);
  }
}

// --- DATA FETCHING ---
void fetchData() {
  // Free heap monitor to prevent fragmentation freeze
  if (ESP.getFreeHeap() < 20000) {
    Serial.println("Critical memory low! Triggering auto-restart...");
    delay(1000);
    ESP.restart();
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    serverOnline = false;
    updateLEDs();
    return;
  }
  
  HTTPClient http;
  
  // Connection timeout limit to prevent hanging
  http.setTimeout(3000); 

  if (authToken == "") {
    http.begin("http://" + String(beszelIp) + ":8090/api/collections/users/auth-with-password");
    http.addHeader("Content-Type", "application/json");
    String payload = "{\"identity\":\"" + String(beszelEmail) + "\",\"password\":\"" + String(beszelPassword) + "\"}";
    if (http.POST(payload) == 200) {
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, http.getString());
      authToken = doc["token"].as<String>();
    }
    http.end();
  }

  String url = "http://" + String(beszelIp) + ":8090/api/collections/system_stats/records?sort=-created&perPage=1&filter=(system.name='" + String(systemName) + "')";
  http.begin(url);
  http.addHeader("Authorization", "Bearer " + authToken); 

  if (http.GET() == 200) {
    serverOnline = true; 
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, http.getString());
    
    if (doc["items"].size() > 0) {
      JsonObject s = doc["items"][0]["stats"];
      
      cpuUsage = s["cpu"];
      cpuTemp = s["t"]["k10temp"]; 
      ramUsage = s["mu"]; 
      diskUsage = s["dp"]; 
      
      netDown = s["b"][1].as<float>() / 1024.0 / 1024.0;
      netUp = s["b"][0].as<float>() / 1024.0 / 1024.0;

      if (s.containsKey("g") && s["g"].containsKey("0")) {
        gpuUsage = s["g"]["0"]["u"];
        gpuTemp = s["t"]["GeForce GTX 1050 Ti"];
        gpuActive = (gpuUsage > 2.0);
      }
    }
  } else {
    serverOnline = false;
    authToken = ""; 
  }
  http.end();
  
  updateLEDs(); 
}

// --- DISPLAY UPDATES ---
void drawScreen() {
  display.clearDisplay();
  
  if (!serverOnline) {
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 25);
    display.print("OFFLINE");
    display.display();
    return;
  }

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  if (currentScreen == 0)      display.print("CPU STATUS");
  else if (currentScreen == 1) display.print("RAM (7.8GB MAX)");
  else if (currentScreen == 2) display.print("DISK USAGE");
  else if (currentScreen == 3) display.print("NETWORK MB/s");
  else if (currentScreen == 4) display.print("GPU 1050 Ti");

  display.setTextSize(2);
  display.setCursor(0, 22);

  if (currentScreen == 0) {
    display.print(cpuUsage, 1); display.print("%");
    display.setTextSize(1); display.setCursor(0, 48);
    display.print("Temp: "); display.print(cpuTemp, 1); display.print(" C");
  } 
  else if (currentScreen == 1) {
    display.print(ramUsage, 1); display.print(" GB");
    display.fillRect(0, 48, map(constrain(ramUsage*10,0,78),0,78,0,128), 8, SSD1306_WHITE);
  }
  else if (currentScreen == 2) {
    display.print(diskUsage, 1); display.print("%");
    display.fillRect(0, 48, map(constrain(diskUsage,0,100),0,100,0,128), 8, SSD1306_WHITE);
  }
  else if (currentScreen == 3) {
    display.setTextSize(1);
    display.setCursor(0, 25); display.print("DL: "); display.print(netDown, 2);
    display.setCursor(0, 45); display.print("UP: "); display.print(netUp, 2);
  }
  else if (currentScreen == 4) {
    display.setTextSize(1); display.print(gpuActive ? "ACTIVE" : "IDLE");
    display.setCursor(0, 35); display.print("Load: "); display.print(gpuUsage, 1); display.print("%");
    display.setCursor(0, 50); display.print("Temp: "); display.print(gpuTemp, 1); display.print(" C");
  }
  display.display();
}

void setup() {
  Serial.begin(115200);
  
  pinMode(ledRed, OUTPUT);
  pinMode(ledYellow, OUTPUT);
  pinMode(ledGreen, OUTPUT);
  pinMode(ledBlue, OUTPUT);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { for(;;); }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
  
  fetchData();
}

void loop() {
  // Non-blocking Wi-Fi reconnection routine
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
    WiFi.reconnect();
    unsigned long startTimeout = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTimeout < 3000)) {
      delay(100);
    }
  }

  unsigned long now = millis();
  if (now - lastDataUpdate >= 10000) { lastDataUpdate = now; fetchData(); }
  if (now - lastScreenChange >= 5000) { lastScreenChange = now; currentScreen++; if(currentScreen>4)currentScreen=0; drawScreen(); }
}
