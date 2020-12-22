#include <FS.h> //this needs to be first, or it all crashes and burns...

#if defined(ESP8266)
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <ESPAsyncTCP.h>
#else
#include <WiFi.h>
#endif

//needed for library
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>  //https://github.com/tzapu/WiFiManager
#include "ESPDateTime.h"
#include "SSD1306Wire.h"
#include <SPI.h>
#include <ArduinoJson.h>
#define ASYNC_HTTP_DEBUG_PORT     Serial
#define _ASYNC_HTTP_LOGLEVEL_ 1
#include <AsyncHTTPRequest_Generic.h>  // https://github.com/khoih-prog/AsyncHTTPRequest_Generic
#include <Ticker.h>

/* If I was less lazy the NTP would be a config setting.  I'm not. */
#define APEX_HOST "phoenix.garbled.net"
#define NTP_SERVER "ntp.garbled.net"
#define NTP_TZ -7
#define APEX_MOONDEV "VAR_ESPMoon"
#define KNOWN_NEW_MOON 1592721660 /* 06:41 6/21/2020 UTC */
#define LUNAR_CYCLE 29.530588853
#define MAX_ILLUM 0.30 /* Max illumination of the controller */
#define MAX_ILLUM_STR "0.30"
#define MIN_POT_RES 5 /* Minimum pot resistance  (25 is 10%?)*/
#define MIN_POT_RES_STR "5"
#define APEX_POLL 180 /* in seconds */
#define APEX_POLL_STR "180"
#define JSON_CONFIG_FILE_SIZE 1024
#define JSON_APEX_SIZE 1024
#define FALLBACK_INTENSITY 100


AsyncWebServer server(80);
DNSServer dns;
SSD1306Wire display(0x3C, D2, D1);
AsyncHTTPRequest request;
Ticker ticker;

int _debug = 0;

/***********************MCP42XXX Commands************************/
const int CS_PIN = D8;
//potentiometer select byte
const int POT0_SEL = 0x11;
const int POT1_SEL = 0x12;
const int BOTH_POT_SEL = 0x13;

//shutdown the device to put it into power-saving mode.
//In this mode, terminal A is open-circuited and the B and W terminals are shorted together.
//send new command and value to exit shutdowm mode.
const int POT0_SHUTDOWN = 0x21;
const int POT1_SHUTDOWN = 0x22;
const int BOTH_POT_SHUTDOWN = 0x23;

bool restartRequired = false;
String wifi_info;
unsigned long ms = millis();
int apex_illumination = FALLBACK_INTENSITY;

String Apex_Json = "";
int json_request_complete = 0;

/* Variables saved to config file */
char str_apex_host[70] = APEX_HOST;
char str_max_illum[8] = MAX_ILLUM_STR;
char str_min_pot_res[8] = MIN_POT_RES_STR;
char str_apex_moondev[20] = APEX_MOONDEV;
char str_apex_poll[8] = APEX_POLL_STR;

bool shouldSaveConfig = false;
float max_illum = MAX_ILLUM;
int min_pot_res = MIN_POT_RES;
int json_bad = 0;
int apex_poll = APEX_POLL;
float cur_ill = 0.0;

/*
    GPIO12: MISO D6
    GPIO13: MOSI D7
    GPIO14: SCLK D5
    GPIO15: CS D8

    GPIO5: SCL  D1
    GPIO5: SDA  D2
*/

/* Tell the main loop to save the config */
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

/* Connect to NTP and tell it to update itself */
void setupDateTime() {
  DateTime.setTimeZone(NTP_TZ);
  DateTime.setServer(NTP_SERVER);
  DateTime.begin(15 * 1000);
  if (!DateTime.isTimeValid()) {
    Serial.println("Failed to get time from server.");
  }
}

