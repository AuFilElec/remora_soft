// **********************************************************************************
// Programmateur Fil Pilote et Suivi Conso
// **********************************************************************************
// Copyright (C) 2014 Thibault Ducret
// Licence MIT
//
// History : 15/01/2015 Charles-Henri Hallard (http://hallard.me)
//                      Intégration de version 1.2 de la carte electronique
//           13/04/2015 Theju
//                      Modification des variables cloud teleinfo
//                      (passage en 1 seul appel) et liberation de variables
//           15/09/2015 Charles-Henri Hallard : Ajout compatibilité ESP8266
//           02/12/2015 Charles-Henri Hallard : Ajout API WEB ESP8266 et Remora V1.3
//           04/01/2016 Charles-Henri Hallard : Ajout Interface WEB GUIT
//           04/03/2017 Manuel Hervo          : Ajout des connexions TCP Asynchrones
//
// **********************************************************************************
#include "Arduino.h"

// Tout est inclus dans le fichier remora.h
// Pour activer des modules spécifiques ou
// changer différentes configurations il
// faut le faire dans le fichier remora.h
#include "remora.h"

// Arduino IDE need include in main INO file
#ifdef ESP8266
  #include <EEPROM.h>
  #include <FS.h>
  #include <Hash.h>
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
  // #include <ESP8266WebServer.h>
  #include <ESP8266mDNS.h>
  #include <ESPAsyncTCP.h>
  #include <ESPAsyncWebServer.h>
  #include <WiFiUdp.h>
  #include <ArduinoOTA.h>
  #include <Wire.h>
  #include <SPI.h>
  #include <Ticker.h>
  #include <NeoPixelBus.h>
  #include <BlynkSimpleEsp8266.h>
  #include <LibMCP23017.h>
  #include <LibSSD1306.h>
  #include <LibGFX.h>
  #include <LibULPNode_RF_Protocol.h>
  #include <LibLibTeleinfo.h>
  #include <LibRadioHead.h>
  #include <LibRHReliableDatagram.h>
#endif


// Variables globales
// ==================
// status global de l'application
uint16_t status = 0;
unsigned long uptime = 0;
bool first_setup;

// Nombre de deconexion cloud detectée
int my_cloud_disconnect = 0;

#ifdef ESP8266
  // ESP8266 WebServer
  AsyncWebServer server(80);
  // Use WiFiClient class to create a connection to WEB server
  WiFiClient client;
  // RGB LED (1 LED)
  MyPixelBus rgb_led(1, RGB_LED_PIN);

  // define whole brigtness level for RGBLED
  uint8_t rgb_brightness = 127;

  Ticker Tick_emoncms;
  Ticker Tick_jeedom;

  volatile boolean task_emoncms = false;
  volatile boolean task_jeedom = false;

  bool ota_blink;

  bool reboot = false;
#endif


// ====================================================
// Following are dedicated to ESP8266 Platform
// Wifi management and OTA updates
// ====================================================
#ifdef ESP8266

/* ======================================================================
Function: Task_emoncms
Purpose : callback of emoncms ticker
Input   :
Output  : -
Comments: Like an Interrupt, need to be short, we set flag for main loop
====================================================================== */
void Task_emoncms()
{
  task_emoncms = true;
}

/* ======================================================================
Function: Task_jeedom
Purpose : callback of jeedom ticker
Input   :
Output  : -
Comments: Like an Interrupt, need to be short, we set flag for main loop
====================================================================== */
void Task_jeedom()
{
  task_jeedom = true;
}

