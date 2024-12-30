#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "dog.h"  // 加入圖片標頭檔

TFT_eSPI tft = TFT_eSPI();

// WiFi 設定
const char* ssid = "hobart";
const char* password = "19880808";

// NTP 設定
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 28800;    // UTC+8
const int   daylightOffset_sec = 0;

// 畫面設定
enum DisplayMode {
    SPLASH_MODE,   // 圖片模式
    CLOCK_MODE,
    AQI_MODE
};
DisplayMode currentMode = SPLASH_MODE;  // 預設從圖片模式開始

// 時鐘設定
const int centerX = 120;
const int centerY = 67;
const int clockRadius = 60;
const int hourLength = 30;
const int minuteLength = 40;
const int secondLength = 50;

uint16_t hourColor;     
uint16_t minuteColor;   
uint16_t secondColor;   
// 按鈕設定
const int rightButtonPin = 32;  
const int leftButtonPin = 33;   

// 按鈕防彈跳設定
const unsigned long DEBOUNCE_DELAY = 100;     // 防抖動延遲時間
const unsigned long MODE_SWITCH_DELAY = 300;  // 模式切換間的延遲時間
unsigned long lastDebounceTimeRight = 0;
unsigned long lastDebounceTimeLeft = 0;
unsigned long lastModeSwitchTime = 0;        // 記錄最後一次模式切換的時間
bool lastSteadyStateRight = HIGH;    
bool lastSteadyStateLeft = HIGH;    
bool lastFlickerableStateRight = HIGH;    
bool lastFlickerableStateLeft = HIGH;    
bool buttonEnabled = true;                   // 用於控制按鈕是否可用
// AQI 設定
const char* aqiUrl = "https://api.waqi.info/feed/taiwan/situn/?token=0d427a0820b91723857cca871667890c56dea8ff";
struct AQIData {
    int aqi;
    String time;
    int pm25;
    bool hasData;  
} aqiData;

unsigned long lastAQIUpdate = 0;
unsigned long lastAQIDrawTime = 0;  
const unsigned long AQI_UPDATE_INTERVAL = 900000; // 15分鐘更新一次

// 修改後的顯示圖片函數，包含 RGB 到 BGR 的轉換
void showSplashScreen() {
    tft.fillScreen(TFT_BLACK);
    int x = (240 - 231) / 2;  // 置中
    int y = 0;
    
    // 建立暫時的緩衝區來轉換顏色格式
    uint16_t* converted_image = new uint16_t[DOG_WIDTH * DOG_HEIGHT];
    for(int i = 0; i < DOG_WIDTH * DOG_HEIGHT; i++) {
        // RGB565 轉 BGR565
        uint16_t pixel = pgm_read_word(&dog[i]);
        uint16_t r = (pixel >> 11) & 0x1F;
        uint16_t g = (pixel >> 5) & 0x3F;
        uint16_t b = pixel & 0x1F;
        converted_image[i] = (b << 11) | (g << 5) | r;
    }
    
    tft.pushImage(x, y, DOG_WIDTH, DOG_HEIGHT, converted_image);
    delete[] converted_image;  // 釋放記憶體
}

void drawAQIScreen() {
    unsigned long currentTime = millis();
    
    if (currentTime - lastAQIDrawTime < AQI_UPDATE_INTERVAL && lastAQIDrawTime != 0) {
        return;
    }
    
    if (!aqiData.hasData) {
        Serial.println("No AQI data available");
        return;
    }

    Serial.println("Drawing AQI screen...");
    
    tft.fillScreen(tft.color565(0, 0, 0));  // 黑色背景
    
    // 標題
    tft.setTextColor(tft.color565(255, 255, 255));  // 白色
    tft.setTextSize(2);
    tft.drawString("Taichung Xitun", 5, 5);
    
    // AQI 數值
    char aqiStr[20];
    sprintf(aqiStr, "AQI: %d", aqiData.aqi);
    tft.drawString(aqiStr, 5, 30);
    
    // 空氣品質狀態
    uint16_t bgColor;
    const char* qualityText;
    if (aqiData.aqi <= 50) {
        bgColor = tft.color565(0, 255, 0);  // 綠色
        qualityText = "GOOD";
    }
    else if (aqiData.aqi <= 100) {
        bgColor = tft.color565(0, 255, 255);  // 黃色
        qualityText = "NORMAL";
    }
    else {
        bgColor = tft.color565(0, 0, 255);  // 紅色
        qualityText = "BAD";
    }
    
    int textWidth = 100;  
    int textHeight = 50;  
    int textX = (tft.width() - textWidth) / 2;  
    int textY = 50;
    tft.fillRect(textX, textY, textWidth, textHeight, bgColor);
    
    tft.setTextColor(tft.color565(0, 0, 0));  // 黑色文字
    int16_t textCenterX = textX + (textWidth - strlen(qualityText) * 12) / 2;
    int16_t textCenterY = textY + (textHeight - 16) / 2;  
    tft.drawString(qualityText, textCenterX, textCenterY);
    
    // 更新時間戳記
    tft.setTextColor(tft.color565(255, 255, 255));  // 白色
    char timeStr[30];
    char hourStr[3], minStr[3], secStr[3];
    
    String timeString = aqiData.time;
    int spacePos = timeString.lastIndexOf(" ");
    String timeComponent = timeString.substring(spacePos + 1);  
    
    strncpy(hourStr, timeComponent.c_str(), 2);
    hourStr[2] = '\0';
    strncpy(minStr, timeComponent.c_str() + 3, 2);
    minStr[2] = '\0';
    strncpy(secStr, timeComponent.c_str() + 6, 2);
    secStr[2] = '\0';
    
    sprintf(timeStr, "Updated: %s:%s:%s", hourStr, minStr, secStr);
    int16_t timeX = (tft.width() - strlen(timeStr) * 6) / 2;  
    tft.drawString(timeStr, 5, 105, 1);
    
    lastAQIDrawTime = currentTime;
}