/* Calculate the lunar illumination and update the OLED */
float get_lunar_illumination() {
  unsigned long sec_since_known;
  float ill, f, days_since_known, cycles_since_known;
  
  if (!DateTime.isTimeValid()) {
      Serial.println("Failed to get time from server, retry.");
      DateTime.begin();
  }
  if (!DateTime.isTimeValid()) {
    return(0.2); /* safety */
  }

  time_t t = DateTime.now();
  sec_since_known = (unsigned long)t - KNOWN_NEW_MOON;
  if (_debug)
    Serial.printf("Sec since known = %d\n", sec_since_known);

  days_since_known = sec_since_known / 86400.0;
  if (_debug)
    Serial.printf("Days since known = %f\n", days_since_known);

  cycles_since_known = days_since_known / LUNAR_CYCLE;
  if (_debug)
    Serial.printf("Cycles since known = %f\n", cycles_since_known);

  /* Knock off the left of the decimal */
  f = cycles_since_known - (int)cycles_since_known;

  /* In the first half of the cycle, the moon becomes more full, then
     it hits full, and moves to new, so maximum illumination is halfway. */
  if (f <= 0.5) {
    ill = (f / 0.5);
  } else {
    ill = (1.0 - f) / 0.5;
  }

  /* Just update the OLED here, because it's easier */
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, DateTime.toString().c_str());
  display.drawProgressBar(0, 25, 120, 10, (int)(f*100));
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 14, "Phase " + String((int)(f*100)) + "%");
  display.drawProgressBar(0, 46, 120, 10, (int)(ill*100));
  display.drawString(64, 35, "Illumination " + String((int)(ill*100)) + "%");
  display.display();

  return(ill);
}

/* 404 */
void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

/* The index.html, and form */
AsyncResponseStream *index_html(AsyncWebServerRequest *request) {
  AsyncResponseStream *response = request->beginResponseStream("text/html");
  
  response->addHeader("Server","ESP Moonlight Control Server");
  response->print("<!DOCTYPE html><html><head>"
		  "<style>\n"
		  "input:invalid {\n"
		  "  border: 3px solid red;\n"
		  "}\n"
		  "html { background-color: black; color: whitesmoke; }\n"
		  "</style>"
		  "<title>Moonlight Controller</title></head><body>\n");

  response->print("<h2>Moonlight Controller</h2>\n");
  response->print("<a href=/info>System Information</a><br>");
  //response->printf("<p>Epoch Time: %ld</p>", DateTime.format("%s").c_str());
  response->printf("<p>Moon Illumination: %f</p>\n", cur_ill);

  /* The form */
  response->print("<form accept-charset=\"UTF-8\" action=\"/post\" autocomplete=\"off\" method=\"POST\">\n");
  response->print("<label for=\"apex_host\">Apex Hostname or IP</label><br />\n");
  response->printf("<input name=\"apex_host\" type=\"text\" value=\"%s\" /> <br />\n", str_apex_host);
  response->print("<label for=\"max_illum\">Max Illumination (0.0-1.0)</label><br />\n");
  response->printf("<input name=\"max_illum\" type=\"text\" value=\"%f\" "
		   "pattern=\"[.0-9]+\"/> <br />\n", max_illum);
  response->print("<label for=\"min_pot_res\">Minimum POT Resistance in kOhm</label><br />\n");
  response->printf("<input name=\"min_pot_res\" type=\"number\" value=\"%d\" "
		   "min=0 max=100> <br />\n", min_pot_res);

  response->printf("<label for=\"apex_moondev\">Apex MoonDevice</label><br />\n");
  response->printf("<input name=\"apex_moondev\" type=\"text\" value=\"%s\" /> <br />\n", str_apex_moondev);

  response->print("<label for=\"apex_poll\">Apex Poll in seconds</label><br />\n");
  response->printf("<input name=\"apex_poll\" type=\"number\" value=\"%d\" "
		   "min=10 max=1800> <br />\n", apex_poll);

  response->print("<input type=submit>");
  response->print("</form>\n");
  response->print("</body></html>");

  return(response);
}

