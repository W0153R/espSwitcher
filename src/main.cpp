#include <WiFi.h>
#include <esp_wifi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <RemoteTransmitter.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <Ticker.h>
#include <Preferences.h>
#include "esp32-hal.h"
#include "lwip/apps/sntp.h"

#define LVGL_TICK_PERIOD 20
#define REPEAT_CAL false
uint16_t lastX = 0,
         lastY = 0;
static lv_res_t buttonClicked(lv_obj_t * btn);

Ticker tick;
TFT_eSPI tft = TFT_eSPI();
#define backlightPin 16
bool backlEnable = true;
bool touchEnable = true;
uint8_t blIntensity = 128;
uint8_t blTimer = 0;
long touchTimer = millis();

struct tm timeInfo;
time_t alarmTimeT;
uint8_t alarmHour;
uint8_t alarmMin;
bool alarmAct;
uint8_t secondOld = 0;
lv_obj_t *hourRoll;
lv_obj_t *minuteRoll;
lv_obj_t *secondRoll;
lv_obj_t *alarmActive;
bool repeatAlarm = false;
bool badkamerAlarm = false;

#define weerURL "http://weerlive.nl/api/json-data-10min.php?key=XXXXXXXXXXXXXXXXXX&locatie=XXXXXXXXXXXXXXXXXX"
const size_t weerJSONcap = JSON_ARRAY_SIZE(1) + JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(49) + 650;
lv_obj_t *weatherContainer;
lv_obj_t *tempText;
lv_obj_t *humidText;
lv_obj_t *weatherIcon;
lv_obj_t *windSpeedIcon;
lv_obj_t *windDirIcon;
lv_obj_t *weatherPrediction;
lv_obj_t *weatherPredictionText;
lv_obj_t *sunUpText;
lv_obj_t *sunDownText;
lv_obj_t *regenPerc;
lv_obj_t *zonPerc;
lv_obj_t *celciusSymbol;
uint16_t weerTimer = 0;

uint8_t wifiTimer = 0;

/* URL example:
 * http://10.0.0.5/sendSignal?device=1&onOff=1    value must be 0 or 1
 *
 * Device list:
 * 1 = Kamer        = A1
 * 2 = Badkamer     = A2
 * 3 = Slaapkamer1  = A3
 * 4 = Slaapkamer2  = D2
 */

WebServer server(80);
KaKuTransmitter transmitter(4);

const char pageString[] PROGMEM = "<!DOCTYPE HTML>\r\n\
<html style=\"text-align:center\">\r\n\
<head>\r\n\
<title>espSwitcher</title>\r\n\
<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no\">\r\n\
<script type=\"text/javascript\">function setPage(){var elems=document.getElementsByTagName('button');var newHeight=document.documentElement.clientHeight/4+'px';for(var i=0;i<elems.length;i++){elems[i].style.height=newHeight;}}function load(url){var xhr=new XMLHttpRequest();xhr.open('GET',url,true);xhr.send('');}</script>\r\n\
<style type=\"text/css\">button{width:50%;border:1px solid white;color:white;padding:15px 32px;text-align:center;text-decoration:none;display:inline-block;font-size:16px}.uit{background-color:#f44336}.aan{background-color:#4CAF50}body{margin:0;padding:0}</style>\r\n\
</head>\r\n\
<body  onload=\"setPage()\">\r\n\
<button class=\"aan\" onclick=\"load('/sendSignal?device=1&onOff=1')\">Kamer aan</button><button class=\"uit\" onclick=\"load('/sendSignal?device=1&onOff=0')\">Kamer uit</button>\r\n\
<br><button class=\"aan\" onclick=\"load('/sendSignal?device=2&onOff=1')\">Badkamer aan</button><button class=\"uit\" onclick=\"load('/sendSignal?device=2&onOff=0')\">Badkamer uit</button>\r\n\
<button class=\"aan\" onclick=\"load('/sendSignal?device=3&onOff=1')\">Slaapkamer 1 aan</button><button class=\"uit\" onclick=\"load('/sendSignal?device=3&onOff=0')\">Slaapkamer 1 uit</button>\r\n\
<br><button class=\"aan\" onclick=\"load('/sendSignal?device=4&onOff=1')\">Slaapkamer 2 aan</button><button class=\"uit\" onclick=\"load('/sendSignal?device=4&onOff=0')\">Slaapkamer 2 uit</button>\r\n\
</body>\r\n\
</html>\n";

void writePage() {
  server.send(202, "text/html", FPSTR(pageString));
}

void transmitSignal(uint8_t device, bool onOff) {
  if (device == 4) {
    transmitter.sendSignal('D', 2, onOff);
  } else {
    transmitter.sendSignal('A', device, onOff);
  }
}

void handleCommand() {
  server.send(200, "text/html", "");
  if (server.uri() == "/sendSignal")  {
    int device = server.arg(0).toInt();
    bool onOff = server.arg(1).toInt();
    transmitSignal(device, onOff);
  }
}

time_t unixTime(uint8_t hour = timeInfo.tm_hour, uint8_t min = timeInfo.tm_min, uint8_t sec = timeInfo.tm_sec) {
  struct tm tm;
  tm.tm_year = timeInfo.tm_year - 1900;
  tm.tm_mon = timeInfo.tm_mon;
  tm.tm_mday = timeInfo.tm_mday;
  tm.tm_hour = hour;
  tm.tm_min = min;
  tm.tm_sec = sec;
  return mktime(&tm);
}

bool getTime() {
  uint32_t start = millis();
  time_t now;
  while ((millis() - start) <= 5000) {
    time(&now);
    localtime_r(&now, &timeInfo);
    if (timeInfo.tm_year > (2016 - 1900)) {
      return true;
    }
    delay(10);
  }
  return false;
}