void updateAQI() {
    Serial.println("Updating AQI data...");
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(aqiUrl);
        
        int httpResponseCode = http.GET();
        Serial.printf("HTTP Response code: %d\n", httpResponseCode);
        
        if (httpResponseCode > 0) {
            String payload = http.getString();
            StaticJsonDocument<1024> doc;
            DeserializationError error = deserializeJson(doc, payload);
            
            if (!error) {
                aqiData.aqi = doc["data"]["aqi"].as<int>();
                aqiData.time = doc["data"]["time"]["s"].as<String>();
                aqiData.hasData = true;
                
                Serial.println("=== 空氣品質資料更新 ===");
                Serial.print("測站: 台中 西屯\n");
                Serial.print("AQI: "); Serial.println(aqiData.aqi);
                Serial.print("更新時間: "); Serial.println(aqiData.time);
                Serial.println("========================");
                
                if (currentMode == AQI_MODE) {
                    lastAQIDrawTime = 0;  
                    drawAQIScreen();
                }
            } else {
                Serial.println("JSON parsing failed");
            }
        } else {
            Serial.println("Failed to get AQI data");
        }
        http.end();
    } else {
        Serial.println("WiFi not connected");
    }
}

void drawAnalogClock(struct tm *timeinfo) {
    static float prevHourAngle = -1, prevMinuteAngle = -1, prevSecondAngle = -1;

    float hourAngle = (timeinfo->tm_hour % 12 + timeinfo->tm_min / 60.0) * 30 - 90;
    float minuteAngle = timeinfo->tm_min * 6 - 90;
    float secondAngle = timeinfo->tm_sec * 6 - 90;

    TFT_eSprite sprite = TFT_eSprite(&tft);
    sprite.createSprite(tft.width(), tft.height());
    sprite.fillSprite(TFT_BLACK);
    
    if (prevHourAngle >= 0) {
        int oldX = centerX + hourLength * cos(prevHourAngle * PI / 180);
        int oldY = centerY + hourLength * sin(prevHourAngle * PI / 180);
        sprite.drawLine(centerX, centerY, oldX, oldY, TFT_BLACK);
    }
    if (prevMinuteAngle >= 0) {
        int oldX = centerX + minuteLength * cos(prevMinuteAngle * PI / 180);
        int oldY = centerY + minuteLength * sin(prevMinuteAngle * PI / 180);
        sprite.drawLine(centerX, centerY, oldX, oldY, TFT_BLACK);
    }
    if (prevSecondAngle >= 0) {
        int oldX = centerX + secondLength * cos(prevSecondAngle * PI / 180);
        int oldY = centerY + secondLength * sin(prevSecondAngle * PI / 180);
        sprite.drawLine(centerX, centerY, oldX, oldY, TFT_BLACK);
    }

    sprite.drawWideLine(centerX, centerY, 
        centerX + hourLength * cos(hourAngle * PI / 180),
        centerY + hourLength * sin(hourAngle * PI / 180),
        3, hourColor);
        
    sprite.drawWideLine(centerX, centerY,
        centerX + minuteLength * cos(minuteAngle * PI / 180),
        centerY + minuteLength * sin(minuteAngle * PI / 180),
        2, minuteColor);
        
    sprite.drawLine(centerX, centerY,
        centerX + secondLength * cos(secondAngle * PI / 180),
        centerY + secondLength * sin(secondAngle * PI / 180),
        secondColor);

    sprite.drawCircle(centerX, centerY, clockRadius, TFT_WHITE);
    sprite.fillCircle(centerX, centerY, 3, TFT_WHITE);
    
    for (int i = 0; i < 12; i++) {
        float angle = i * 30 * PI / 180;
        sprite.drawLine(
            centerX + (clockRadius - 5) * cos(angle),
            centerY + (clockRadius - 5) * sin(angle),
            centerX + clockRadius * cos(angle),
            centerY + clockRadius * sin(angle),
            TFT_WHITE
        );
    }

    char dateStr[20];
    const char* weekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    sprintf(dateStr, "%s %02d/%02d", weekdays[timeinfo->tm_wday], timeinfo->tm_mon + 1, timeinfo->tm_mday);
    sprite.setTextColor(TFT_BLUE, TFT_BLACK);
    sprite.drawString(dateStr, centerX - 30, centerY - 45, 2);

    sprite.pushSprite(0, 0);
    sprite.deleteSprite();

    prevHourAngle = hourAngle;
    prevMinuteAngle = minuteAngle;
    prevSecondAngle = secondAngle;
}