/* A simple info page */
AsyncResponseStream *info_html(AsyncWebServerRequest *request) {
  int pota = 0;
  AsyncResponseStream *response = request->beginResponseStream("text/html");

  response->addHeader("Server","ESP Async Web Server");
  response->printf("<!DOCTYPE html><html><head><title>Webpage at %s</title></head><body>", request->url().c_str());

  response->print(wifi_info.c_str());

  response->printf("<p>Time: %s</p>", DateTime.toString().c_str());
  response->printf("<p>Moon Illumination: %f</p>", cur_ill);

  response->printf("<p>Apex Host: %s</p>", str_apex_host);
  response->printf("<p>Max Illumination: %f</p>", max_illum);
  response->printf("<p>Minimum POT Resistance: %d</p>", min_pot_res);
  response->printf("<p>Apex Moon device: %s</p>", str_apex_moondev);
  response->printf("<p>Apex Poll Time: %d</p>", apex_poll);
  response->printf("<p>apex_illumination: %d</p>", apex_illumination);
  pota = (int)(cur_ill * max_illum * 255.0 * (apex_illumination / 100.0));
  response->printf("<p>Pot A before min: %d</p>", pota);
  if (pota < (min_pot_res / 100.0) * 255.0)
      pota = (min_pot_res / 100.0) * 255.0;
  response->printf("<p>Pot A after min: %d</p>", pota);
  response->printf("<p>Final Ohm Value: %f</p>",
		   cur_ill * max_illum * (apex_illumination / 100.0) * 100.0);
  
  response->print("<p>To update image: curl -F \"image=@filename\" IP_ADDR/update</p>");
  response->print("</body></html>");

  return(response);
}

/* Parse a config file */
DynamicJsonDocument parse_json_conf(char *filename)
{
  DynamicJsonDocument json_doc(JSON_CONFIG_FILE_SIZE);
  DeserializationError j_error;
  File configFile;

  json_bad = 0;
  if (SPIFFS.begin()) {
    if (SPIFFS.exists(filename)) {
      configFile = SPIFFS.open(filename, "r");

      Serial.printf("reading config file %s\n", filename);
      if (configFile) {
	j_error = deserializeJson(json_doc, configFile);
	if (j_error) {
	  Serial.printf("failed to load json config %s", filename);
	  json_bad = 1;
	}
	Serial.println("Read config file");
	configFile.close();
      } else {
	Serial.println("Cannot open config file for reading");
	json_bad = 1;
      }
    } else {
      Serial.println("No config file found");
      json_bad = 1;
    }
  } else {
    Serial.println("failed to mount FS");
    json_bad = 1;
  }
  return(json_doc);
}

/* Save a config file */
void save_config_to_fs()
{
  DynamicJsonDocument json_doc(JSON_CONFIG_FILE_SIZE);
  char *wtf;
  
  Serial.println("saving config");

  wtf = strdup(str_apex_host);
  
  json_doc["apex_host"] = wtf;
  
  sprintf(str_max_illum, "%f", max_illum);
  json_doc["max_illum"] = str_max_illum;
  
  sprintf(str_min_pot_res, "%d", min_pot_res);
  json_doc["min_pot_res"] = str_min_pot_res;
  
  json_doc["apex_moondev"] = str_apex_moondev;
  
  sprintf(str_apex_poll, "%d", apex_poll);
  json_doc["apex_poll"] = str_apex_poll;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("failed to open config file for writing");
  }

  serializeJson(json_doc, Serial);
  Serial.println();
  serializeJson(json_doc, configFile);
  configFile.close();
  shouldSaveConfig = false;

  /* I have no idea why this is happening, probably a stack smash */
  sprintf(str_apex_host, "%s", wtf);
}

/* Write data to the digipot */
void DigitalPotWrite(int cmd, int val)
{
  // constrain input value within 0 - 255
  val = constrain(val, 0, 255);
  // set the CS pin to low to select the chip:
  digitalWrite(CS_PIN, LOW);
  // send the command and value via SPI:
  SPI.transfer(cmd);
  SPI.transfer(val);
  // Set the CS pin high to execute the command:
  digitalWrite(CS_PIN, HIGH);
}