/* ======================================================================
Function: WifiHandleConn
Purpose : Handle Wifi connection / reconnection and OTA updates
Input   : setup true if we're called 1st Time from setup
Output  : state of the wifi status
Comments: -
====================================================================== */
int WifiHandleConn(boolean setup = false)
{
  int ret = WiFi.status();
  uint8_t timeout ;

  if (setup) {
    // Feed the dog
    _wdt_feed();

    DebugF("========== SDK Saved parameters Start");
    WiFi.printDiag(DEBUG_SERIAL);
    DebuglnF("========== SDK Saved parameters End");

    #if defined (DEFAULT_WIFI_SSID) && defined (DEFAULT_WIFI_PASS)
      DebugF("Connection au Wifi : ");
      Debug(DEFAULT_WIFI_SSID);
      DebugF(" avec la clé '");
      Debug(DEFAULT_WIFI_PASS);
      DebugF("'...");
      Debugflush();
      WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
    #else
      if (*config.ssid) {
        DebugF("Connection à: ");
        Debug(config.ssid);
        Debugflush();

        // Do wa have a PSK ?
        if (*config.psk) {
          // protected network
          Debug(F(" avec la clé '"));
          Debug(config.psk);
          Debug(F("'..."));
          Debugflush();
          WiFi.begin(config.ssid, config.psk);
        } else {
          // Open network
          Debug(F("AP Ouvert"));
          Debugflush();
          WiFi.begin(config.ssid);
        }
      }
    #endif

    timeout = 25; // 25 * 200 ms = 5 sec time out

    // 200 ms loop
    while ( ((ret = WiFi.status()) != WL_CONNECTED) && timeout )
    {
      // Orange LED
      LedRGBON(COLOR_ORANGE);
      delay(50);
      LedRGBOFF();
      delay(150);
      --timeout;
    }

    // connected ? disable AP, client mode only
    if (ret == WL_CONNECTED)
    {
      DebuglnF("connecte!");
      WiFi.mode(WIFI_STA);

      DebugF("IP address   : "); Debugln(WiFi.localIP());
      DebugF("MAC address  : "); Debugln(WiFi.macAddress());

    // not connected ? start AP
    } else {
      char ap_ssid[32];
      DebugF("Erreur, passage en point d'acces ");
      Debugln(DEFAULT_HOSTNAME);

      // protected network
      DebugF(" avec la clé '");
      if (*config.ap_psk) {
        Debug(config.ap_psk);
      } else {
        Debug(DEFAULT_WIFI_AP_PASS);
      }
      Debugln("'");
      Debugflush();

      if (*config.ap_psk) {
        WiFi.softAP(DEFAULT_HOSTNAME, config.ap_psk);
      } else {
        WiFi.softAP(DEFAULT_HOSTNAME, DEFAULT_WIFI_AP_PASS);
      }
      WiFi.mode(WIFI_AP_STA);

      DebugF("IP address   : "); Debugln(WiFi.softAPIP());
      DebugF("MAC address  : "); Debugln(WiFi.softAPmacAddress());
    }

    // Feed the dog
    _wdt_feed();

    // Set OTA parameters
     ArduinoOTA.setPort(DEFAULT_OTA_PORT);
     ArduinoOTA.setHostname(DEFAULT_HOSTNAME);
     if (*config.ota_auth) {
       ArduinoOTA.setPassword(config.ota_auth);
     }/* else {
       ArduinoOTA.setPassword(DEFAULT_OTA_PASS);
     }*/
     ArduinoOTA.begin();

    // just in case your sketch sucks, keep update OTA Available
    // Trust me, when coding and testing it happens, this could save
    // the need to connect FTDI to reflash
    // Usefull just after 1st connexion when called from setup() before
    // launching potentially buggy main()
    for (uint8_t i=0; i<= 10; i++) {
      LedRGBON(COLOR_MAGENTA);
      delay(100);
      LedRGBOFF();
      delay(200);
      ArduinoOTA.handle();
    }

  } // if setup

  return WiFi.status();
}

#endif