void initDisplay() {
    pinMode(4, OUTPUT);    // RST
    pinMode(2, OUTPUT);    // DC
    pinMode(15, OUTPUT);   // CS
    digitalWrite(15, HIGH);

    digitalWrite(4, HIGH);
    delay(100);
    digitalWrite(4, LOW);
    delay(100);
    digitalWrite(4, HIGH);
    delay(200);

    SPI.begin(18, -1, 23, 15);
    tft.init();
    tft.setRotation(1);    // 設為橫向
    tft.fillScreen(TFT_BLACK);
}

void connectWiFi() {
    tft.setTextColor(tft.color565(255, 255, 255), tft.color565(0, 0, 0));
    tft.drawString("Connecting to WiFi", 10, 10, 2);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    tft.drawString("WiFi Connected", 10, 30, 2);
    delay(1000);
}

void setup() {
    Serial.begin(115200);
    Serial.println("Starting...");

    pinMode(rightButtonPin, INPUT_PULLUP);
    pinMode(leftButtonPin, INPUT_PULLUP);

    hourColor = tft.color565(0, 0, 255);     // 紅色
    minuteColor = tft.color565(0, 255, 0);   // 綠色
    secondColor = tft.color565(255, 0, 0);   // 藍色

    aqiData.hasData = false;  // 初始化 AQI 資料狀態

    initDisplay();
    connectWiFi();
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    updateAQI();
    
    // 初始顯示圖片
    showSplashScreen();
}

void loop() {
    // 處理右鍵
    bool currentStateRight = digitalRead(rightButtonPin);
    if (currentStateRight != lastFlickerableStateRight) {
        lastDebounceTimeRight = millis();
        lastFlickerableStateRight = currentStateRight;
    }

    // 處理左鍵
    bool currentStateLeft = digitalRead(leftButtonPin);
    if (currentStateLeft != lastFlickerableStateLeft) {
        lastDebounceTimeLeft = millis();
        lastFlickerableStateLeft = currentStateLeft;
    }

    // 按鍵觸發處理
    unsigned long currentTime = millis();
    
    // 檢查是否已經過了足夠的防抖動和模式切換延遲時間
    if (((currentTime - lastDebounceTimeRight) > DEBOUNCE_DELAY || 
         (currentTime - lastDebounceTimeLeft) > DEBOUNCE_DELAY) &&
        (currentTime - lastModeSwitchTime) > MODE_SWITCH_DELAY &&
        buttonEnabled) {
        
        // 右鍵處理 (順時針切換: 圖片 -> 時鐘 -> AQI)
        if (lastSteadyStateRight == HIGH && currentStateRight == LOW) {
            buttonEnabled = false;
            lastModeSwitchTime = currentTime;
            
            switch (currentMode) {
                case SPLASH_MODE:
                    currentMode = CLOCK_MODE;
                    tft.fillScreen(TFT_BLACK);
                    break;
                case CLOCK_MODE:
                    currentMode = AQI_MODE;
                    tft.fillScreen(TFT_BLACK);
                    lastAQIDrawTime = 0;
                    drawAQIScreen();
                    break;
                case AQI_MODE:
                    currentMode = SPLASH_MODE;
                    showSplashScreen();
                    break;
            }
        }
        
        // 左鍵處理 (逆時針切換: 圖片 -> AQI -> 時鐘)
        if (lastSteadyStateLeft == HIGH && currentStateLeft == LOW) {
            buttonEnabled = false;
            lastModeSwitchTime = currentTime;
            
            switch (currentMode) {
                case SPLASH_MODE:
                    currentMode = AQI_MODE;
                    tft.fillScreen(TFT_BLACK);
                    lastAQIDrawTime = 0;
                    drawAQIScreen();
                    break;
                case AQI_MODE:
                    currentMode = CLOCK_MODE;
                    tft.fillScreen(TFT_BLACK);
                    break;
                case CLOCK_MODE:
                    currentMode = SPLASH_MODE;
                    showSplashScreen();
                    break;
            }
        }
        
        lastSteadyStateRight = currentStateRight;
        lastSteadyStateLeft = currentStateLeft;
        
        // 重新啟用按鈕
        buttonEnabled = true;
    }

    // 根據當前模式顯示畫面
    switch (currentMode) {
        case SPLASH_MODE:
            // 在圖片模式下不需要更新畫面
            break;
            
        case CLOCK_MODE:
            {
                struct tm timeinfo;
                if (!getLocalTime(&timeinfo)) {
                    tft.drawString("Time Error", 10, 10, 2);
                    return;
                }
                drawAnalogClock(&timeinfo);
            }
            break;
            
        case AQI_MODE:
            if (millis() - lastAQIUpdate >= AQI_UPDATE_INTERVAL) {
                updateAQI();
                lastAQIUpdate = millis();
            }
            drawAQIScreen();
            break;
    }
}