void saveSettings() {
  Preferences prefs;
  if (prefs.begin("alarmSettings", false)) {
    prefs.putBool("alarmActive", alarmAct);
    prefs.putBool("repeatAlarm", repeatAlarm);
    prefs.putBool("badkamerAlarm", badkamerAlarm);
    prefs.putUChar("alarmHour", alarmHour);
    prefs.putUChar("alarmMin", alarmMin);
    prefs.putUChar("backLight", blIntensity);
  }
  prefs.end();
}

void setAlarmTimeT() {
  alarmTimeT = unixTime(alarmHour, alarmMin, 0);
  if (alarmHour < timeInfo.tm_hour || (alarmHour == timeInfo.tm_hour && alarmMin <= timeInfo.tm_min)) { //alarm is not today
    alarmTimeT += (24 * 60 * 60);
  }
}

bool getWeather() {
  if (WiFi.status() == WL_CONNECTED) {
  	String payload = "";
    HTTPClient http;
  	http.begin(weerURL);
  	int httpCode = http.GET();

  	if(httpCode > 0 && httpCode == HTTP_CODE_OK) {
			payload = http.getString();
			http.end();

      DynamicJsonDocument weerJSONdoc(weerJSONcap);
      DeserializationError weerJSONerror = deserializeJson(weerJSONdoc, payload);
      if (!weerJSONerror) {
    		JsonObject weerJSON = weerJSONdoc["liveweer"][0];
        const char* temp = weerJSON["temp"];
        const char* lv = weerJSON["lv"];
        const char* windr = weerJSON["windr"];
        uint8_t winds = weerJSON["winds"].as<uint8_t>();
        const char* verw = weerJSON["verw"];
        const char* sup = weerJSON["sup"];
        const char* sunder = weerJSON["sunder"];
        const char* beschrv = weerJSON["image"];

        if(temp != nullptr && lv != nullptr && windr != nullptr && verw != nullptr && sup != nullptr && sunder != nullptr && beschrv != nullptr) {
          char neerslag[5];
          char zon[5];
          strcpy(neerslag, weerJSON["d0neerslag"]);
          strcpy(zon, weerJSON["d0zon"]);
          strcat(neerslag, "%");
          strcat(zon, "%");

          bool nacht = false;
          char supHourChar[3] = { sup[0], sup[1], '\0' };
          char supMinuteChar[3] = { sup[3], sup[4], '\0' };
          uint8_t supHour = atoi(supHourChar);
          uint8_t supMinute = atoi(supMinuteChar);
          char sunderHourChar[3] = { sunder[0], sunder[1], '\0' };
          char sunderMinuteChar[3] = { sunder[3], sunder[4], '\0' };
          uint8_t sunderHour = atoi(sunderHourChar);
          uint8_t sunderMinute = atoi(sunderMinuteChar);
          if (timeInfo.tm_hour < supHour || timeInfo.tm_hour > sunderHour || \
              (timeInfo.tm_hour == supHour && timeInfo.tm_min < supMinute) || \
              (timeInfo.tm_hour == sunderHour && timeInfo.tm_min > sunderMinute)) {
                nacht = true;
          }

          char samenv[25];
          strcpy(samenv, weerJSON["samenv"]);
          strlwr(samenv);
          if (strstr(samenv, "motregen") || strstr(samenv, "lichte")) {
            strcpy(samenv, "motregen");
          } else {
            strcpy(samenv, beschrv);
          }
          char ico = ' ';
          if (strstr(samenv, "zonnig")) {
            ico = nacht ? 'D' : '8';
          } else if (strstr(samenv, "bliksem")) {
            ico = nacht ? '>' : ';';
          } else if (strstr(samenv, "motregen")) {
            ico = nacht ? 'C' : 'H';
          } else if (strstr(samenv, "regen")) {
            ico = nacht ? '@' : 'F';
          } else if (strstr(samenv, "buien")) {
            ico = nacht ? 'A' : 'H';
          } else if (strstr(samenv, "hagel")) {
            ico = nacht ? '=' : ':';
          } else if (strstr(samenv, "sneeuw")) {
            ico = nacht ? 'B' : 'G';
          } else if (strstr(samenv, "halfbewolkt")) {
            ico = nacht ? '?' : '7';
          } else if (strstr(samenv, "zwaarbewolkt")) {
            ico = nacht ? '<' : '6';
          } else if (strstr(samenv, "bewolkt")) {
            ico = nacht ? '<' : '5';
          } else if (strstr(samenv, "nachtmist")) {
            ico = 'E';
          } else if (strstr(samenv, "mist")) {
            ico = nacht ? 'E' : '9';
          } else if (strstr(samenv, "helderenacht")) {
            ico = 'D';
          } else if (strstr(samenv, "wolkennacht")) {
            ico = '<';
          }
          const char wIcon[2] = { ico, '\0' };

          char windrIco = ' ';
          if (strstr(windr, "Noord")) {
            windrIco = 'X';
          } else if (strstr(windr, "NNO")) {
            windrIco = 'Y';
          } else if (strstr(windr, "ONO")) {
            windrIco = '[';
          } else if (strstr(windr, "NO")) {
            windrIco = 'Z';
          } else if (strstr(windr, "Oost")) {
            windrIco = '\\';
          } else if (strstr(windr, "OZO")) {
            windrIco = ']';
          } else if (strstr(windr, "ZZO")) {
            windrIco = '_';
          } else if (strstr(windr, "ZO")) {
            windrIco = '^';
          } else if (strstr(windr, "Zuid")) {
            windrIco = '`';
          } else if (strstr(windr, "ZZW")) {
            windrIco = 'a';
          } else if (strstr(windr, "WZW")) {
            windrIco = 'c';
          } else if (strstr(windr, "ZW")) {
            windrIco = 'b';
          } else if (strstr(windr, "West")) {
            windrIco = 'd';
          } else if (strstr(windr, "WNW")) {
            windrIco = 'e';
          } else if (strstr(windr, "NNW")) {
            windrIco = '1';
          } else if (strstr(windr, "NW")) {
            windrIco = '0';
          }
          const char windrIcon[2] = { windrIco, '\0' };

          char windsIco = ' ';
          switch (winds) {
            case 0:
              windsIco = 'K';
              break;
            case 1:
              windsIco = 'L';
              break;
            case 2:
              windsIco = 'M';
              break;
            case 3:
              windsIco = 'N';
              break;
            case 4:
              windsIco = 'O';
              break;
            case 5:
              windsIco = 'P';
              break;
            case 6:
              windsIco = 'Q';
              break;
            case 7:
              windsIco = 'R';
              break;
            case 8:
              windsIco = 'S';
              break;
            case 9:
              windsIco = 'T';
              break;
            case 10:
              windsIco = 'U';
              break;
            case 11:
              windsIco = 'V';
              break;
            case 12:
              windsIco = 'W';
          }
          const char windsIcon[2] = { windsIco, '\0' };

          lv_label_set_text(tempText, temp);
          lv_obj_align(celciusSymbol, tempText, LV_ALIGN_OUT_RIGHT_MID, 1, 0);
          lv_label_set_text(humidText, lv);
          lv_label_set_text(weatherIcon, wIcon);
          lv_label_set_text(windSpeedIcon, windsIcon);
          lv_label_set_text(windDirIcon, windrIcon);
          lv_label_set_text(weatherPredictionText, verw);
          lv_label_set_text(sunUpText, sup);
          lv_label_set_text(sunDownText, sunder);
          lv_label_set_text(regenPerc, neerslag);
          lv_label_set_text(zonPerc, zon);

          lv_obj_align(weatherPrediction, weatherContainer, LV_ALIGN_OUT_TOP_MID, 0, 0);

          return true;
        }
      }
  	} else {
  		http.end();
  	}
  }
  return false;
}