/* ======================================================================
Function: timeAgo
Purpose : format total seconds to human readable text
Input   : second
Output  : pointer to string
Comments: -
====================================================================== */
char * timeAgo(unsigned long sec)
{
  static char buff[16];

  // Clear buffer
  buff[0] = '\0';

  if (sec < 2) {
    sprintf_P(buff,PSTR("just now"));
  } else if (sec < 60) {
    sprintf_P(buff,PSTR("%d seconds ago"), sec);
  } else if (sec < 120) {
    sprintf_P(buff,PSTR("1 minute ago"));
  } else if (sec < 3600) {
    sprintf_P(buff,PSTR("%d minutes ago"), sec/60);
  } else if (sec < 7200) {
    sprintf_P(buff,PSTR("1 hour ago"));
  } else if (sec < 86400) {
    sprintf_P(buff,PSTR("%d hours ago"), sec/3660);
  } else if (sec < 172800) {
    sprintf_P(buff,PSTR("yesterday"));
  } else if (sec < 604800) {
    sprintf_P(buff,PSTR("%d days ago"), sec/86400);
  }
  return buff;
}


/* ======================================================================
Function: setup
Purpose : prepare and init stuff, configuration, ..
Input   : -
Output  : -
Comments: -
====================================================================== */
void setup()
{
  uint8_t rf_version = 0;

  #if defined DEBUG_INIT || !defined MOD_TELEINFO
    DEBUG_SERIAL.begin(115200);
  #endif

  // says main loop to do setup
  first_setup = true;

  Debugln("Starting main setup");
  Debugflush();
}



