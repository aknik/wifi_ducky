#include <ESP8266WiFi.h>
#include <FS.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include <EEPROM.h>
#include "data.h"

#include "Settings.h"

#define BAUD_RATE 9600
#define bufferSize 600
#define debug false

/////////////////////////////////////////////////

#include <SoftwareSerial.h>

#define rxPin 14
#define txPin 12



SoftwareSerial mySerial(rxPin,txPin); // RX = rxPin, TX  = txPin

const static uint8_t HIDTable[] =  {
  0x00, // 0
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x2A,0x00,0x00, // 10
  0x00,0x00,0x28,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // 20
  0x00,0x00,0x00,0x00,0x00,0x00,0x29,0x00,0x00,0x00, // 30
  0x00,0x2c,0x1e,0x1f,0x20,0x21,0x22,0x23,0x2d,0x25,
  0x26,0x30,0x30,0x36,0x38,0x37,0x24,0x27,0x1e,0x1f,
  0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x37,0x36,0x64,
  0x27,0x64,0x2d,0x1f,0x04,0x05,0x06,0x07,0x08,0x09,
  0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,
  0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,
  0x2f,0x35,0x30,0x2f,0x38,0x2f,0x04,0x05,0x06,0x07,
  0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,
  0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,
  0x1c,0x1d,0x34,0x1e,0x32,0x21,0x2c // 127
};

const static uint8_t MODTable[] =  {
  0x00, // 0
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // 10
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // 20
  0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00, // 30
  0x00,0x00,0x02,0x02,0x40,0x02,0x02,0x02,0x00,0x02,
  0x02,0x02,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x02,0x00,
  0x02,0x02,0x02,0x40,0x02,0x02,0x02,0x02,0x02,0x02,
  0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,
  0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,
  0x40,0x40,0x40,0x02,0x02,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x40,0x40,0x40,0x40,0x00 // 127
};


uint8_t key ;
uint8_t mod ;

String last = "";
int defaultDelay = 0;

String bufferStr = "";
/////////////////////////////////////////




/* ============= CHANGE WIFI CREDENTIALS ============= */
const char *ssid = "WiFi Duck";
const char *password = "quackquack"; //min 8 chars
/* ============= ======================= ============= */

AsyncWebServer server(80);
FSInfo fs_info;

Settings settings;

bool shouldReboot = false;

//Web stuff
extern const uint8_t data_indexHTML[] PROGMEM;
extern const uint8_t data_updateHTML[] PROGMEM;
extern const uint8_t data_error404[] PROGMEM;
extern const uint8_t data_styleCSS[] PROGMEM;
extern const uint8_t data_functionsJS[] PROGMEM;
extern const uint8_t data_liveHTML[] PROGMEM;
extern const uint8_t data_infoHTML[] PROGMEM;
extern const uint8_t data_nomalizeCSS[] PROGMEM;
extern const uint8_t data_skeletonCSS[] PROGMEM;
extern const uint8_t data_license[] PROGMEM;
extern const uint8_t data_settingsHTML[] PROGMEM;
extern const uint8_t data_viewHTML[] PROGMEM;

extern String formatBytes(size_t bytes);

//Script stuff
bool runLine = false;
bool runScript = false;
File script;

uint8_t scriptBuffer[bufferSize];
uint8_t scriptLineBuffer[bufferSize];
int bc = 0; //buffer counter
int lc = 0; //line buffer counter


void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
  File f;
  
  if(!filename.startsWith("/")) filename = "/" + filename;
  
  if(!index) f = SPIFFS.open(filename, "w"); //create or trunicate file
  else f = SPIFFS.open(filename, "a"); //append to file (for chunked upload)
  
  if(debug) Serial.write(data, len);
  f.write(data, len);
  
  if(final){ //upload finished
	  if(debug) Serial.printf("UploadEnd: %s, %u B\n", filename.c_str(), index+len);
	  f.close();
  }
}

void send404(AsyncWebServerRequest *request){
  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", data_error404, sizeof(data_error404));
  request->send(response);
}

