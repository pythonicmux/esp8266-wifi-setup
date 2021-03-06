#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>

const IPAddress apIP(192, 168, 1, 1);
char apSSID[21]; //Monarch-Setup-chipid
const char *apPass = "birdbird";
boolean settingMode;
boolean needToReset = false;
String ssidList;

DNSServer dnsServer;
ESP8266WebServer webServer(80);
WiFiServer wifiServer(60);

void setup() {
  sprintf(apSSID, "%s%06x", "Monarch-Setup-", ESP.getChipId());
  Serial.begin(115200);
  EEPROM.begin(512);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(2,INPUT_PULLUP);
  delay(10);
  if (restoreConfig()) { //if stuff is written in eeprom
    if (checkConnection()) { //waiting for wifi connetion
      //if it did connect
      settingMode = false;
      //startWebServer(); 
      //NOW GO TO LOOP AND START UART PASSTHROUGH
      wifiServer.begin();
      Serial.print("Connected. Starting UART passthrough at ");
      Serial.print(WiFi.localIP());
      Serial.println(", port 60");
      return;
    }
  } //if no connection found then it's in setup mode
  settingMode = true;
  setupMode(); //makes soft access point and does web server w/ form
}

//echo client test stuff
int ledState = HIGH; //to blink led
unsigned long previousMillis = 0;
const long interval = 500;
const long resetInterval = 300;
WiFiClient client;

void loop() {
  //set up wifi 
  if (settingMode) {
    dnsServer.processNextRequest();
    webServer.handleClient();
    ledState = HIGH;
    digitalWrite(LED_BUILTIN, ledState);
    return;
  }
  //UART passthrough
  else{
    //if GPIO2 is pulled to LOW then
    //and restart- even if GPIO2 is LOW after it restarts,
    //it's fine because it won't check for it until connection 
    //to wifi is made, and since the memory is clear then
    //the user has to re-enter the WiFi credentials before it'll check.
    //Therefore, as long as GPIO2 isn't low when the user re-enters
    //the SSID and password, it should be fine.
    if((digitalRead(2) == LOW) || needToReset){
        Serial.println("memory reset");
        if(needToReset) return;
        needToReset = true;
        //reset memory and commit it to clear stored Wi-Fi data
        for (int i = 0; i < 96; ++i) {
          EEPROM.write(i, 0);
        }
        EEPROM.commit();
        unsigned long currentMillis = millis();
        if(currentMillis - previousMillis >= resetInterval){
          previousMillis = currentMillis;
          if(ledState == LOW){
            ledState = HIGH;
          }
          else{
            ledState = LOW;
          }
        }
        digitalWrite(LED_BUILTIN, ledState);
        ESP.restart();
    }
    //if connected to wifi and client
    if((client = wifiServer.available())){  
        Serial.println("Client connected");   
        //solid if client connected
        ledState = LOW;
        digitalWrite(LED_BUILTIN, ledState);
        while(client.connected()){
          while(client.available() > 0){
            char c = client.read();
            Serial.write(c);
            client.write(c);
          }
        }
        client.stop();
        Serial.println("Client disconnected normally");
    }
    //if not then blinking
    else if(WiFi.status() == WL_CONNECTED){
        //Serial.println("Client not connected");
        unsigned long currentMillis = millis();
        if(currentMillis - previousMillis >= interval){
          previousMillis = currentMillis;
          if(ledState == LOW){
            ledState = HIGH;
          }
          else{
            ledState = LOW;
          }
        }
        digitalWrite(LED_BUILTIN, ledState);
    }
    //not connected
    else{
      ledState = HIGH;
      digitalWrite(LED_BUILTIN, ledState);
      Serial.println("Wi-Fi disconnected");
      //restart
      setup();
    }
  }
}

boolean restoreConfig() {
  Serial.println("Reading EEPROM...");
  String ssid = "";
  String pass = "";
  if (EEPROM.read(0) != 0) {
    for (int i = 0; i < 32; ++i) {
      ssid += char(EEPROM.read(i));
    }
    Serial.print("SSID: ");
    Serial.println(ssid);
    for (int i = 32; i < 96; ++i) {
      pass += char(EEPROM.read(i));
    }
    Serial.print("Password: ");
    Serial.println(pass);
    WiFi.begin(ssid.c_str(), pass.c_str());
    return true;
  }
  else {
    Serial.println("Config not found.");
    return false;
  }
}

boolean checkConnection() {
  int count = 0;
  Serial.print("Waiting for Wi-Fi connection");
  while ( count < 30 ) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println();
      Serial.println("Connected!");
      return (true);
    }
    delay(500);
    Serial.print(".");
    count++;
  }
  Serial.println("Timed out.");
  return false;
}