/* ======================================================================
Function: mysetup
Purpose : prepare and init stuff, configuration, ..
Input   : -
Output  : -
Comments: -
====================================================================== */
void mysetup()
{
  uint8_t rf_version = 0;

  #if defined (ESP8266)

    #ifdef MOD_TELEINFO
      // Init de la téléinformation
      Serial.begin(1200, SERIAL_7E1);
    #endif

    // Clear our global flags
    config.config = 0;

    // Our configuration is stored into EEPROM
    //EEPROM.begin(sizeof(_Config));
    EEPROM.begin(1024);

    DebugF("Config size="); Debug(sizeof(_Config));
    DebugF(" (emoncms=");   Debug(sizeof(_emoncms));
    DebugF("  jeedom=");   Debug(sizeof(_jeedom));
    Debugln(')');
    Debugflush();

    // Check File system init
    if (!SPIFFS.begin())
    {
      // Serious problem
      DebuglnF("SPIFFS Mount failed");
    } else {

      DebuglnF("SPIFFS Mount succesfull");

      Dir dir = SPIFFS.openDir("/");
      while (dir.next()) {
        String fileName = dir.fileName();
        size_t fileSize = dir.fileSize();
        Debugf("FS File: %s, size: %d\n", fileName.c_str(), fileSize);
        _wdt_feed();
      }
      DebuglnF("");
    }

    // Read Configuration from EEP
    if (readConfig()) {
        DebuglnF("Good CRC, not set!");
    } else {
      // Reset Configuration
      resetConfig();

      // save back
      saveConfig();

      // Indicate the error in global flags
      config.config |= CFG_BAD_CRC;

      DebuglnF("Reset to default");
    }

    // Connection au Wifi ou Vérification
    WifiHandleConn(true);

    // OTA callbacks
    ArduinoOTA.onStart([]() {
      if (ArduinoOTA.getCommand() == U_SPIFFS) {
        SPIFFS.end();
      }
      LedRGBON(COLOR_MAGENTA);
      DebugF("\r\nUpdate Started..");
      ota_blink = true;
    });

    ArduinoOTA.onEnd([]() {
      LedRGBOFF();
      DebuglnF("Update finished restarting");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      if (ota_blink) {
        LedRGBON(COLOR_MAGENTA);
      } else {
        LedRGBOFF();
      }
      ota_blink = !ota_blink;
      //Debugf("Progress: %u%%\n", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error) {
      LedRGBON(COLOR_RED);
      DEBUG_SERIAL.printf("Update Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) DebuglnF("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) DebuglnF("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) DebuglnF("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) DebuglnF("Receive Failed");
      else if (error == OTA_END_ERROR) DebuglnF("End Failed");
      //reboot = true;
    });

    // handler for uptime
    server.on("/uptime", HTTP_GET, [](AsyncWebServerRequest *request) {
      String response = "";
      response += FPSTR("{\r\n");
      response += F("\"uptime\":");
      response += uptime;
      response += FPSTR("\r\n}\r\n") ;
      request->send(200, "text/json", response);
    });

    server.on("/config_form.json", HTTP_POST, handleFormConfig);
    server.on("/factory_reset",handleFactoryReset );
    server.on("/reset", handleReset);
    server.on("/tinfo", tinfoJSON);
    server.on("/tinfo.json", tinfoJSONTable);
    server.on("/system.json", sysJSONTable);
    server.on("/config.json", confJSONTable);
    server.on("/spiffs.json", spiffsJSONTable);
    server.on("/wifiscan.json", wifiScanJSON);

    // handler for the hearbeat
    server.on("/hb.htm", HTTP_GET, [](AsyncWebServerRequest *request) {
      AsyncWebServerResponse *response = request->beginResponse(200, "text/html", R"(OK)");
      response->addHeader("Connection", "close");
      response->addHeader("Access-Control-Allow-Origin", "*");
      request->send(response);
    });

    // handler for the /update form POST (once file upload finishes)
    server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
      reboot = !Update.hasError();
      AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", reboot ? "OK" : "FAIL");
      response->addHeader("Connection", "close");
      request->send(response);
    }, handle_fw_upload);

    server.onNotFound(handleNotFound);

    // serves all SPIFFS Web file with 24hr max-age control
    // to avoid multiple requests to ESP
    server.serveStatic("/font", SPIFFS, "/font","max-age=86400");
    server.serveStatic("/js",   SPIFFS, "/js"  ,"max-age=86400");
    server.serveStatic("/css",  SPIFFS, "/css" ,"max-age=86400");
    server.begin();
    DebuglnF("HTTP server started");

    #ifdef BLYNK_AUTH
      Blynk.config(BLYNK_AUTH);
    #endif

  #endif

  // Init bus I2C
  i2c_init();

  Debug("Remora Version ");
  Debugln(REMORA_VERSION);
  Debug("Compile avec les fonctions : ");

  #if defined (REMORA_BOARD_V10)
    Debug("BOARD V1.0 ");
  #elif defined (REMORA_BOARD_V11)
    Debug("BOARD V1.1 ");
  #elif defined (REMORA_BOARD_V12)
    Debug("BOARD V1.2 MCP23017 ");
  #elif defined (REMORA_BOARD_V13)
    Debug("BOARD V1.3 MCP23017 ");
  #elif defined (REMORA_BOARD_V14)
    Debug("BOARD V1.4 MCP23017 ");
  #else
    Debug("BOARD Inconnue");
  #endif

  #ifdef MOD_OLED
    Debug("OLED ");
  #endif
  #ifdef MOD_TELEINFO
    Debug("TELEINFO ");
  #endif
  #ifdef MOD_RF69
    Debug("RFM69 ");
  #endif
  #ifdef BLYNK_AUTH
    Debug("BLYNK ");
  #endif
  #ifdef MOD_ADPS
    Debug("ADPS ");
  #endif

  Debugln();

  // Init des fils pilotes
  if (pilotes_setup())
    status |= STATUS_MCP ;

  #ifdef MOD_OLED
    // Initialisation de l'afficheur
    if (display_setup())
    {
      status |= STATUS_OLED ;
      // Splash screen
      display_splash();
    }
  #endif

  #ifdef MOD_RF69
    // Initialisation RFM69 Module
    if ( rfm_setup())
      status |= STATUS_RFM ;
  #endif

  // Feed the dog
  _wdt_feed();

  #ifdef MOD_TELEINFO
    // Initialiser la téléinfo et attente d'une trame valide
    // Le status est mis à jour dans les callback de la teleinfo
    tinfo_setup(true);
    // Initialise la mise à jour de Jeedom si la config est définie
    if (strlen(config.jeedom.host) > 0 && strlen(config.jeedom.url) > 0
      && strlen(config.jeedom.apikey) > 0 && (config.jeedom.freq > 0 && config.jeedom.freq < 86400)) {
      // Jeedom Update if needed
      Tick_jeedom.detach();
      Tick_jeedom.attach(config.jeedom.freq, Task_jeedom);
    }
  #endif

  // Led verte durant le test
  LedRGBON(COLOR_GREEN);

  // Enclencher le relais 1 seconde
  // si dispo sur la carte
  #ifndef REMORA_BOARD_V10
    Debug("Relais=ON   ");
    Debugflush();
    relais("1");
    for (uint8_t i=0; i<20; i++)
    {
      delay(10);
     // Feed the dog
     _wdt_feed();

      // Ne pas bloquer la reception et
      // la gestion de la téléinfo
      #ifdef MOD_TELEINFO
        tinfo_loop();
      #endif
    }
    Debugln("Relais=OFF");
    Debugflush();
    relais("0");
  #endif

  // nous avons fini, led Jaune
  LedRGBON(COLOR_YELLOW);

  // Hors gel, désactivation des fils pilotes
  initFP();

  // On etteint la LED embarqué du core
  LedRGBOFF();

  Debugln("Starting main loop");
  Debugflush();
}