void calibrateDisplay(bool manualTrigger) {
  uint16_t calData[5];
  bool calDataOK = false;
  Preferences prefs;

  if (prefs.begin("touchCal", false)) {
    if (manualTrigger) {
      prefs.clear();
    } else {
      calData[0] = prefs.getUShort("calDat0", 0);
      calData[1] = prefs.getUShort("calDat1", 0);
      calData[2] = prefs.getUShort("calDat2", 0);
      calData[3] = prefs.getUShort("calDat3", 0);
      calData[4] = prefs.getUShort("calDat4", 0);
      for (uint8_t i = 0; i < 5; i++) {
        calDataOK = calData[i] != 0 ? true : calDataOK;
      }
    }

    if (calDataOK) {
      tft.setTouch(calData);
    } else {
      tft.fillScreen(TFT_BLACK);
      tft.setCursor(20, 0);
      tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);
      prefs.putUShort("calDat0", calData[0]);
      prefs.putUShort("calDat1", calData[1]);
      prefs.putUShort("calDat2", calData[2]);
      prefs.putUShort("calDat3", calData[3]);
      prefs.putUShort("calDat4", calData[4]);
    }
  }
  prefs.end();
}

void flushDisplay(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const lv_color_t *color_p) {
  uint16_t c;
  tft.startWrite(); /* Start new TFT transaction */
  tft.setAddrWindow(x1, y1, (x2 - x1 + 1), (y2 - y1 + 1)); /* set the working window */
  for (int y = y1; y <= y2; y++) {
    for (int x = x1; x <= x2; x++) {
      c = color_p->full;
      tft.writeColor(c, 1);
      color_p++;
    }
  }
  tft.endWrite(); /* terminate TFT transaction */
  lv_flush_ready(); /* tell lvgl that flushing is done */
}

static void lvglTimerTicker() { lv_tick_inc(LVGL_TICK_PERIOD); }

bool readTouchscreen(lv_indev_data_t * data) {
  uint16_t newX = 0,
           newY = 0;
  bool pressed = tft.getTouch(&newX, &newY);
  data->state = LV_INDEV_STATE_REL;
  if (pressed) {
    if (backlEnable && !touchEnable && (millis() - touchTimer) >= 500) {
      touchEnable = true;
    } else if (!backlEnable) {
      backlEnable = true;
      ledcWrite(0, blIntensity);
      touchTimer = millis();
    }
    if (touchEnable) {
      bool weatherPredicHidden = lv_obj_get_hidden(weatherPrediction);
      if (newY > 225 && weatherPredicHidden) {
        lv_obj_set_hidden(weatherPrediction, false);
      } else if (newY <= 225 && !weatherPredicHidden) {
        lv_obj_set_hidden(weatherPrediction, true);
      } else {
        lastX = newX;
        lastY = newY;
        data->state = LV_INDEV_STATE_PR;
      }
    }
    blTimer = 0;
  }
  data->point.x = lastX;
  data->point.y = lastY;
  return false;
}

