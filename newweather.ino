/**The MIT License (MIT)

Copyright (c) 2018 by Daniel Eichhorn - ThingPulse

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

See more at https://thingpulse.com
*/

//Monsteryuan forked from Daniel Eichhorn/ThingPulse ESP8266 Weather Station

#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
// time
#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval
#include <coredecls.h>                  // settimeofday_cb()


#include "OLEDDisplayUi.h"
#include "SSD1306Wire.h"
#include "HeFeng.h"

#include "WeatherStationFonts.h"
#include "WeatherStationImages.h"

/***************************
 * Begin Settings
 **************************/

// config
#define TZ              +8       // (utc+) TZ in hours
#define DST_MN          0      // use 60mn for summer time in some countries

// Setup 
// weather update
const int UPDATE_INTERVAL_SECS = 20 * 60; // Update every 20 minutes  online weather
// Setup
// tem update
const int UPDATE_CURR_INTERVAL_SECS = 10; // Update every 10 secs DS18B20

// Display Settings
// config
const int I2C_DISPLAY_ADDRESS = 0x3c;
const int SDA_PIN = D2;
const int SDC_PIN = D1;

// button
// config
int button_wifi = D3;

unsigned long btn_time_s = 0;
unsigned long btn_time_e = 0;
unsigned long btn_time_dur = 0;
int wifi_status_old = 0;
int wifi_pin_lh = 0;
int temp_wifi = 0;


const String WDAY_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const String MONTH_NAMES[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

/***************************
 * End Settings
 **************************/

// Initialize the oled display for address 0x3c
// sda-pin=14 and sdc-pin=12
SSD1306Wire     display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN);   // or SSD1306Wire  display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN);
OLEDDisplayUi   ui( &display );

HeFengCurrentData currentWeather;
HeFengForeData foreWeather[3];
HeFeng HeFengClient;

//OpenWeatherMapForecastData forecasts[MAX_FORECASTS];
//OpenWeatherMapForecast forecastClient;

#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)

const char* HEFENG_KEY="";
const char* HEFENG_LOCATION="";
time_t now;

// flag changed in the ticker function every 10 minutes
bool readyForWeatherUpdate = false;

String lastUpdate = "--";

long timeSinceLastWUpdate = 0;
long timeSinceLastCurrUpdate = 0;

String currTemp="-1.0";
//declaring prototypes
void drawProgress(OLEDDisplay *display, int percentage, String label);
void updateData(OLEDDisplay *display);
void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecast(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex);
void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state);
void setReadyForWeatherUpdate();


// Add frames
// this array keeps function pointers to all frames
// frames are the single views that slide from right to left
FrameCallback frames[] = { drawDateTime, drawCurrentWeather,drawForecast };
//drawForecast
int numberOfFrames = 3;

OverlayCallback overlays[] = { drawHeaderOverlay };
int numberOfOverlays = 1;


bool autoConfig() {
    WiFi.mode(WIFI_STA);
    WiFi.begin();
    Serial.print("AutoConfig Waiting......");
    int counter = 0;
    for (int i = 0; i < 20; i++)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("AutoConfig Success");
            Serial.printf("SSID:%s\r\n", WiFi.SSID().c_str());
            Serial.printf("PSW:%s\r\n", WiFi.psk().c_str());
            WiFi.printDiag(Serial);
            return true;
        }
        else
        {
            delay(500);
            Serial.print(".");
            display.clear();
            display.drawString(64, 10, "Connecting to WiFi");
            display.drawXbm(46, 30, 8, 8, counter % 3 == 0 ? activeSymbole : inactiveSymbole);
            display.drawXbm(60, 30, 8, 8, counter % 3 == 1 ? activeSymbole : inactiveSymbole);
            display.drawXbm(74, 30, 8, 8, counter % 3 == 2 ? activeSymbole : inactiveSymbole);
            display.display(); 
            counter++; 
        }
    }
    Serial.println("AutoConfig Faild!" );
    return false;
}


ESP8266WebServer server(80);
String str = "<!DOCTYPE html><html><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><meta http-equiv=\"X-UA-Compatible\" content=\"ie=edge\"><title>ESP8266网页配网</title><script type=\"text/javascript\">function wifi(){var ssid = aa.value;var password = bb.value;var xmlhttp=new XMLHttpRequest();xmlhttp.open(\"GET\",\"/HandleWifi?ssid=\"+ssid+\"&password=\"+password,true);xmlhttp.send();xmlhttp.onload = function(e){alert(this.responseText);}}</script></head><body>hello,我是fxy,输入wifi信息给wifi时钟配网:  <form>WiFi名称：<input type=\"text\" placeholder=\"请输入您WiFi的名称\" id=\"aa\"><br>WiFi密码：<input type=\"text\" placeholder=\"请输入您WiFi的密码\" id=\"bb\"><br><input type=\"button\" value=\"连接\" onclick=\"wifi()\"></form></body></html>";