/* ======================================================================
Function: loop
Purpose : boucle principale du programme
Input   : -
Output  : -
Comments: -
====================================================================== */
void loop()
{
  static bool refreshDisplay = false;
  static bool lastcloudstate;
  static unsigned long previousMillis = 0;  // last time update
  unsigned long currentMillis = millis();
  bool currentcloudstate ;

  // our own setup
  if (first_setup) {
    mysetup();
    first_setup = false;
  }

  #ifdef ESP8266
  /* Reboot handler */
  if (reboot) {
    delay(REBOOT_DELAY);
    ESP.restart();
  }
  #endif

  // Gérer notre compteur de secondes
  if ( millis()-previousMillis > 1000) {
    // Ceci arrive toute les secondes écoulées
    previousMillis = currentMillis;
    uptime++;
    refreshDisplay = true ;
    #ifdef BLYNK_AUTH
      if ( Blynk.connected() ) {
        String up    = String(uptime) + "s";
        String papp  = String(mypApp) + "W";
        String iinst = String(myiInst)+ "A";
        Blynk.virtualWrite(V0, up, papp, iinst, mypApp);
        _yield();
      }
    #endif
  } else {
    #ifdef BLYNK_AUTH
      Blynk.run(); // Initiates Blynk
    #endif
  }

  #ifdef MOD_TELEINFO
    // Vérification de la reception d'une 1ere trame téléinfo
    tinfo_loop();
    _yield();
  #endif

  #ifdef MOD_RF69
    // Vérification de la reception d'une trame RF
    if (status & STATUS_RFM)
      rfm_loop();
      _yield();
  #endif

  #ifdef MOD_OLED
    // pour le moment on se contente d'afficher la téléinfo
    screen_state = screen_teleinfo;

    // Modification d'affichage et afficheur présent ?
    if (refreshDisplay && (status & STATUS_OLED))
      display_loop();
      _yield();
  #endif

  // çà c'est fait
  refreshDisplay = false;

  #if defined (ESP8266)
    // recupération de l'état de connexion au Wifi
    currentcloudstate = WiFi.status()==WL_CONNECTED ? true:false;
  #endif

  // La connexion cloud vient de chager d'état ?
  if (lastcloudstate != currentcloudstate)
  {
    // Mise à jour de l'état
    lastcloudstate=currentcloudstate;

    // on vient de se reconnecter ?
    if (currentcloudstate)
    {
      // led verte
      LedRGBON(COLOR_GREEN);
    }
    else
    {
      // on compte la deconnexion led rouge
      my_cloud_disconnect++;
      Debug("Perte de conexion au cloud #");
      Debugln(my_cloud_disconnect);
      LedRGBON(COLOR_RED);
    }
  }

  // Connection au Wifi ou Vérification
  #ifdef ESP8266
    // Webserver
    //server.handleClient();
    ArduinoOTA.handle();

    if (task_emoncms) {
      emoncmsPost();
      task_emoncms=false;
    } else if (task_jeedom) {
      jeedomPost();
      task_jeedom=false;
    }
  #endif
}