void sendToIndex(AsyncWebServerRequest *request){
  AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "");
  response->addHeader("Location","/");
  request->send(response);
}

void sendSettings(AsyncWebServerRequest *request) {
	AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", data_settingsHTML, sizeof(data_settingsHTML));
	request->send(response);
}

void setup() {
 ///////////////////////////
  mySerial.begin(9600); //Start mySerial    
  mySerial.flush();
  delay(1000);
  Serial.println("\nDucky started");

  while (mySerial.available() > 0 )  { delay(0);   } //  Espera señal de ATTINY de que está listo para recibir datos
 
//////////////////	
  
  Serial.begin(BAUD_RATE);
  delay(2000);
  if(debug) Serial.println("\nstarting...\nSSID: "+ (String)ssid +"\nPassword: "+ (String)password);

  EEPROM.begin(4096);
  SPIFFS.begin();

  settings.load();
  if(debug) settings.print();

  if(settings.autoExec) {
	  String _name = (String)settings.autostart;
	  script = SPIFFS.open("/" + _name, "r");
	  runScript = true;
	  runLine = true;
  }
  
  WiFi.mode(WIFI_STA);
  WiFi.softAP(settings.ssid, settings.password, settings.channel, settings.hidden);
  
  // ===== WebServer ==== //
  MDNS.addService("http","tcp",80);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
	  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", data_indexHTML, sizeof(data_indexHTML));
	  request->send(response);
  });
  server.on("/index.html", HTTP_GET, sendToIndex);

  server.on("/live.html", HTTP_GET, [](AsyncWebServerRequest *request) {
	  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", data_liveHTML, sizeof(data_liveHTML));
	  request->send(response);
  });

  server.on("/view.html", HTTP_GET, [](AsyncWebServerRequest *request) {
	  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", data_viewHTML, sizeof(data_viewHTML));
	  request->send(response);
  });

  server.on("license", HTTP_GET, [](AsyncWebServerRequest *request) {
	  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/plain", data_license, sizeof(data_license));
	  request->send(response);
  });

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
	  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/css", data_styleCSS, sizeof(data_styleCSS));
	  request->send(response);
  });

  server.on("/normalize.css", HTTP_GET, [](AsyncWebServerRequest *request) {
	  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/css", data_nomalizeCSS, sizeof(data_nomalizeCSS));
	  request->send(response);
  });

  server.on("/skeleton.css", HTTP_GET, [](AsyncWebServerRequest *request) {
	  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/css", data_skeletonCSS, sizeof(data_skeletonCSS));
	  request->send(response);
  });

  server.on("/functions.js", HTTP_GET, [](AsyncWebServerRequest *request) {
	  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/javascript", data_functionsJS, sizeof(data_functionsJS));
	  request->send(response);
  });

  server.on("/info.html", HTTP_GET, [](AsyncWebServerRequest *request) {
	  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", data_infoHTML, sizeof(data_infoHTML));
	  request->send(response);
  });

  server.on("/settings.html", HTTP_GET, [](AsyncWebServerRequest *request) {
	  sendSettings(request);
  });

  server.on("/settings.html", HTTP_POST, [](AsyncWebServerRequest *request) {

	  if(request->hasArg("ssid")) {
		  String _ssid = request->arg("ssid");
		  settings.ssidLen = _ssid.length();
		  _ssid.toCharArray(settings.ssid, 32);
		  if(debug) Serial.println("new SSID = '"+_ssid+"'");
	  }
	  if(request->hasArg("pswd")) {
		  String _pswd = request->arg("pswd");
		  settings.passwordLen = _pswd.length();
		  _pswd.toCharArray(settings.password, 32);
		  if(debug) Serial.println("new password = '" + _pswd + "'");
	  }
	  if(request->hasArg("autostart")) {
		  String _autostart = request->arg("autostart");
		  settings.autostartLen = _autostart.length();
		  _autostart.toCharArray(settings.autostart, 32);
		  if(debug) Serial.println("new autostart = '" + _autostart + "'");
	  }
	  if(request->hasArg("ch")) settings.channel = request->arg("ch").toInt();
	  if(request->hasArg("hidden")) settings.hidden = true;
	  else settings.hidden = false;
	  if(request->hasArg("autoExec")) settings.autoExec = true;
	  else settings.autoExec = false;
	  
	  settings.save();
	  if(debug) settings.print();

	  sendSettings(request);
  });

  server.on("/settings.json", HTTP_GET, [](AsyncWebServerRequest *request) {
	  String output = "{";
	  output += "\"ssid\":\"" + (String)settings.ssid + "\",";
	  output += "\"password\":\"" + (String)settings.password + "\",";
	  output += "\"channel\":" + String((int)settings.channel) + ",";
	  output += "\"hidden\":" + String((int)settings.hidden) + ",";
	  output += "\"autoExec\":" + String((int)settings.autoExec) + ",";
	  output += "\"autostart\":\"" + (String)settings.autostart + "\"";
	  output += "}";
	  request->send(200, "text/json", output);
  });

  server.on("/list.json", HTTP_GET, [](AsyncWebServerRequest *request) {
	  SPIFFS.info(fs_info);
	  Dir dir = SPIFFS.openDir("");
	  String output;
	  output += "{";
	  output += "\"totalBytes\":" + (String)fs_info.totalBytes + ",";
	  output += "\"usedBytes\":" + (String)fs_info.usedBytes + ",";
	  output += "\"list\":[ ";
	  while(dir.next()) {
		  File entry = dir.openFile("r");
		  String filename = String(entry.name()).substring(1);
		  output += '{';
		  output += "\"n\":\"" + filename + "\",";//name
		  output += "\"s\":\"" + formatBytes(entry.size()) + "\"";//size 
		  output += "},";
		  entry.close();
	  }
	  output = output.substring(0, output.length() - 1);
	  output += "]}";
	  request->send(200, "text/json", output);
  });

  server.on("/script", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasArg("name")){
      String _name = request->arg("name");
      request->send(SPIFFS, "/"+_name, "text/plain");
    }else send404(request);
  });

  server.on("/run", HTTP_POST, [](AsyncWebServerRequest *request) {
	  if(request->hasArg("name")) {
		  String _name = request->arg("name");
		  script = SPIFFS.open("/" + _name, "r");
		  runScript = true;
		  runLine = true;
		  request->send(200, "text/plain", "true");
	  }
	  else if(request->hasArg("script")) {
		  Serial.println(request->arg("script"));
		  request->send(200, "text/plain", "true");
	  }
	  else send404(request);
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
	  if(request->hasArg("name") && request->hasArg("script")) {
		  String _name = request->arg("name");
		  String _script = request->arg("script");
		  File f = SPIFFS.open("/" + _name, "w");
		  if(f) {
			  f.print(_script);
			  request->send(200, "text/plain", "true");
		  }
		  else request->send(200, "text/plain", "false");
	  }
	  else send404(request);
  });


  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasArg("name")){
      String _name = request->arg("name");
      SPIFFS.remove("/"+_name);
      request->send(200, "text/plain", "true");
    }else send404(request);
  });

  server.on("/rename", HTTP_GET, [](AsyncWebServerRequest *request) {
	  if(request->hasArg("name") && request->hasArg("newName")) {
		  String _name = request->arg("name");
		  String _newName = request->arg("newName");
		  SPIFFS.rename("/" + _name, "/" + _newName);
		  request->send(200, "text/plain", "true");
	  }
	  else send404(request);
  });

  server.on("/format", HTTP_GET, [](AsyncWebServerRequest *request){
    SPIFFS.format();
    request->send(200, "text/plain", "true");
    sendToIndex(request);
  });

  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request){
    sendToIndex(request);
  }, handleUpload);
  
  server.onNotFound([](AsyncWebServerRequest *request){
    send404(request);
  });

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
	  AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "");
	  response->addHeader("Location", "/info.html");
	  request->send(response);
  });

  server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request) {
	  shouldReboot = true;
  });

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
	  settings.reset();
	  request->send(200, "text/plain", "true");
	  sendToIndex(request);
  });

  //update
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    shouldReboot = !Update.hasError();
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot?"OK":"FAIL");
    response->addHeader("Connection", "close");
    request->send(response);
  },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index){
      if(debug) Serial.printf("Update Start: %s\n", filename.c_str());
      Update.runAsync(true);
      if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)){
		  if(debug) Update.printError(Serial);
      }
    }
    if(!Update.hasError()){
      if(Update.write(data, len) != len){
        Update.printError(Serial);
      }
    }
    if(final){
      if(Update.end(true)){
		if(debug) Serial.printf("Update Success: %uB\n", index+len);
      } else {
		if(debug) Update.printError(Serial);
      }
    }
  });
  
  server.begin();
  
  if(debug) Serial.println("started");
}