void handleRoot() {
    server.send(200, "text/html", str);
}

void HandleWifi() {
    String wifis = server.arg("ssid"); //从JavaScript发送的数据中找ssid的值
    String wifip = server.arg("password"); //从JavaScript发送的数据中找password的值
    Serial.println("received:"+wifis);
    server.send(200, "text/html", "连接中..");
    WiFi.begin(wifis,wifip);
}

void handleNotFound() { 
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++) {
        message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    server.send(404, "text/plain", message);
}

void htmlConfig() {
    WiFi.mode(WIFI_AP_STA);//设置模式为AP+STA
    WiFi.softAP("wifi_clock");

    IPAddress myIP = WiFi.softAPIP();
  
    if (MDNS.begin("clock")) {
        Serial.println("MDNS responder started");
    }
    
    server.on("/", handleRoot);
    server.on("/HandleWifi", HTTP_GET, HandleWifi);
    server.onNotFound(handleNotFound);//请求失败回调函数
    MDNS.addService("http", "tcp", 80);
    server.begin();//开启服务器
    Serial.println("HTTP server started");
    int counter = 0;
    while(1)
    {
        server.handleClient();
        MDNS.update();  
        delay(500);
        display.clear();
        display.drawString(64, 5, "WIFI AP:wifi_clock");
        display.drawString(64, 20, "192.168.4.1");
        display.drawString(64, 35, "waiting for config wifi.");
        display.drawXbm(46, 50, 8, 8, counter % 3 == 0 ? activeSymbole : inactiveSymbole);
        display.drawXbm(60, 50, 8, 8, counter % 3 == 1 ? activeSymbole : inactiveSymbole);
        display.drawXbm(74, 50, 8, 8, counter % 3 == 2 ? activeSymbole : inactiveSymbole);
        display.display();  
        counter++;
        
        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("HtmlConfig Success");
            Serial.printf("SSID:%s\r\n", WiFi.SSID().c_str());
            Serial.printf("PSW:%s\r\n", WiFi.psk().c_str());
            Serial.println("HTML连接成功");
            break;
        }
    }
       server.close();  
       WiFi.mode(WIFI_STA);
}


void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println();

    // initialize dispaly
    display.init();
    display.clear();
    display.display();

    // set display prop
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setContrast(255);
    display.flipScreenVertically();

    bool wifiConfig = autoConfig();
    if(wifiConfig == false) {
        htmlConfig();//HTML配网
    }

    ui.setTargetFPS(30);

    ui.setActiveSymbol(activeSymbole);
    ui.setInactiveSymbol(inactiveSymbole);

    // You can change this to
    // TOP, LEFT, BOTTOM, RIGHT
    ui.setIndicatorPosition(BOTTOM);

    // Defines where the first frame is located in the bar.
    ui.setIndicatorDirection(LEFT_RIGHT);

    // You can change the transition that is used
    // SLIDE_LEFT, SLIDE_RIGHT, SLIDE_TOP, SLIDE_DOWN
    ui.setFrameAnimation(SLIDE_LEFT);

    ui.setFrames(frames, numberOfFrames);
    ui.setTimePerFrame(7500);
    ui.setOverlays(overlays, numberOfOverlays);

    // Inital UI takes care of initalising the display too.
    ui.init();

    Serial.println("");
    configTime(TZ_SEC, DST_SEC, "pool.ntp.org","0.cn.pool.ntp.org","1.cn.pool.ntp.org");
    updateData(&display);
   
    pinMode(button_wifi, INPUT); // 按钮输入
}


void loop() {
    if (millis() - timeSinceLastWUpdate > (1000L*UPDATE_INTERVAL_SECS)) {
        setReadyForWeatherUpdate();
        timeSinceLastWUpdate = millis();
    }
    
    if (millis() - timeSinceLastCurrUpdate > (1000L*UPDATE_CURR_INTERVAL_SECS)) {
        if( ui.getUiState()->frameState == FIXED) {
//            currTemp=String(ds.getTempC(), 1);
            timeSinceLastCurrUpdate = millis();
        }
    }

    if (readyForWeatherUpdate && ui.getUiState()->frameState == FIXED) {
        updateData(&display);
    }

    int remainingTimeBudget = ui.update();

    if (remainingTimeBudget > 0) {
    // You can do some work here
    // Don't do stuff if you are below your
    // time budget.
        delay(remainingTimeBudget);
    }

// wifi 按钮超过5s清除密码
    temp_wifi = digitalRead(button_wifi);
    if (temp_wifi == HIGH) {
        wifi_pin_lh = 1;
        if (wifi_status_old == 0){
            wifi_status_old =1;
            btn_time_s = millis();
        }
    }
    else {
        if (wifi_pin_lh ==1) {
            btn_time_e = millis();
            btn_time_dur = btn_time_e - btn_time_s;

            if (btn_time_dur > 5000) {
                Serial.println("it is clearing wifi passwd now !!!");
                Serial.println("it is clearing wifi passwd now !!!");
                Serial.println("it is clearing wifi passwd now !!!");
                Serial.println(btn_time_dur);
                WiFi.disconnect(true);
                delay(3000);
                Serial.println("it will restart!!!");
                ESP.restart();
                //ESP.reset();
            }
        }
    }


}