void startWebServer() {
  if (settingMode) {
    Serial.print("Starting Web Server at ");
    Serial.println(WiFi.softAPIP());
    webServer.on("/settings", []() {
      String s = "<h1>Wi-Fi Settings</h1><p>Please enter your password by selecting the SSID.</p>";
      s += "<form method=\"get\" action=\"setap\"><label>SSID: </label><select name=\"ssid\">";
      s += ssidList;
      s += "</select><br>Password: <input name=\"pass\" length=64 type=\"password\"><input type=\"submit\"></form>";
      webServer.send(200, "text/html", makePage("Wi-Fi Settings", s));
    });
    webServer.on("/setap", []() {
      for (int i = 0; i < 96; ++i) {
        EEPROM.write(i, 0);
      }
      String ssid = urlDecode(webServer.arg("ssid"));
      Serial.print("SSID: ");
      Serial.println(ssid);
      String pass = urlDecode(webServer.arg("pass"));
      Serial.print("Password: ");
      Serial.println(pass);
      Serial.println("Writing SSID to EEPROM...");
      for (int i = 0; i < ssid.length(); ++i) {
        EEPROM.write(i, ssid[i]);
      }
      Serial.println("Writing Password to EEPROM...");
      for (int i = 0; i < pass.length(); ++i) {
        EEPROM.write(32 + i, pass[i]);
      }
      EEPROM.commit();
      Serial.println("Write EEPROM done!");
      String s = "<h1>Setup complete.</h1><p>device will be connected to \"";
      s += ssid;
      s += "\" after the restart.";
      webServer.send(200, "text/html", makePage("Wi-Fi Settings", s));
      //reset gpio2 pin before reset??
      //pinMode(2,INPUT);
      ESP.restart();
    });
    webServer.onNotFound([]() {
      String s = "<h1>AP mode</h1><p><a href=\"/settings\">Wi-Fi Settings</a></p>";
      webServer.send(200, "text/html", makePage("AP mode", s));
    });
  }
  //if it's set up
  else {
    return;
    Serial.print("Starting Web Server at ");
    Serial.println(WiFi.localIP());
    webServer.on("/", []() {
      String s = "<h1>STA mode</h1><p><a href=\"/reset\">Reset Wi-Fi Settings</a></p>";
      webServer.send(200, "text/html", makePage("STA mode", s));
    });
    webServer.on("/reset", []() {
      for (int i = 0; i < 96; ++i) {
        EEPROM.write(i, 0);
      }
      EEPROM.commit();
      String s = "<h1>Wi-Fi settings was reset.</h1><p>Please reset device.</p>";
      webServer.send(200, "text/html", makePage("Reset Wi-Fi Settings", s));
    });
  }
  webServer.begin();
}

void setupMode() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  delay(100);
  Serial.println("");
  for (int i = 0; i < n; ++i) {
    ssidList += "<option value=\"";
    ssidList += WiFi.SSID(i);
    ssidList += "\">";
    ssidList += WiFi.SSID(i);
    ssidList += "</option>";
  }
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSSID, apPass);
  dnsServer.start(53, "*", apIP);
  startWebServer();
  Serial.print("Starting Access Point at \"");
  Serial.print(apSSID);
  Serial.println("\"");
  Serial.print("Password is \"");
  Serial.print(apPass);
  Serial.println("\"");
}

String makePage(String title, String contents) {
  String s = "<!DOCTYPE html><html><head>";
  s += "<meta name=\"viewport\" content=\"width=device-width,user-scalable=0\">";
  s += "<title>";
  s += title;
  s += "</title></head><body>";
  s += contents;
  s += "</body></html>";
  return s;
}

String urlDecode(String input) {
  String s = input;
  s.replace("%20", " ");
  s.replace("+", " ");
  s.replace("%21", "!");
  s.replace("%22", "\"");
  s.replace("%23", "#");
  s.replace("%24", "$");
  s.replace("%25", "%");
  s.replace("%26", "&");
  s.replace("%27", "\'");
  s.replace("%28", "(");
  s.replace("%29", ")");
  s.replace("%30", "*");
  s.replace("%31", "+");
  s.replace("%2C", ",");
  s.replace("%2E", ".");
  s.replace("%2F", "/");
  s.replace("%2C", ",");
  s.replace("%3A", ":");
  s.replace("%3A", ";");
  s.replace("%3C", "<");
  s.replace("%3D", "=");
  s.replace("%3E", ">");
  s.replace("%3F", "?");
  s.replace("%40", "@");
  s.replace("%5B", "[");
  s.replace("%5C", "\\");
  s.replace("%5D", "]");
  s.replace("%5E", "^");
  s.replace("%5F", "-");
  s.replace("%60", "`");
  return s;
}