void sendBuffer(){
  for(int i=0;i<bc;i++) Serial.write((char)scriptBuffer[i]);
  runLine = false;
  bc = 0;
/////////////////////////////////////////////////
	
	
	while(f.available()) {

    bufferStr=f.readStringUntil('\n');

    bufferStr.replace("\r","\n");
    bufferStr.replace("\n\n","\n");
    bufferStr.replace("\n ","\n");
      
      int latest_return = bufferStr.indexOf("\n");
      if(latest_return == -1){
         Serial.println("\nrun: "+bufferStr);
        Line(bufferStr);
        bufferStr = "";
      } else{
         Serial.println("\nrun: '"+bufferStr.substring(0, latest_return)+"'");
        Line(bufferStr.substring(0, latest_return));
        last=bufferStr.substring(0, latest_return);
        bufferStr = bufferStr.substring(latest_return + 1);
      }
    }
	
	
/////////////////////////////////////////////////
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
}

void addToBuffer(){
  if(bc + lc > bufferSize) sendBuffer();
  for(int i=0;i<lc;i++){
    scriptBuffer[bc] = scriptLineBuffer[i];
    bc++;
  }
  lc = 0;
}

void loop() {
  if(shouldReboot) ESP.restart();
  
  if(Serial.available()) {
    uint8_t answer = Serial.read();
    if(answer == 0x99) {
      if(debug) Serial.println("done");
      runLine = true;
	}
	else {
		String command = (char)answer + Serial.readStringUntil('\n');
		command.replace("\r", "");
		if(command == "reset") {
			settings.reset();
			shouldReboot = true;
		}
	}
  }

  if(runScript && runLine){
    if(script.available()){
      uint8_t nextChar = script.read();
	  if(debug) Serial.write(nextChar);
      scriptLineBuffer[lc] = nextChar;
      lc++;
      if(nextChar == 0x0D || lc == bufferSize) addToBuffer();
    }else{
      addToBuffer();
      if(bc > 0) sendBuffer();
      runScript = false;
      script.close();
    }
  }

}
/////////////////////////////////////////////////