/*********** Apex Code *************/

/* Ask the apex for it's status */
void sendApexRequest() 
{
  char url[150];

  sprintf(url, "http://%s/cgi-bin/status.json", str_apex_host);
  Serial.printf("Sending req to %s\n", url);

  /* Clear the buffer */
  Apex_Json = "";
  json_request_complete = 0;

  if (request.readyState() == readyStateUnsent || request.readyState() == readyStateDone) {
    request.setDebug(false);
    request.open("GET", url);
    request.send();
  } else {
    Serial.println("Can't send request");
  }
}

/* We have to use the data callback here, not the ready callback.
   The ready callback gets called when there is only partial data.
   Therefore, we have to do this stupid dance, where we continually append
   data to the internal buffer.
   We work around the low memory, by constantly trying to find the device
   we are looking for, and cutting the buffer down as we go, until we
   finally find it.  Then we hack out just that section of the json,
   and hand only that to ArduinoJson.  The apex typically returns in the range
   of about 15k, which is just too much.
*/
void ApexDataCB(void* optParm, AsyncHTTPRequest* request, size_t avail)
{
  DynamicJsonDocument apex_doc(JSON_APEX_SIZE);
  DeserializationError j_error;
  int i, j, stridx, strstart, strend;
  String discard, hold_apex;

  if (json_request_complete) {
    /* Calling the responseText clears the internal buffer out of the lib */
    discard = request->responseText();
    return;
  }

  //Serial.println("In ApexDataCB");
  Apex_Json += request->responseText();

  /* Try to find the moon device in the current buffer */
  stridx = Apex_Json.indexOf(str_apex_moondev);
  if (stridx != -1) {
    if (_debug)
      Serial.printf("found Moon Device at %d\n", stridx);
    /* look for closing brace */
    strend = Apex_Json.indexOf("}", stridx);
    if (strend == -1)
      return;
    if (_debug)
      Serial.println(Apex_Json.substring(stridx-5, strend+1));
    /* look backwards from the device name for the open brace */
    strstart = Apex_Json.indexOf("{", stridx-60);
    if (strstart == -1)
      return;
    if (_debug)
      Serial.println(Apex_Json.substring(strstart, strend+1));
    /* Hand it to the json procesor */
    j_error = deserializeJson(apex_doc, Apex_Json.substring(strstart, strend+1));
    if (_debug)
      Serial.printf("Error = %s\n", j_error.c_str());
    if (!j_error) {
      json_request_complete = 1;
      apex_illumination = apex_doc["intensity"];
      Serial.printf("Set intensity to %d\n", apex_illumination);
    } else
      return;
  } else {
    /* Carefully manage the memory on the ESP */
    hold_apex = Apex_Json;
    i = hold_apex.indexOf("{");
    while (i != -1) {
      j = i+1;
      if (j >= hold_apex.length())
	j = -1;
      else {
	i = hold_apex.indexOf("{", j);
      }
    }
    if (j != -1)
      Apex_Json = hold_apex.substring(j, hold_apex.length());
    else
      Apex_Json = hold_apex;
  }
}