void drawProgress(OLEDDisplay *display, int percentage, String label) {
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(ArialMT_Plain_10);
    display->drawString(64, 10, label);
    display->drawProgressBar(2, 28, 124, 10, percentage);
    display->display();
}

// aaa
void updateData(OLEDDisplay *display) {
    display->flipScreenVertically();
    drawProgress(display, 30, "Updating weather...");

    for(int i=0;i<5;i++){
        HeFengClient.doUpdateCurr(&currentWeather, HEFENG_KEY, HEFENG_LOCATION);
        if(currentWeather.cond_txt!="no network"){
            break;
        }
    }
    drawProgress(display, 50, "Updating forecasts...");
  
    for(int i=0;i<5;i++){
        HeFengClient.doUpdateFore(foreWeather, HEFENG_KEY, HEFENG_LOCATION);
        if(foreWeather[0].datestr!="N/A"){
            break;
        }
    }
 
    readyForWeatherUpdate = false;
    drawProgress(display, 100, "Done...");
    delay(1000);
}

// aaa
void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
    now = time(nullptr);
    struct tm* timeInfo;
    timeInfo = localtime(&now);
    char buff[16];

    display->flipScreenVertically();

    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(ArialMT_Plain_16);
    String date = WDAY_NAMES[timeInfo->tm_wday];
 
    sprintf_P(buff, PSTR("%04d-%02d-%02d, %s"), timeInfo->tm_year + 1900, timeInfo->tm_mon+1, timeInfo->tm_mday, WDAY_NAMES[timeInfo->tm_wday].c_str());
    display->drawString(64 + x, 5 + y, String(buff));
    display->setFont(ArialMT_Plain_24);

    sprintf_P(buff, PSTR("%02d:%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec);
    display->drawString(64 + x, 22 + y, String(buff));
    display->setTextAlignment(TEXT_ALIGN_LEFT);
}

// aaa
void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
    display->flipScreenVertically();
    display->setFont(ArialMT_Plain_10);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(64 + x, 38 + y, currentWeather.cond_txt+" | Wind: "+currentWeather.wind_sc);

    display->setFont(ArialMT_Plain_24);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    String temp = currentWeather.tmp + "°C" ;
    display->drawString(60 + x, 3 + y, temp);
    display->setFont(ArialMT_Plain_10);
    display->drawString(70 + x, 26 + y, currentWeather.fl+"°C | "+currentWeather.hum+"%");
    display->setFont(Meteocons_Plain_36);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(32 + x, 0 + y, currentWeather.iconMeteoCon);
}

// aaa
void drawForecast(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
    display->flipScreenVertically();
    drawForecastDetails(display, x, y, 0);
    drawForecastDetails(display, x + 44, y, 1);
    drawForecastDetails(display, x + 88, y, 2);
}

// aaa
void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex) {
    display->flipScreenVertically();
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(ArialMT_Plain_10);
    display->drawString(x + 20, y, foreWeather[dayIndex].datestr);
    display->setFont(Meteocons_Plain_21);
    display->drawString(x + 20, y + 12, foreWeather[dayIndex].iconMeteoCon);

    String temp=foreWeather[dayIndex].tmp_min+" | "+foreWeather[dayIndex].tmp_max;
    display->setFont(ArialMT_Plain_10);
    display->drawString(x + 20, y + 34, temp);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
}

// aaa
void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state) {
    now = time(nullptr);
    struct tm* timeInfo;
    timeInfo = localtime(&now);
    char buff[14];
    sprintf_P(buff, PSTR("%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min);

    display->flipScreenVertically();
    display->setColor(WHITE);
    display->setFont(ArialMT_Plain_10);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(6, 54, String(buff));
    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    String temp =currTemp +"°C";
    display->drawString(128, 54, temp);
    display->drawHorizontalLine(0, 52, 128);
}

void setReadyForWeatherUpdate() {
    Serial.println("Setting readyForUpdate to true");
    readyForWeatherUpdate = true;
}