void createTabs() {
  lv_style_pretty.body.main_color = LV_COLOR_BLACK; // rollers background
  lv_style_pretty.body.grad_color = LV_COLOR_GRAY; // rollers background
  lv_style_pretty.text.color = LV_COLOR_SILVER; // rollers text
  lv_style_plain_color.text.color = LV_COLOR_WHITE; // rollers selected text
  lv_style_plain.text.color = LV_COLOR_WHITE; // label text

  lv_style_plain.body.main_color = LV_COLOR_BLACK; // page background
  lv_style_plain.body.grad_color = LV_COLOR_GRAY; // page background
  lv_style_plain_color.body.opa = 0; // remove tab bar indicator
  lv_style_transp.body.padding.hor = 0; // remove tab padding horizontal
  lv_style_transp.body.padding.ver = 0; // remove tab padding vertical
  lv_style_transp.body.padding.inner = 0; // remove tab spacing
  lv_style_transp.text.color = LV_COLOR_WHITE;

  lv_style_btn_rel.body.radius = 0; // (inactive tab) button
  lv_style_btn_rel.body.padding.ver = 5;
  lv_style_btn_rel.text.color = LV_COLOR_SILVER;
  lv_style_btn_rel.body.main_color = LV_COLOR_BLACK;
  lv_style_btn_rel.body.grad_color = LV_COLOR_GRAY;

  lv_style_btn_pr.body.radius = 0; // (inactive tab) button pressed
  lv_style_btn_pr.body.padding.ver = 5;
  lv_style_btn_pr.text.color = LV_COLOR_WHITE;
  lv_style_btn_pr.body.main_color = LV_COLOR_GRAY;
  lv_style_btn_pr.body.grad_color = LV_COLOR_BLACK;

  lv_style_btn_tgl_rel.body.radius = 0; // (active tab/toggle) button
  lv_style_btn_tgl_rel.body.padding.ver = 5;
  lv_style_btn_tgl_rel.text.color = LV_COLOR_WHITE;
  lv_style_btn_tgl_rel.body.main_color = LV_COLOR_GRAY;
  lv_style_btn_tgl_rel.body.grad_color = LV_COLOR_BLACK;

  lv_style_btn_tgl_pr.body.radius = 0; // (active tab/toggle) button pressed
  lv_style_btn_tgl_pr.body.padding.ver = 5;
  lv_style_btn_tgl_pr.text.color = LV_COLOR_SILVER;
  lv_style_btn_tgl_pr.body.main_color = LV_COLOR_BLACK;
  lv_style_btn_tgl_pr.body.grad_color = LV_COLOR_GRAY;

  static lv_style_t styleOnSwitchRel;
  lv_style_copy(&styleOnSwitchRel, &lv_style_btn_rel);
  styleOnSwitchRel.body.main_color = LV_COLOR_GREEN;
  styleOnSwitchRel.body.grad_color = LV_COLOR_BLACK;
  styleOnSwitchRel.body.radius = 100;
  styleOnSwitchRel.text.color = LV_COLOR_WHITE;

  static lv_style_t styleOnSwitchPr;
  lv_style_copy(&styleOnSwitchPr, &lv_style_btn_pr);
  styleOnSwitchPr.body.main_color = LV_COLOR_BLACK;
  styleOnSwitchPr.body.grad_color = LV_COLOR_GREEN;
  styleOnSwitchPr.body.radius = 100;

  static lv_style_t styleOffSwitchRel;
  lv_style_copy(&styleOffSwitchRel, &lv_style_btn_rel);
  styleOffSwitchRel.body.main_color = LV_COLOR_RED;
  styleOffSwitchRel.body.grad_color = LV_COLOR_BLACK;
  styleOffSwitchRel.body.radius = 100;
  styleOffSwitchRel.text.color = LV_COLOR_WHITE;

  static lv_style_t styleOffSwitchPr;
  lv_style_copy(&styleOffSwitchPr, &lv_style_btn_pr);
  styleOffSwitchPr.body.main_color = LV_COLOR_BLACK;
  styleOffSwitchPr.body.grad_color = LV_COLOR_RED;
  styleOffSwitchPr.body.radius = 100;

  static lv_style_t styleNormalButtonRel;
  lv_style_copy(&styleNormalButtonRel, &lv_style_btn_rel);
  styleNormalButtonRel.body.radius = 15;

  static lv_style_t styleNormalButtonPr;
  lv_style_copy(&styleNormalButtonPr, &lv_style_btn_pr);
  styleNormalButtonPr.body.radius = 15;

  static lv_style_t weatherContainerStyle;
  lv_style_copy(&weatherContainerStyle, &lv_style_scr);
  weatherContainerStyle.body.main_color = LV_COLOR_GRAY;
  weatherContainerStyle.body.grad_color = LV_COLOR_BLACK;
  weatherContainerStyle.body.padding.ver = 0;
  weatherContainerStyle.text.font = &weerIconen;
  weatherContainerStyle.text.color = LV_COLOR_WHITE;

  lv_obj_t *tabview;
  tabview = lv_tabview_create(lv_scr_act(), NULL);
  lv_tabview_set_anim_time(tabview, 500);
  lv_obj_set_size(tabview, lv_obj_get_width(lv_scr_act()), lv_obj_get_height(lv_scr_act()) - 20);
  lv_obj_t *switch1 = lv_tabview_add_tab(tabview, "Bed");
  lv_obj_t *switch2 = lv_tabview_add_tab(tabview, "Other");
  lv_obj_t *alarm = lv_tabview_add_tab(tabview, "Alarm");
  lv_obj_t *settings = lv_tabview_add_tab(tabview, "Setting");

  weatherContainer = lv_cont_create(lv_scr_act(), NULL);
  lv_cont_set_style(weatherContainer, &weatherContainerStyle);
  lv_cont_set_fit(weatherContainer, false, false);
  lv_cont_set_layout(weatherContainer, LV_LAYOUT_PRETTY);
  lv_obj_set_size(weatherContainer, lv_obj_get_width(lv_scr_act()), 20);
  lv_obj_align(weatherContainer, lv_scr_act(), LV_ALIGN_IN_BOTTOM_LEFT, 0, 0);

  lv_obj_t *tempContainer = lv_cont_create(weatherContainer, NULL);
  lv_cont_set_style(tempContainer, &lv_style_transp_tight);
  lv_cont_set_layout(tempContainer, LV_LAYOUT_OFF);
  lv_cont_set_fit(tempContainer, true, true);   // true, false -> heightShit

  lv_obj_t *tempSymbol = lv_label_create(tempContainer, NULL);
  lv_obj_set_style(tempSymbol, &weatherContainerStyle);
  lv_label_set_text(tempSymbol, "4");

  tempText = lv_label_create(tempContainer, NULL);
  lv_obj_align(tempText, tempSymbol, LV_ALIGN_OUT_RIGHT_MID, 5, 1);
  lv_obj_set_style(tempText, &lv_style_transp);
  lv_label_set_text(tempText, "00.0");

  celciusSymbol = lv_label_create(tempContainer, NULL);
  lv_obj_align(celciusSymbol, tempText, LV_ALIGN_OUT_RIGHT_MID, 1, 0);
  lv_label_set_text(celciusSymbol, "3");

  lv_obj_t *humidContainer = lv_cont_create(weatherContainer, tempContainer);

  humidText = lv_label_create(humidContainer, NULL);
  lv_obj_align(humidText, humidContainer, LV_ALIGN_IN_TOP_LEFT, 0, 1);
  lv_obj_set_style(humidText, &lv_style_transp);
  lv_label_set_text(humidText, "00");

  lv_obj_t *humidIcon = lv_label_create(humidContainer, NULL);
  lv_obj_align(humidIcon, humidText, LV_ALIGN_OUT_RIGHT_MID, 2, -1);
  lv_label_set_text(humidIcon, "2");

  weatherIcon = lv_label_create(weatherContainer, NULL);
  lv_label_set_text(weatherIcon, "8");

  lv_obj_t *windContainer = lv_cont_create(weatherContainer, tempContainer);

  windSpeedIcon = lv_label_create(windContainer, NULL);
  lv_label_set_text(windSpeedIcon, "K");

  windDirIcon = lv_label_create(windContainer, NULL);
  lv_obj_align(windDirIcon, windSpeedIcon, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
  lv_label_set_text(windDirIcon, "X");

  weatherPrediction = lv_cont_create(lv_scr_act(), NULL);
  lv_cont_set_fit(weatherPrediction, false, true);
  lv_cont_set_layout(weatherPrediction, LV_LAYOUT_CENTER);
  lv_obj_set_hidden(weatherPrediction, true);
  lv_obj_set_width(weatherPrediction, lv_obj_get_width(lv_scr_act()) - 40);

  weatherPredictionText = lv_label_create(weatherPrediction, NULL);
  lv_label_set_align(weatherPredictionText, LV_LABEL_ALIGN_CENTER);
  lv_label_set_text(weatherPredictionText, "");
  lv_label_set_long_mode(weatherPredictionText, LV_LABEL_LONG_BREAK);
  lv_obj_set_style(weatherPredictionText, &lv_style_transp);
  lv_obj_set_width(weatherPredictionText, lv_obj_get_width(weatherPrediction) - 20);

  lv_obj_t *sunContainer = lv_cont_create(weatherPrediction, tempContainer);

  lv_obj_t *sunUpIcon = lv_label_create(sunContainer, NULL);
  lv_obj_set_style(sunUpIcon, &weatherContainerStyle);
  lv_label_set_text(sunUpIcon, "I");

  sunUpText = lv_label_create(sunContainer, NULL);
  lv_obj_align(sunUpText, sunUpIcon, LV_ALIGN_OUT_RIGHT_MID, 10, 1);
  lv_obj_set_style(sunUpText, &lv_style_transp);
  lv_label_set_text(sunUpText, "00:00");

  lv_obj_t *sunDownIcon = lv_label_create(sunContainer, sunUpIcon);
  lv_obj_align(sunDownIcon, sunUpText, LV_ALIGN_OUT_RIGHT_MID, 20, -1);
  lv_label_set_text(sunDownIcon, "J");

  sunDownText = lv_label_create(sunContainer, sunUpText);
  lv_obj_align(sunDownText, sunDownIcon, LV_ALIGN_OUT_RIGHT_MID, 10, 1);
  lv_label_set_text(sunDownText, "00:00");

  lv_obj_t *zonregenContainer = lv_cont_create(weatherPrediction, tempContainer);

  lv_obj_t *regenPercLabel = lv_label_create(zonregenContainer, sunUpIcon);
  lv_label_set_text(regenPercLabel, "F");

  regenPerc = lv_label_create(zonregenContainer, sunUpText);
  lv_obj_align(regenPerc, regenPercLabel, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
  lv_label_set_text(regenPerc, "00%");

  lv_obj_t *zonPercLabel = lv_label_create(zonregenContainer, sunUpIcon);
  lv_obj_align(zonPercLabel, regenPerc, LV_ALIGN_OUT_RIGHT_MID, 20, 0);
  lv_label_set_text(zonPercLabel, "8");

  zonPerc = lv_label_create(zonregenContainer, sunUpText);
  lv_obj_align(zonPerc, zonPercLabel, LV_ALIGN_OUT_RIGHT_MID, 10, 1);
  lv_label_set_text(zonPerc, "00%");

  lv_obj_align(weatherPrediction, weatherContainer, LV_ALIGN_OUT_TOP_MID, 0, 0);

  lv_obj_t *container1 = lv_cont_create(switch1, tempContainer);
  lv_cont_set_fit(container1, false, false);
  lv_obj_set_size(container1, lv_obj_get_width(switch1), lv_obj_get_height(switch1));

  lv_obj_t *bedBtn1on = lv_btn_create(container1, NULL);
  lv_btn_set_style(bedBtn1on, LV_BTN_STYLE_REL, &styleOnSwitchRel);
  lv_btn_set_style(bedBtn1on, LV_BTN_STYLE_PR, &styleOnSwitchPr);
  lv_btn_set_action(bedBtn1on, LV_BTN_ACTION_CLICK, buttonClicked);
  lv_obj_align(bedBtn1on, container1, LV_ALIGN_IN_TOP_LEFT, 10, 10);
  lv_obj_set_size(bedBtn1on, (lv_obj_get_width(container1) / 2) - 15, (lv_obj_get_height(container1) / 2) - 15);
  lv_obj_set_free_num(bedBtn1on, 1);
  lv_obj_t *label = lv_label_create(bedBtn1on, NULL);
  lv_label_set_text(label, "Bed 1");

  lv_obj_t *bedBtn1off = lv_btn_create(container1, bedBtn1on);
  lv_btn_set_style(bedBtn1off, LV_BTN_STYLE_REL, &styleOffSwitchRel);
  lv_btn_set_style(bedBtn1off, LV_BTN_STYLE_PR, &styleOffSwitchPr);
  lv_obj_align(bedBtn1off, bedBtn1on, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
  lv_obj_set_free_num(bedBtn1off, 2);
  label = lv_label_create(bedBtn1off, NULL);
  lv_label_set_text(label, "Bed 1");

  lv_obj_t *bedBtn2on = lv_btn_create(container1, bedBtn1on);
  lv_obj_align(bedBtn2on, bedBtn1on, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
  lv_obj_set_free_num(bedBtn2on, 3);
  label = lv_label_create(bedBtn2on, NULL);
  lv_label_set_text(label, "Bed 2");

  lv_obj_t *bedBtn2off = lv_btn_create(container1, bedBtn1off);
  lv_obj_align(bedBtn2off, bedBtn2on, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
  lv_obj_set_free_num(bedBtn2off, 4);
  label = lv_label_create(bedBtn2off, NULL);
  lv_label_set_text(label, "Bed 2");

  lv_obj_t *container2 = lv_cont_create(switch2, container1);

  lv_obj_t *badkamerOn = lv_btn_create(container2, bedBtn1on);
  lv_obj_align(badkamerOn, container2, LV_ALIGN_IN_TOP_LEFT, 10, 10);
  lv_obj_set_free_num(badkamerOn, 5);
  label = lv_label_create(badkamerOn, NULL);
  lv_label_set_text(label, "Badkamer");

  lv_obj_t *badkamerOff = lv_btn_create(container2, bedBtn1off);
  lv_obj_align(badkamerOff, badkamerOn, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
  lv_obj_set_free_num(badkamerOff, 6);
  label = lv_label_create(badkamerOff, NULL);
  lv_label_set_text(label, "Badkamer");

  lv_obj_t *kamerOn = lv_btn_create(container2, bedBtn1on);
  lv_obj_align(kamerOn, badkamerOn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
  lv_obj_set_free_num(kamerOn, 7);
  label = lv_label_create(kamerOn, NULL);
  lv_label_set_text(label, "Kamer");

  lv_obj_t *kamerOff = lv_btn_create(container2, bedBtn1off);
  lv_obj_align(kamerOff, kamerOn, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
  lv_obj_set_free_num(kamerOff, 8);
  label = lv_label_create(kamerOff, NULL);
  lv_label_set_text(label, "Kamer");

  lv_obj_t *container3 = lv_cont_create(alarm, container1);

  minuteRoll = lv_roller_create(container3, NULL);
  lv_roller_set_options(minuteRoll, "00\n"
                                    "01\n"
                                    "02\n"
                                    "03\n"
                                    "04\n"
                                    "05\n"
                                    "06\n"
                                    "07\n"
                                    "08\n"
                                    "09\n"
                                    "10\n"
                                    "11\n"
                                    "12\n"
                                    "13\n"
                                    "14\n"
                                    "15\n"
                                    "16\n"
                                    "17\n"
                                    "18\n"
                                    "19\n"
                                    "20\n"
                                    "21\n"
                                    "22\n"
                                    "23\n"
                                    "24\n"
                                    "25\n"
                                    "26\n"
                                    "27\n"
                                    "28\n"
                                    "29\n"
                                    "30\n"
                                    "31\n"
                                    "32\n"
                                    "33\n"
                                    "34\n"
                                    "35\n"
                                    "36\n"
                                    "37\n"
                                    "38\n"
                                    "39\n"
                                    "40\n"
                                    "41\n"
                                    "42\n"
                                    "43\n"
                                    "44\n"
                                    "45\n"
                                    "46\n"
                                    "47\n"
                                    "48\n"
                                    "49\n"
                                    "50\n"
                                    "51\n"
                                    "52\n"
                                    "53\n"
                                    "54\n"
                                    "55\n"
                                    "56\n"
                                    "57\n"
                                    "58\n"
                                    "59");
  lv_roller_set_visible_row_count(minuteRoll, 3);
  lv_roller_set_hor_fit(minuteRoll, false);
  lv_roller_set_selected(minuteRoll, timeInfo.tm_min, true);
  lv_obj_set_width(minuteRoll, 50);
  lv_obj_align(minuteRoll, container3, LV_ALIGN_IN_TOP_MID, 0, 10);

  hourRoll = lv_roller_create(container3, minuteRoll);
  lv_roller_set_options(hourRoll, "00\n"
                                  "01\n"
                                  "02\n"
                                  "03\n"
                                  "04\n"
                                  "05\n"
                                  "06\n"
                                  "07\n"
                                  "08\n"
                                  "09\n"
                                  "10\n"
                                  "11\n"
                                  "12\n"
                                  "13\n"
                                  "14\n"
                                  "15\n"
                                  "16\n"
                                  "17\n"
                                  "18\n"
                                  "19\n"
                                  "20\n"
                                  "21\n"
                                  "22\n"
                                  "23");
  lv_roller_set_visible_row_count(hourRoll, 3);
  lv_roller_set_selected(hourRoll, timeInfo.tm_hour, true);
  lv_obj_align(hourRoll, minuteRoll, LV_ALIGN_OUT_LEFT_MID, -10, 0);

  secondRoll = lv_roller_create(container3, minuteRoll);
  lv_roller_set_selected(secondRoll, timeInfo.tm_sec, true);
  lv_obj_align(secondRoll, minuteRoll, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

  lv_obj_t *alarmLabel = lv_label_create(container3, NULL);
  lv_label_set_text(alarmLabel, "Alarm:");
  lv_obj_align(alarmLabel, minuteRoll, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  lv_obj_t *alarmMinuteRoll = lv_roller_create(container3, minuteRoll);
  lv_roller_set_action(alarmMinuteRoll, buttonClicked);
  lv_roller_set_selected(alarmMinuteRoll, alarmMin, true);
  lv_obj_align(alarmMinuteRoll, alarmLabel, LV_ALIGN_OUT_BOTTOM_MID, 30, 10);
  lv_obj_set_free_num(alarmMinuteRoll, 9);

  lv_obj_t *alarmHourRoll = lv_roller_create(container3, hourRoll);
  lv_roller_set_action(alarmHourRoll, buttonClicked);
  lv_roller_set_selected(alarmHourRoll, alarmHour, true);
  lv_obj_align(alarmHourRoll, alarmMinuteRoll, LV_ALIGN_OUT_LEFT_MID, -10, 0);
  lv_obj_set_free_num(alarmHourRoll, 10);

  alarmActive = lv_cb_create(container3, NULL);
  lv_cb_set_text(alarmActive, "");
  lv_cb_set_action(alarmActive, buttonClicked);
  lv_cb_set_checked(alarmActive, alarmAct);
  lv_cb_set_style(alarmActive, LV_CB_STYLE_BOX_PR, &styleOffSwitchPr);
  lv_cb_set_style(alarmActive, LV_CB_STYLE_BOX_TGL_REL, &styleOffSwitchRel);
  lv_cb_set_style(alarmActive, LV_CB_STYLE_BOX_TGL_PR, &styleOffSwitchPr);
  lv_obj_align(alarmActive, alarmMinuteRoll, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
  lv_obj_set_size(alarmActive, 20, 20);
  lv_obj_set_free_num(alarmActive, 11);

  lv_obj_t *container4 = lv_cont_create(settings, container1);

  lv_obj_t *calibrateScreen = lv_btn_create(container4, NULL);
  lv_btn_set_action(calibrateScreen, LV_BTN_ACTION_CLICK, buttonClicked);
  lv_btn_set_style(calibrateScreen, LV_BTN_STYLE_REL, &styleNormalButtonRel);
  lv_btn_set_style(calibrateScreen, LV_BTN_STYLE_PR, &styleNormalButtonPr);
  lv_obj_align(calibrateScreen, container4, LV_ALIGN_IN_TOP_LEFT, 10, 10);
  lv_obj_set_size(calibrateScreen, (lv_obj_get_width(container4) / 2) - 15, lv_obj_get_height(container4) / 4);
  lv_obj_set_free_num(calibrateScreen, 12);
  label = lv_label_create(calibrateScreen, NULL);
  lv_label_set_text(label, "Calibreren");

  lv_obj_t *resetWifi = lv_btn_create(container4, calibrateScreen);
  lv_obj_align(resetWifi, calibrateScreen, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
  lv_obj_set_free_num(resetWifi, 13);
  label = lv_label_create(resetWifi, NULL);
  lv_label_set_text(label, "Reset WiFi");

  lv_obj_t *alarmRepeat = lv_label_create(container4, NULL);
  lv_label_set_text(alarmRepeat, "Alarm herhalen:");
  lv_obj_align(alarmRepeat, calibrateScreen, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);

  lv_obj_t *repeatSwitch = lv_sw_create(container4, NULL);
  lv_sw_set_action(repeatSwitch, buttonClicked);
  if (repeatAlarm) {
    lv_sw_on(repeatSwitch);
  }
  lv_obj_set_free_num(repeatSwitch, 14);
  lv_obj_align(repeatSwitch, alarmRepeat, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
  uint16_t xPosSwitch = lv_obj_get_x(resetWifi) + lv_obj_get_width(resetWifi) - lv_obj_get_width(repeatSwitch);
  lv_obj_set_x(repeatSwitch, xPosSwitch);

  lv_obj_t *alarmBadk = lv_label_create(container4, NULL);
  lv_label_set_text(alarmBadk, "Badkamer bij alarm:");
  lv_obj_align(alarmBadk, alarmRepeat, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);

  lv_obj_t *badkSwitch = lv_sw_create(container4, NULL);
  lv_sw_set_action(badkSwitch, buttonClicked);
  if (badkamerAlarm) {
    lv_sw_on(badkSwitch);
  }
  lv_obj_set_free_num(badkSwitch, 15);
  lv_obj_align(badkSwitch, alarmBadk, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
  lv_obj_set_x(badkSwitch, xPosSwitch);

  lv_obj_t *backLight = lv_label_create(container4, NULL);
  lv_label_set_text(backLight, "Backlight:");
  lv_obj_align(backLight, alarmBadk, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);

  lv_obj_t *backlSlider = lv_slider_create(container4, NULL);
  lv_slider_set_action(backlSlider, buttonClicked);
  lv_slider_set_range(backlSlider, 10 , 255);
  lv_slider_set_value(backlSlider, blIntensity);
  lv_obj_set_free_num(backlSlider, 16);
  lv_obj_align(backlSlider, backLight, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
}

static lv_res_t buttonClicked(lv_obj_t * btn) {
  uint8_t id = lv_obj_get_free_num(btn);
  switch (id) {
    case 1:
      transmitSignal(3, true);
      break;
    case 2:
      transmitSignal(3, false);
      break;
    case 3:
      transmitSignal(4, true);
      break;
    case 4:
      transmitSignal(4, false);
      break;
    case 5:
      transmitSignal(2, true);
      break;
    case 6:
      transmitSignal(2, false);
      break;
    case 7:
      transmitSignal(1, true);
      break;
    case 8:
      transmitSignal(1, false);
      break;
    case 9:
      alarmMin = lv_roller_get_selected(btn);
      setAlarmTimeT();
      saveSettings();
      break;
    case 10:
      alarmHour = lv_roller_get_selected(btn);
      setAlarmTimeT();
      saveSettings();
      break;
    case 11:
      alarmAct = lv_cb_is_checked(btn);
      saveSettings();
      break;
    case 12:
      calibrateDisplay(true);
      createTabs();
      break;
    case 13:
      WiFi.disconnect(true,true);
      delay(1000);
      ESP.restart();
      break;
    case 14:
      repeatAlarm = lv_sw_get_state(btn);
      saveSettings();
      break;
    case 15:
      badkamerAlarm = lv_sw_get_state(btn);
      saveSettings();
      break;
    case 16:
      blIntensity = lv_slider_get_value(btn);
      ledcWrite(0, blIntensity);
      saveSettings();
      break;
  }
  return LV_RES_OK;
}

void setup() {
  WiFiManager wifiManager;
  wifiManager.setDebugOutput(false);
  wifiManager.setClass("invert");
  wifiManager.setScanDispPerc(true);
  wifiManager.setCountry("CN"); // CN is 1-13, just like most of the world
  wifiManager.setHostname("espSwitcher");
  if (!wifiManager.autoConnect("espSwitcher")) { delay(1000); ESP.restart(); }
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  WiFi.setSleep(false);                         // MIGHT NOT DO ANYTHING FOR US, but it might give quicker response times
  server.on(F("/"), writePage);
  server.on(F("/sendSignal"), handleCommand);
  server.onNotFound(writePage);
  server.begin();

  lv_init(); // init lvgl

  tft.begin(); // init TFT_eSPI
  tft.setRotation(3);

  lv_disp_drv_t displayDriver; // init lvgl display driver
  lv_disp_drv_init(&displayDriver);
  displayDriver.disp_flush = flushDisplay;
  lv_disp_drv_register(&displayDriver);

  lv_indev_drv_t touchDriver; // init lvgl touchscreen driver
  lv_indev_drv_init(&touchDriver);
  touchDriver.read = readTouchscreen;
  touchDriver.type = LV_INDEV_TYPE_POINTER;
  lv_indev_drv_register(&touchDriver);

  tick.attach_ms(LVGL_TICK_PERIOD, lvglTimerTicker); // init lvgl graphics timer

  if (sntp_enabled()) { sntp_stop(); }
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, (char*)"nl.pool.ntp.org");
  sntp_init();
  setenv("TZ", "CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00", 1);
  tzset();
  while (!getTime()) {
    delay(500);
  };

  Preferences prefs;
  if (prefs.begin("alarmSettings", false)) {
    alarmAct = prefs.getBool("alarmActive", false);
    repeatAlarm = prefs.getBool("repeatAlarm", false);
    badkamerAlarm = prefs.getBool("badkamerAlarm", false);
    alarmHour = prefs.getUChar("alarmHour", 0);
    alarmMin = prefs.getUChar("alarmMin", 0);
    blIntensity = prefs.getUChar("backLight", blIntensity);
  }
  prefs.end();

  ledcSetup(0, 1000, 8);
  ledcAttachPin(backlightPin, 0);
  ledcWrite(0, blIntensity);

  calibrateDisplay(REPEAT_CAL); // calibrate tft

  createTabs();

  setAlarmTimeT();

  getWeather();
}

void loop() {
  server.handleClient();
  lv_task_handler();

  if (getTime() && secondOld != timeInfo.tm_sec){
    secondOld = timeInfo.tm_sec;

    lv_roller_set_selected(secondRoll, timeInfo.tm_sec, true);
    lv_roller_set_selected(minuteRoll, timeInfo.tm_min, true);
    lv_roller_set_selected(hourRoll, timeInfo.tm_hour, true);

    time_t now = unixTime();
    if (alarmAct && now >= alarmTimeT && now < (alarmTimeT + 60)) { //getTime could hog 5 sec (or fail) and proceed past alarmTimeT
      transmitSignal(3, true);
      transmitSignal(4, true);
      if (badkamerAlarm) { transmitSignal(2, true); }
      if (repeatAlarm) {
        setAlarmTimeT();
      } else {
        alarmAct = false;
        lv_cb_set_checked(alarmActive, false);
        saveSettings();
      }
    }

    if (blTimer == 10) {
      backlEnable = false;
      touchEnable = false;
      ledcWrite(0, 0);
    } else {
      blTimer++;
    }

    // WiFi disconnect fix?
    if (WiFi.status() != WL_CONNECTED) { //maybe move this outside the getTime loop
      wifiTimer++;
      if (wifiTimer > 20) {
        ESP.restart();
      } else if (wifiTimer > 5) {
        WiFi.disconnect();
        WiFi.enableSTA(false);
        delay(200);
        WiFi.enableSTA(true);
        delay(200);
        WiFi.setHostname("espSwitcher");
        WiFi.begin();
        WiFi.waitForConnectResult();
      }
    } else if (wifiTimer) { wifiTimer = 0; }

    weerTimer++;
    if (weerTimer >= 600) {
      getWeather();
      weerTimer = 0;
    }
  }
}
