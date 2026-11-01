#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>

extern AsyncWebServer  httpSrv;
extern AsyncWebSocket  wsSrv;
extern AsyncAuthenticationMiddleware webAuth;
void wifiChangeTick();  // im App-Task aufrufen
void webAuthSetup();    // nach loadConfig(), vor den Routen
extern const char DASH_HTML[] PROGMEM;

String buildJson();
void onWsEvent(AsyncWebSocket* s,AsyncWebSocketClient* c,
               AwsEventType t,void* arg,uint8_t* data,size_t len);