void DigiKeyboard(uint8_t key ){

      
      if(key > 0 || mod > 0 ) {
        
      mySerial.flush();
      
      mySerial.write(key);
      mySerial.write(mod);
      
      while (mySerial.available() != 2)  { delay(0);   } // Espera confirmacion del ATTINY

      // Serial.print(key,HEX);Serial.print(",");
      // Serial.print(mod,HEX);Serial.print(",");

                      
///////////////////////////if(key == 0 ) delay(mod);
                      
      
      
                                } 
}// THE END -0x5d




void Line(String _line)
{
  int firstSpace = _line.indexOf(" ",1);
  //Serial.println(_line.substring(0,firstSpace));
  if(firstSpace == -1) Press(_line);
    else if(_line.substring(0,firstSpace) == "STRING"){
    
    for(int i=firstSpace+1;i<_line.length();i++) { 
      
      
      
      Serial.print(_line[i],DEC);Serial.print(">");Serial.print(HIDTable[_line[i]],HEX);Serial.print(_line[i]);
      
      mod = MODTable[_line[i]];
   
      key = HIDTable[_line[i]];
      
      DigiKeyboard(key);
  
      
      }

    
  }
  else if(_line.substring(0,firstSpace) == "DELAY"){
    int delaytime = _line.substring(firstSpace + 1).toInt();
    
while (delaytime > 0) {
                                               
if (delaytime > 255) {
        mod = 255; DigiKeyboard(0);
        delaytime = delaytime - 255;
} else {
        mod = delaytime; DigiKeyboard(0);
        delaytime = 0;
}
                      }
    
    mod = delaytime; DigiKeyboard(0);
  }
  else if(_line.substring(0,firstSpace) == "DEFAULTDE }LAY") defaultDelay = _line.substring(firstSpace + 1).toInt();
  else if(_line.substring(0,firstSpace) == "REM"){} //nothing :/
  else if(_line.substring(0,firstSpace) == "REPLAY") {
    int replaynum = _line.substring(firstSpace + 1).toInt();
    while(replaynum)
    {
      Line(last);
      --replaynum;
    }
  } else{
      String remain = _line;

      while(remain.length() > 0){
        int latest_space = remain.indexOf(" ");
        if (latest_space == -1){
          Press(remain);
          remain = "";
        }
        else{
          Press(remain.substring(0, latest_space));
          remain = remain.substring(latest_space + 1);
        }
        delay(5);
      } }
  }