/* The setup */
void setup() {
  Serial.begin(115200);

  /* First reset the pot to 0 */
  pinMode(CS_PIN, OUTPUT);   // set the CS_PIN as an output:
  SPI.begin();     // initialize SPI:
  DigitalPotWrite(POT0_SEL, 0);

  /* Init the display, setup basic message */
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, "Initializing");
  display.display();

  /* Read config from FS */

  DynamicJsonDocument json_doc = parse_json_conf("/config.json");
  if (!json_bad) {
    Serial.println("parsed json");

    strlcpy(str_apex_host, json_doc["apex_host"] | APEX_HOST, 70);
    strlcpy(str_max_illum, json_doc["max_illum"] | MAX_ILLUM_STR, 8);
    strlcpy(str_min_pot_res, json_doc["min_pot_res"] | MIN_POT_RES_STR, 8);
    strlcpy(str_apex_moondev, json_doc["apex_moondev"] | APEX_MOONDEV, 20);
    strlcpy(str_apex_poll, json_doc["apex_poll"] | APEX_POLL_STR, 8);

    if (_debug) {
      Serial.println("Config file contents:");
      Serial.printf("apex_host: %s\n", str_apex_host);
      Serial.printf("max_illum: %s\n", str_max_illum);
      Serial.printf("min_pot_res: %s\n", str_min_pot_res);
      Serial.printf("apex_moondev: %s\n", str_apex_moondev);
      Serial.printf("apex_poll: %s\n", str_apex_poll);
      Serial.println("-------");
    }
  } else {
    Serial.println("failed to load json config");
  }

  AsyncWiFiManagerParameter custom_apex_host("apex host", "apex host",
					     str_apex_host, 70);
  AsyncWiFiManagerParameter custom_max_illum("max illumination",
					     "max illumination", str_max_illum, 8);
  AsyncWiFiManagerParameter custom_min_pot_res("Minimum Pot RES",
					       "Minimum Pot RES",
					       str_min_pot_res, 8);
  AsyncWiFiManagerParameter custom_apex_moondev("apex moondev", "apex moondev",
						str_apex_moondev, 20);
  AsyncWiFiManagerParameter custom_apex_poll("apex poll", "apex poll",
					     str_apex_poll, 8);

  
  AsyncWiFiManager wifiManager(&server, &dns);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  //reset saved settings uncomment to nuke FS
  //wifiManager.resetSettings();
  wifiManager.addParameter(&custom_apex_host);
  wifiManager.addParameter(&custom_max_illum);
  wifiManager.addParameter(&custom_min_pot_res);
  wifiManager.addParameter(&custom_apex_moondev);
  wifiManager.addParameter(&custom_apex_poll);

  wifiManager.autoConnect("ESPLunarPhase01");

  Serial.println("connected...yeey :)");
  wifi_info = wifiManager.infoAsString();

  //read updated parameters
  strcpy(str_apex_host, custom_apex_host.getValue());
  strcpy(str_max_illum, custom_max_illum.getValue());
  strcpy(str_min_pot_res, custom_min_pot_res.getValue());
  strcpy(str_apex_moondev, custom_apex_moondev.getValue());
  strcpy(str_apex_poll, custom_apex_poll.getValue());
  min_pot_res = atoi(str_min_pot_res);
  max_illum = atof(str_max_illum);
  apex_poll = atoi(str_apex_poll);
  if (_debug) {
    Serial.println("Config contents after wifi connect:");
    Serial.printf("apex_host: %s\n", str_apex_host);
    Serial.printf("max_illum: %s\n", str_max_illum);
    Serial.printf("min_pot_res: %s\n", str_min_pot_res);
    Serial.printf("apex_moondev: %s\n", str_apex_moondev);
    Serial.printf("apex_poll: %s\n", str_apex_poll);
    Serial.println("-------");
  }
  
  /* Gather the time */
  setupDateTime();

  display.clear();
  display.drawString(0, 0, DateTime.toString().c_str());
  display.display();

  /* Setup the webserver */
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(index_html(request));
    });

  server.on("/info", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(info_html(request));
    });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(200, "text/plain", "Rebooting...");
      restartRequired = true;
    });

  server.on("/post", HTTP_POST, [](AsyncWebServerRequest *request){
        String message;

        if (request->hasParam("apex_host", true)) {
            message = request->getParam("apex_host", true)->value();
	    strncpy(str_apex_host, message.c_str(), 69);
	    shouldSaveConfig = true;
        }
        if (request->hasParam("max_illum", true)) {
            message = request->getParam("max_illum", true)->value();
	    strncpy(str_max_illum, message.c_str(), 7);
	    max_illum = atof(str_max_illum);
	    shouldSaveConfig = true;
        }
	if (request->hasParam("min_pot_res", true)) {
            message = request->getParam("min_pot_res", true)->value();
	    strncpy(str_min_pot_res, message.c_str(), 7);
	    min_pot_res = atoi(str_min_pot_res);
	    shouldSaveConfig = true;
        }
	if (request->hasParam("apex_moondev", true)) {
            message = request->getParam("apex_moondev", true)->value();
	    strncpy(str_apex_moondev, message.c_str(), 19);
	    shouldSaveConfig = true;
        }
	if (request->hasParam("apex_poll", true)) {
            message = request->getParam("apex_poll", true)->value();
	    strncpy(str_apex_poll, message.c_str(), 7);
	    apex_poll = atoi(str_apex_poll);
	    shouldSaveConfig = true;
        }
        request->send(200, "text/plain", "Updated settings.");
    });

  /* Handle remote code upload */
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
      // the request handler is triggered after the upload has finished... 
      // create the response, add header, and send response
      AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", (Update.hasError())?"FAIL":"OK");
      response->addHeader("Connection", "close");
      response->addHeader("Access-Control-Allow-Origin", "*");
      restartRequired = true;  // Tell the main loop to restart the ESP
      request->send(response);
    },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
      //Upload handler chunks in data
    
      if(!index){ // if index == 0 then this is the first frame of data
	Serial.printf("UploadStart: %s\n", filename.c_str());
	Serial.setDebugOutput(true);
      
	// calculate sketch space required for the update
	uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
	if(!Update.begin(maxSketchSpace)){//start with max available size
	  Update.printError(Serial);
	}
	Update.runAsync(true); // tell the updaterClass to run in async mode
      }

      //Write chunked data to the free sketch space
      if(Update.write(data, len) != len){
        Update.printError(Serial);
      }
    
      if(final){ // if the final flag is set then this is the last frame of data
	if(Update.end(true)){ //true to set the size to the current progress
          Serial.printf("Update Success: %u B\nRebooting...\n", index+len);
	  request->send(200, "text/plain", "New binary uploaded.");
	  restartRequired = true;
        } else {
          Update.printError(Serial);
        }
        Serial.setDebugOutput(false);
      }
    });

    server.onNotFound(notFound);

    server.begin();

    /* Ask the apex for current data once, and setup cron */
    request.setDebug(false);
    request.onData(ApexDataCB);
    ticker.attach(APEX_POLL, sendApexRequest);
    sendApexRequest();
    Serial.println("Setup complete.");
}

/* The main loop 
   Basically just recalculate the lunar phase every 5 seconds, update the OLED,
   and update the POT.
*/
void loop() {
  float ill = 0.0;
  int pota = 0;

  if (millis() - ms > 5000) {
    ms = millis();

    if (shouldSaveConfig)
      save_config_to_fs();

    if (restartRequired)
      ESP.restart();

    ill = get_lunar_illumination();
    cur_ill = ill;
    
    Serial.println("--------------------");
    Serial.printf("Lunar Illumination: %f\n", ill);
    Serial.printf("Local  Time:   %s\n", DateTime.toString().c_str());

    if (_debug) {
      Serial.printf("max_illum: %f\n", max_illum);
      Serial.printf("apex_illumination: %d\n", apex_illumination);
      Serial.printf("min_pot_res: %f\n", min_pot_res);
    }
    pota = (int)(ill * max_illum * 255.0 * (apex_illumination / 100.0));
    if (_debug)
      Serial.printf("PotA before min: %d\n", pota);
    if (pota < (min_pot_res / 100.0) * 255.0)
      pota = (min_pot_res / 100.0) * 255.0;
    Serial.printf("Setting PotA = %d\n", pota);
    
    DigitalPotWrite(POT0_SEL, pota);
  }
}