void Press(String b){

  //Serial.println(b);

  if(b.length() == 1) DigiKeyboard(char(b[0]));
  else if (b.equals("ENTER")) DigiKeyboard(0x28);
  else if (b.equals("CTRL")) mod = (MOD_CONTROL_LEFT);
  else if (b.equals("SHIFT")) mod = (MOD_SHIFT_LEFT);
  else if (b.equals("ALT")) mod = (MOD_ALT_LEFT);
  else if (b.equals("GUI")) mod = (MOD_GUI_LEFT);
  else if (b.equals("UP") || b.equals("UPARROW")) DigiKeyboard(KEY_ARROW_UP);
  else if (b.equals("DOWN") || b.equals("DOWNARROW")) DigiKeyboard(KEY_ARROW_DOWN);
  else if (b.equals("LEFT") || b.equals("LEFTARROW")) DigiKeyboard(KEY_ARROW_LEFT);
  else if (b.equals("RIGHT") || b.equals("RIGHTARROW")) DigiKeyboard(KEY_ARROW_RIGHT);
  else if (b.equals("DELETE")) DigiKeyboard(KEY_DELETE);
  else if (b.equals("PAGEUP")) DigiKeyboard(KEY_PAGE_UP);
  else if (b.equals("PAGEDOWN")) DigiKeyboard(KEY_PAGE_DOWN);
  else if (b.equals("HOME")) DigiKeyboard(KEY_HOME);
  else if (b.equals("ESC")) DigiKeyboard(KEY_ESCAPE);
  else if (b.equals("BACKSPACE")) DigiKeyboard(KEY_DELETE);
  else if (b.equals("INSERT")) DigiKeyboard(KEY_INSERT);
  else if (b.equals("TAB")) DigiKeyboard(KEY_TAB);
  else if (b.equals("END")) DigiKeyboard(KEY_END);
  else if (b.equals("CAPSLOCK")) DigiKeyboard(KEY_CAPS_LOCK);
  else if (b.equals("F1")) DigiKeyboard(KEY_F1);
  else if (b.equals("F2")) DigiKeyboard(KEY_F2);
  else if (b.equals("F3")) DigiKeyboard(KEY_F3);
  else if (b.equals("F4")) DigiKeyboard(KEY_F4);
  else if (b.equals("F5")) DigiKeyboard(KEY_F5);
  else if (b.equals("F6")) DigiKeyboard(KEY_F6);
  else if (b.equals("F7")) DigiKeyboard(KEY_F7);
  else if (b.equals("F8")) DigiKeyboard(KEY_F8);
  else if (b.equals("F9")) DigiKeyboard(KEY_F9);
  else if (b.equals("F10")) DigiKeyboard(KEY_F10);
  else if (b.equals("F11")) DigiKeyboard(KEY_F11);
  else if (b.equals("F12")) DigiKeyboard(KEY_F12);
  else if (b.equals("SPACE")) DigiKeyboard(' ');
  //else Serial.println("not found :'"+b+"'("+String(b.length())+")");
}



