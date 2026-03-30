#include <Arduino.h>
#include <sys/time.h>
#include "main.h"
#include <LittleFS.h>
#include <math.h>
#include <WiFiClientSecure.h>
#include "Robot60.h"
#include "Robot20.h"
#include "Robot35.h"

// WiFi настройки (можно изменить через веб-интерфейс)
String wifiSSID = WIFI_SSID;
String wifiPassword = WIFI_PASSWORD;

// NTP настройки (можно изменить через веб-интерфейс)
String ntpServer = NTP_SERVER;
long gmtOffset_sec = 7200;       // GMT+2 (Украина, зима), если dstAuto == false
int daylightOffset_sec = 0;
bool dstAuto = true;             // автоматический переход летнее/зимнее (EET/EEST, правила ЕС)

// Погода настройки (можно изменить через веб-интерфейс)
String weatherAPIKey = WEATHER_API_KEY;  // Получите на openweathermap.org
String weatherCity = WEATHER_CITY;
bool weatherEnabled = true;

// Глобальные объекты
TFT_eSPI tft = TFT_eSPI();
ESP8266WebServer server(80);

// Переменные состояния
bool wifiConnected = false;
bool timeSynced = false;
unsigned long lastUpdateTime = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long lastAlertUpdate = 0;
const unsigned long updateInterval = 500; // Дисплей и двоеточие: 2 раза/с (полсекунды вкл/выкл)
const unsigned long weatherUpdateInterval = 600000; // Обновление погоды каждые 10 минут
const unsigned long alertUpdateInterval = 30000; // Обновление статуса тревоги каждые 30 секунд
const unsigned long wifiReconnectInterval = 30000; // Пауза между попытками переподключения к WiFi (мс)
const unsigned long otaCheckInterval = 3600000; // Проверка OTA-обновления раз в час

unsigned long lastWifiReconnectAttempt = 0;
unsigned long lastOtaCheck = 0;

bool ntpInitialized = false;

// Данные погоды
WeatherData weather = {0, 0, "", "", false};
ForecastDay forecast[2] = {{0, 0, -1, "", false}, {0, 0, -1, "", false}}; // Прогноз на 2 дня (humidity = -1 означает "не найдено")

// Данные о воздушной тревоге
AlertData alert = {false, "", 0, false};

// Переменные для оптимизации отображения
int lastHour = -1;
int lastMinute = -1;
int lastSecond = -1;
int lastDay = -1;
int lastMonth = -1;
int lastYear = -1;
int lastWeekday = -1;
String lastDateStr = "";
String lastDayStr = "";
String lastTempStr = "";
String lastHumStr = "";
String lastWeatherIcon = "";
String lastWeatherDesc = "";
String lastForecastStr = "";
String lastHumNumStr = "";
bool lastWifiState = false;

bool otaInProgress = false;
int otaProgressPercent = 0;

// Флаг первого запуска
#define RTC_FLAG_ADDR 64

void setup() {
  Serial.begin(115200);
  delay(500);
  
  // Автоматический перезапуск после прошивки
  uint32_t restartFlag = 0;
  ESP.rtcUserMemoryRead(RTC_FLAG_ADDR, &restartFlag, sizeof(restartFlag));
  
  if (restartFlag != 0x12345678) {
    restartFlag = 0x12345678;
    ESP.rtcUserMemoryWrite(RTC_FLAG_ADDR, &restartFlag, sizeof(restartFlag));
    Serial.println("[BOOT] Первая загрузка после прошивки, перезапуск...");
    Serial.flush();
    delay(2000);
    ESP.restart();
    return;
  }
  
  Serial.println("\n========================================");
  Serial.println("WiFi Clock - Инициализация");
  Serial.println("========================================");
  Serial.print("Версия: ");
  Serial.println(FIRMWARE_VERSION);
  Serial.print("CPU: ");
  Serial.print(ESP.getCpuFreqMHz());
  Serial.println(" MHz");
  Serial.print("Память: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" байт\n");
  
  // Инициализация дисплея
  displayInit();
  
  // Отображение версии
  displayVersion();
  delay(1000);
  
  // Простое подключение к WiFi
  Serial.println("\n--- Подключение к WiFi ---");
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  
  Serial.print("[WiFi] Подключение к ");
  Serial.print(wifiSSID);
  Serial.print("...");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println(" OK");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" FAILED");
    wifiConnected = false;
  }
  
  // Инициализация NTP если WiFi подключен
  if (wifiConnected) {
    setupNTP();
    ntpInitialized = true;
    
    // Первый запрос погоды сразу после подключения
    if (weatherEnabled) {
      updateWeather();
      lastWeatherUpdate = millis();
    }

    // Проверка OTA при старте (сразу после подключения к WiFi)
    checkForOtaUpdate();
    lastOtaCheck = millis();
  }
  
  // Запуск веб-сервера
  setupWebServer();
  
  // Не затираем экран: иначе после displayVersion() остаётся чёрный экран до тех пор,
  // пока NTP не даст валидное время (displayTime() тогда сразу return).
  if (time(nullptr) > 1000000000) {
    displayTime();
  } else {
    displayVersion();
  }
  
  Serial.println("\n========================================");
  Serial.println("Инициализация завершена");
  Serial.println("========================================\n");
}

void loop() {
  uint32_t now = millis();
  
  // Автоматическое переподключение к WiFi, если сеть появилась позже
  ensureWiFiConnected();
  
  // Периодическая проверка OTA-обновления прошивки
  if (wifiConnected && (now - lastOtaCheck >= otaCheckInterval)) {
    checkForOtaUpdate();
    lastOtaCheck = now;
  }
  
  // Обработка веб-сервера
  server.handleClient();
  
  // Обновление времени на дисплее (500 мс — для мигания двоеточия)
  if (now - lastUpdateTime >= updateInterval) {
    // Проверяем синхронизацию времени, если WiFi подключен и NTP инициализирован
    if (wifiConnected && ntpInitialized) {
      updateTime(); // Эта функция проверяет и обновляет timeSynced
    }
    
    // Отображаем время (работает даже без синхронизации)
    displayTime();
    lastUpdateTime = now;
  }
  
  // Обновление погоды каждые 10 минут
  if (weatherEnabled && wifiConnected && 
      (now - lastWeatherUpdate >= weatherUpdateInterval)) {
    updateWeather();
    lastWeatherUpdate = now;
  }
  
  // Обновление статуса воздушной тревоги каждые 30 секунд
  if (wifiConnected && (now - lastAlertUpdate >= alertUpdateInterval)) {
    updateAlert();
    lastAlertUpdate = now;
  }
  
  // Предупреждение о тревоге теперь отображается как иконка вместо погоды (в displayTime())
  
  delay(10);
}

void resetBuiltinTextBeforeSmoothFont() {
  tft.setTextFont(1);
  tft.setTextSize(1);
}

void clearScreenAfterSplashForClockUi() {
  tft.unloadFont();
  resetBuiltinTextBeforeSmoothFont();
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
}

// Функции для установки шрифтов (легко заменить на сглаженные шрифты в будущем)
void setTimeFont() {
  tft.unloadFont();
  resetBuiltinTextBeforeSmoothFont();
  tft.loadFont(Robot60);
}

void setDateFont() {
  tft.unloadFont();
  resetBuiltinTextBeforeSmoothFont();
  tft.loadFont(Robot20);
}

void setTempFont() {
  tft.unloadFont();
  resetBuiltinTextBeforeSmoothFont();
  tft.loadFont(Robot35);
}

void setHumidityFont() {
  tft.unloadFont();
  tft.setTextFont(1);
  tft.setTextSize(2);
}

void showOtaScreen(const String& title, const String& line2) {
  // Полноэкранная индикация OTA. Используем стандартный шрифт, чтобы гарантировать наличие символов.
  tft.unloadFont();
  tft.setTextFont(1);
  tft.setTextSize(2);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(title, 120, 20);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(line2, 120, 55);
  
  // Рамка прогресс-бара
  tft.drawRect(20, 110, 200, 20, TFT_DARKGREY);
}

void drawOtaProgress(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  
  // Заполнение прогресс-бара
  int fillW = (200 - 2) * percent / 100;
  tft.fillRect(21, 111, 198, 18, TFT_BLACK);
  if (fillW > 0) {
    tft.fillRect(21, 111, fillW, 18, TFT_GREEN);
  }
  
  // Проценты
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.fillRect(70, 140, 100, 24, TFT_BLACK);
  tft.drawString(String(percent) + "%", 120, 140);
}

void hideOtaScreen() {
  // После OTA нельзя только заливать чёрным: при несинхронизированном времени displayTime()
  // сразу return — экран остаётся пустым. Показываем заставку; при валидном времени
  // следующий displayTime() перерисует часы (lastHour = -1).
  displayVersion();
  lastHour = -1;
  lastMinute = -1;
  lastSecond = -1;
  lastDay = -1;
  lastWeekday = -1;
  lastDateStr = "";
  lastTempStr = "";
  lastHumStr = "";
  lastHumNumStr = "";
  lastWeatherIcon = "";
  lastWeatherDesc = "";
  lastForecastStr = "";
  lastHumNumStr = "";
  lastWifiState = false;
}

void eraseDiffSmoothGlyphs(const String& oldStr, const String& newStr, int16_t x, int16_t y,
  int8_t padXLeft, int8_t padXRight, int8_t padYTop, int8_t padYBottom) {
  tft.setTextDatum(ML_DATUM);
  int16_t oh = tft.fontHeight();
  if (oh < 8) oh = 24;
  int olen = oldStr.length();
  int nlen = newStr.length();
  int n = (olen > nlen) ? olen : nlen;
  for (int i = 0; i < n; i++) {
    uint8_t oc = (i < olen) ? (uint8_t)oldStr[i] : 0;
    uint8_t nc = (i < nlen) ? (uint8_t)newStr[i] : 0;
    if (oc != nc && oc) {
      int16_t x0 = x + tft.textWidth(oldStr.substring(0, i));
      int16_t w = tft.textWidth(String((char)oc));
      if (w < 1) w = 8;
      tft.fillRect(x0 - padXLeft, y - oh / 2 - padYTop, w + padXLeft + padXRight, oh + padYTop + padYBottom, TFT_BLACK);
    }
  }
}

void displayInit() {
  Serial.println("[DISPLAY] Инициализация дисплея...");
  
  // Инициализация файловой системы для сглаженных шрифтов
  if (LittleFS.begin()) {
    Serial.println("[DISPLAY] LittleFS инициализирована");
  } else {
    Serial.println("[DISPLAY] Ошибка инициализации LittleFS (сглаженные шрифты недоступны)");
  }
  
  // Подсветка: tft.init() выставит TFT_BL в TFT_BACKLIGHT_ON из User_Setup.h (не дублируем уровень здесь).
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  
  // Настройка текста
  tft.setTextDatum(MC_DATUM);
  
  Serial.println("[DISPLAY] Дисплей инициализирован");
}

void displayVersion() {
  tft.unloadFont();
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(1);
  
  // Название (крупнее, чтобы заставка читалась с расстояния)
  tft.setTextSize(4);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("WiFi Clock", 120, 58);
  
  // Версия
  tft.setTextSize(3);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  String versionStr = "v" + String(FIRMWARE_VERSION);
  tft.drawString(versionStr, 120, 102);
  
  // Информация о системе
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String cpuStr = String(ESP.getCpuFreqMHz()) + " MHz";
  tft.drawString(cpuStr, 120, 138);
  
  String memStr = String(ESP.getFreeHeap() / 1024) + " KB RAM";
  tft.drawString(memStr, 120, 162);
}

// Функция displayBootBanner удалена

// Инициализация WiFi (полностью асинхронная, без блокировок)
// Функции initWiFi, doReconnect, setupWiFi, checkInternet удалены

void setupNTP() {
  if (!wifiConnected) {
    Serial.println("[NTP] WiFi не подключен, пропускаю инициализацию");
    return;
  }
  
  Serial.println("\n--- Настройка NTP ---");
  Serial.print("[NTP] Сервер: ");
  Serial.println(ntpServer);
  if (dstAuto) {
    Serial.println("[NTP] Режим: автоматическое летнее/зимнее время (EET/EEST, POSIX TZ)");
  } else {
    Serial.print("[NTP] Часовой пояс: GMT");
    Serial.print((gmtOffset_sec >= 0 ? "+" : ""));
    Serial.print(gmtOffset_sec / 3600);
    if (daylightOffset_sec != 0) {
      Serial.print(", летнее смещение +");
      Serial.print(daylightOffset_sec / 3600);
      Serial.println(" ч");
    } else {
      Serial.println();
    }
  }

  if (dstAuto) {
    // Последнее воскресенье марта 03:00 → летнее (EEST), последнее воскресенье октября 04:00 → зима (EET)
    configTime("EET-2EEST,M3.5.0/3,M10.5.0/4", ntpServer.c_str());
  } else {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer.c_str());
  }
  
  // Неблокирующая проверка синхронизации (проверяем сразу, без долгих задержек)
  time_t now = time(nullptr);
  
  // Даем небольшую задержку для начала синхронизации
  delay(100);
  
  // Проверяем несколько раз с короткими задержками
  int attempts = 0;
  while (now < 1000000000 && attempts < 5) {
    delay(200); // Короткие задержки вместо 1 секунды
    now = time(nullptr);
    attempts++;
    if (now > 1000000000) break;
  }
  
  if (now > 1000000000) {
    timeSynced = true;
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      Serial.print("[NTP] ✓ Время синхронизировано: ");
      char timeStr[20];
      strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
      Serial.println(timeStr);
    } else {
      Serial.println("[NTP] ✓ Время синхронизировано");
    }
  } else {
    timeSynced = false;
    Serial.println("[NTP] ⚠ Синхронизация в процессе... (проверится в следующем цикле)");
    // Не устанавливаем timeSynced = false окончательно, т.к. синхронизация может занять время
  }
}

void updateTime() {
  time_t now = time(nullptr);
  if (now < 1000000000) {
    timeSynced = false;
    return;
  }
  timeSynced = true;
}

void displayTime() {
  struct tm timeinfo;
  time_t now = time(nullptr);
  
  // Отладочная информация (только при первой загрузке или проблемах)
  static bool debugPrinted = false;
  if (!debugPrinted) {
    Serial.print("[TIME] time(nullptr) = ");
    Serial.println(now);
    Serial.print("[TIME] timeSynced = ");
    Serial.println(timeSynced ? "true" : "false");
  }
  
  if (!getLocalTime(&timeinfo)) {
    // Если getLocalTime не работает, пробуем через localtime_r
    if (now > 0 && now > 1000000000) {
      localtime_r(&now, &timeinfo);
      if (!debugPrinted) {
        Serial.println("[TIME] Используем localtime_r");
      }
    } else {
      // Время ещё не синхронизировано — иначе после hideOtaScreen() или заливки чёрным
      // экран остаётся пустым до появления NTP.
      if (!debugPrinted) {
        Serial.println("[TIME] Время ещё не синхронизировано — заставка");
      }
      debugPrinted = true;
      static unsigned long lastNoTimeSplash = 0;
      unsigned long ms = millis();
      if (lastNoTimeSplash == 0 || ms - lastNoTimeSplash >= 1500) {
        displayVersion();
        lastNoTimeSplash = ms;
      }
      return;
    }
  }
  
  if (!debugPrinted) {
    Serial.print("[TIME] Время получено: ");
    Serial.print(timeinfo.tm_hour);
    Serial.print(":");
    Serial.println(timeinfo.tm_min);
    debugPrinted = true;
  }
  
  // Погода была на экране, а теперь выключена или невалидна — иначе остаются «хвосты» от текста/иконок
  static bool lastWeatherUiShown = false;
  bool showWeatherUi = weatherEnabled && weather.valid;
  if (lastWeatherUiShown && !showWeatherUi) {
    tft.fillRect(125, 68, 115, 105, TFT_BLACK);
    tft.fillRect(8, 84, 150, 44, TFT_BLACK);
    tft.fillRect(26, 124, 120, 32, TFT_BLACK);
    tft.fillRect(0, 144, 240, 96, TFT_BLACK);
    lastWeatherIcon = "";
    lastWeatherDesc = "";
    lastTempStr = "";
    lastHumStr = "";
    lastForecastStr = "";
    lastHumNumStr = "";
    lastHumStr = "";
  }
  lastWeatherUiShown = showWeatherUi;
  
  static char lastHourDisplayed[4] = "";
  static char lastMinDisplayed[4] = "";
  static char lastSecDisplayed[4] = "";
  static int16_t lastSecXPos = -1;
  if (lastHour < 0) lastHourDisplayed[0] = '\0';
  if (lastMinute < 0) lastMinDisplayed[0] = '\0';
  if (lastSecond < 0) {
    lastSecDisplayed[0] = '\0';
    lastSecXPos = -1;
  }
  
  // Первый кадр часов после заставки/OTA: полная очистка + сброс текста; шрифты Robot ниже через setTimeFont/…
  if (lastHour < 0) {
    clearScreenAfterSplashForClockUi();
  }
  
  // Время всегда на одной позиции (иконка тревоги заменяет иконку погоды, не перекрывает время)
  int timeY = 30;
  
  // Верхняя строка: Время слева (72 pt) + Иконка Wi-Fi справа
  setTimeFont(); // Установка шрифта для времени (легко заменить на сглаженный)
  tft.setTextDatum(ML_DATUM); // Выравнивание слева
  
  // Часы: затирание только изменившихся цифр (сглаженный шрифт)
  {
    char hourStr[4];
    sprintf(hourStr, "%02d", timeinfo.tm_hour);
    if (strcmp(hourStr, lastHourDisplayed) != 0) {
      if (lastHourDisplayed[0] != '\0') {
        setTimeFont();
        tft.setTextDatum(ML_DATUM);
        eraseDiffSmoothGlyphs(String(lastHourDisplayed), String(hourStr), 10, timeY, 2, 2, 3, 2);
      }
      setTimeFont();
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.drawString(hourStr, 10, timeY);
      strcpy(lastHourDisplayed, hourStr);
      lastHour = timeinfo.tm_hour;
    }
  }
  
  // Двоеточие: первая половина секунды видно, вторая скрыто (как у обычных часов).
  // Раньше Y=27 при timeY=30 и маленький fillRect — затиралась только нижняя точка.
  {
    char hourStrForColon[4];
    sprintf(hourStrForColon, "%02d", timeinfo.tm_hour);
    setTimeFont();
    tft.setTextDatum(ML_DATUM);
    int colonX = 10 + tft.textWidth(hourStrForColon) + 4;
    int16_t fh = tft.fontHeight();
    if (fh < 8) fh = 48;
    int cw = tft.textWidth(":");
    if (cw < 4) cw = 12;
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    bool colonVisible = (tv.tv_usec < 500000);
    if (colonVisible) {
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.drawString(":", colonX, timeY);
    } else {
      tft.fillRect(colonX - 2, timeY - fh / 2 - 3, cw + 6, fh + 6, TFT_BLACK);
    }
  }
  
  // Минуты: затирание только изменившихся цифр
  {
    char minStr[4];
    sprintf(minStr, "%02d", timeinfo.tm_min);
    if (strcmp(minStr, lastMinDisplayed) != 0) {
      if (lastMinDisplayed[0] != '\0') {
        setTimeFont();
        tft.setTextDatum(ML_DATUM);
        eraseDiffSmoothGlyphs(String(lastMinDisplayed), String(minStr), 93, timeY, 2, 2, 2, 0);
      }
      setTimeFont();
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.drawString(minStr, 93, timeY);
      strcpy(lastMinDisplayed, minStr);
      lastMinute = timeinfo.tm_min;
    }
  }
  
  // Секунды (Robot20): затирание только изменившихся цифр
  {
    setTimeFont();
    tft.setTextDatum(ML_DATUM);
    char minStrForW[4];
    sprintf(minStrForW, "%02d", timeinfo.tm_min);
    int minWidth = tft.textWidth(minStrForW);
    int secX = 93 + minWidth + 5;
    int secY = timeY - 15;
    char secStr[4];
    sprintf(secStr, "%02d", timeinfo.tm_sec);
    if (strcmp(secStr, lastSecDisplayed) != 0) {
      if (lastSecDisplayed[0] != '\0') {
        setDateFont();
        tft.setTextDatum(ML_DATUM);
        int16_t oh = tft.fontHeight();
        if (oh < 8) oh = 22;
        if (lastSecXPos >= 0 && secX != lastSecXPos) {
          int sw = tft.textWidth(String(lastSecDisplayed));
          tft.fillRect(lastSecXPos, secY - oh / 2 - 3, sw + 4, oh + 6, TFT_BLACK);
        } else {
          // padXLeft=0: не заходить влево на цифры минут (Robot60), padXRight — сглаживание справа
          eraseDiffSmoothGlyphs(String(lastSecDisplayed), String(secStr), secX, secY, 0, 2, 3, 2);
        }
      }
      setDateFont();
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.drawString(secStr, secX, secY);
      strcpy(lastSecDisplayed, secStr);
      lastSecXPos = secX;
      lastSecond = timeinfo.tm_sec;
      setTimeFont();
    }
  }
  
  // Иконка Wi-Fi справа вверху (горизонтальные полоски, крупнее, от края к центру)
  static unsigned long lastWifiIconUpdate = 0;
  static bool wifiConnectingBlink = false;
  
  // Обновляем иконку каждые 500мс (для мигания при подключении) или при изменении состояния
  bool needUpdate = (wifiConnected != lastWifiState) || (millis() - lastWifiIconUpdate >= 500);
  
  if (needUpdate) {
    lastWifiIconUpdate = millis();
    
    // Стираем старую иконку (прямоугольник 30x20 пикселей - увеличен размер)
    tft.fillRect(210, 5, 30, 20, TFT_BLACK); // Поднято до верха (Y=5 вместо 15)
    
    if (wifiConnected) {
      // WiFi подключен - показываем горизонтальные полоски зелёным цветом в зависимости от силы сигнала
      int rssi = WiFi.RSSI();
      uint16_t color = TFT_GREEN;
      
      // Определяем количество полосок по силе сигнала
      int bars = 0;
      if (rssi > -50) bars = 4;      // Отличный сигнал - 4 полоски
      else if (rssi > -60) bars = 3; // Хороший сигнал - 3 полоски
      else if (rssi > -70) bars = 2; // Средний сигнал - 2 полоски
      else if (rssi > -80) bars = 1; // Слабый сигнал - 1 полоска
      else bars = 1;                 // Очень слабый - всё равно 1 полоска
      
      // Рисуем горизонтальные полоски от правого края к центру
      // Полоски расположены одна над другой, ширина увеличивается снизу вверх
      int rightEdge = 240; // Правый край экрана
      int startY = 20; // Нижняя позиция (поднято вверх)
      int barHeight = 3; // Высота каждой полоски (увеличена)
      int barSpacing = 2; // Расстояние между полосками
      
      for (int i = 0; i < bars; i++) {
        int barWidth = 6 + i * 4; // Ширина увеличивается снизу вверх (6, 10, 14, 18)
        int barX = rightEdge - barWidth; // Полоски начинаются от правого края
        int barY = startY - (i * (barHeight + barSpacing)); // Позиция Y снизу вверх
        
        tft.fillRect(barX, barY, barWidth, barHeight, color);
      }
      
      lastWifiState = wifiConnected;
    } else {
      // WiFi не подключен - мигаем жёлтым при подключении
      wifiConnectingBlink = !wifiConnectingBlink;
      uint16_t color = wifiConnectingBlink ? TFT_YELLOW : TFT_BLACK;
      
      // Показываем 2 горизонтальные полоски при подключении (мигают, от края к центру)
      if (wifiConnectingBlink) {
        int rightEdge = 240;
        int startY = 20; // Поднято вверх
        int barHeight = 3;
        int barSpacing = 2;
        
        // Две горизонтальные полоски от правого края
        tft.fillRect(rightEdge - 6, startY, 6, barHeight, color); // Нижняя (короткая)
        tft.fillRect(rightEdge - 10, startY - (barHeight + barSpacing), 10, barHeight, color); // Верхняя (длиннее)
      }
      
      lastWifiState = wifiConnected;
    }
  }
  
  // Иконка тревоги под WiFi иконкой (мигает красным если тревога, зелёным постоянно если нет)
  static unsigned long lastAlertIconUpdate = 0;
  static bool alertIconVisible = true;
  
  if (alert.valid) {
    bool needAlertUpdate = (millis() - lastAlertIconUpdate >= 500); // Мигание каждые 500мс
    
    if (needAlertUpdate) {
      lastAlertIconUpdate = millis();
      
      if (alert.active) {
        // Тревога активна - мигаем красным
        alertIconVisible = !alertIconVisible;
        if (alertIconVisible) {
          // Рисуем красную иконку тревоги
          drawAlertIconSmall(210, 28, 20); // X=210 (под WiFi), Y=28 (под WiFi иконкой), размер 20x20
        } else {
          // Стираем иконку
          tft.fillRect(210, 28, 20, 20, TFT_BLACK);
        }
      } else {
        // Тревоги нет - постоянно горит зелёным
        alertIconVisible = true;
        drawAlertIconSmall(210, 28, 20); // Зелёная иконка
      }
    } else if (!alert.active && alertIconVisible) {
      // При первой проверке или изменении состояния рисуем зелёную иконку
      drawAlertIconSmall(210, 28, 20);
    }
  } else {
    // Если данные о тревоге не получены, стираем область
    tft.fillRect(210, 28, 20, 20, TFT_BLACK);
  }
  
  // Вторая строка: Дата в формате "Пн Вт Ср 27 Пт Сб Вс" (Robot20), влево напротив времени
  const int dateRowY = 67;
  setDateFont(); // Установка шрифта Robot20 для даты
  tft.setTextDatum(ML_DATUM); // Выравнивание слева
  
  // Формируем строку даты: все дни недели, число месяца поверх текущего дня
  if (timeinfo.tm_mday != lastDay || timeinfo.tm_wday != lastWeekday) {
    // Сокращённые названия дней недели (tm_wday: 0=Вс, 1=Пн, 2=Вт, 3=Ср, 4=Чт, 5=Пт, 6=Сб)
    const char* weekdays[] = {"Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};
    
    // Формируем строку со всеми днями недели
    String dateStr = "";
    for (int i = 1; i <= 7; i++) { // Начинаем с понедельника (1)
      int dayIndex = i % 7; // Преобразуем: 1->1(Пн), 2->2(Вт), ..., 6->6(Сб), 7->0(Вс)
      
      if (dayIndex == timeinfo.tm_wday) {
        // Текущий день - выводим число месяца (с ведущим нулём если однозначное)
        char dayNum[4];
        sprintf(dayNum, "%02d", timeinfo.tm_mday);
        dateStr += String(dayNum);
      } else {
        // Обычный день - выводим название
        dateStr += String(weekdays[dayIndex]);
      }
      
      // Добавляем пробел между днями (кроме последнего)
      if (i < 7) {
        dateStr += " ";
      }
    }
    
    if (lastDateStr.length() > 0) {
      setDateFont();
      tft.setTextDatum(ML_DATUM);
      int dh = tft.fontHeight();
      if (dh < 8) dh = 22;
      tft.fillRect(6, dateRowY - dh / 2 - 3, 228, dh + 8, TFT_BLACK);
    }
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(dateStr, 10, dateRowY);
    lastDateStr = dateStr;
    lastDay = timeinfo.tm_mday;
    lastWeekday = timeinfo.tm_wday;
  }
  
  // Иконка погоды справа с подписью, по центру между датой и температурой (крупнее, отступ от края)
  if (weatherEnabled && weather.valid) {
    // Дата на Y=67, температура на Y=100, центр между ними: Y≈85
    // Иконка 56x56 + подпись ~18px + отступ 4px = ~78px общая высота
    // Центр блока на Y=85, значит верх иконки на Y=85-39≈46, опущено на 7px = 53
    int iconSize = 56; // Увеличена с 48 до 56
    int iconY = 75; // Опущено на 7 пикселей (было 46)
    
    // Robot20 (как у даты): встроенный шрифт setTextSize() плохо декодирует UTF-8 кириллицу (вместо «П» и т.п.)
    setDateFont();
    tft.setTextDatum(ML_DATUM);
    String longestDesc = "Перем обл";
    int maxTextWidth = tft.textWidth(longestDesc);
    
    // Получаем описание погоды на русском и сокращаем его для компактного отображения
    String weatherDesc = getShortWeatherDescription(getWeatherDescription(weather.icon));
    
    // Вычисляем позицию: иконка центрируется относительно самого длинного текста
    // Правый край экрана с отступом 10px
    int rightEdge = 240 - 10; // 230px
    // Центр самого длинного текста
    int textCenterX = rightEdge - maxTextWidth / 2;
    // Центр иконки совпадает с центром текста
    int iconX = textCenterX - iconSize / 2;
    
    // Обновляем иконку и подпись погоды только если они изменились
    if (weather.icon != lastWeatherIcon || weatherDesc != lastWeatherDesc) {
      // Стираем старую иконку (только область иконки, чтобы не задевать строку даты)
      tft.fillRect(iconX, iconY, iconSize, iconSize, TFT_BLACK);
      
      // Стираем старую подпись (только область подписи)
      int clearTextWidth = maxTextWidth + 14;
      int clearTextX = textCenterX - (maxTextWidth / 2) - 7;
      int textY = iconY + iconSize - 3;
      int dhClear = tft.fontHeight();
      if (dhClear < 8) dhClear = 22;
      tft.fillRect(clearTextX, textY, clearTextWidth, dhClear + 6, TFT_BLACK);
      
      // Рисуем иконку (увеличенную)
      drawWeatherIcon(weather.icon, iconX, iconY, iconSize);
      
      tft.setTextDatum(TC_DATUM);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(weatherDesc, textCenterX, textY);
      
      lastWeatherIcon = weather.icon; // Обновляем только если показываем погоду
      lastWeatherDesc = weatherDesc;
    }
  }
  
  // Третья строка: Температура влево (44 pt) с окружностью и буквой C, под датой
  if (weatherEnabled && weather.valid) {
    String tempStr = (weather.temperature >= 0 ? "+" : "") + String(weather.temperature, 0);
    if (tempStr != lastTempStr) {
      int16_t textX = 10;
      int16_t textY = 100;
      setTempFont();
      tft.setTextDatum(ML_DATUM);
      if (lastTempStr.length() > 0) {
        eraseDiffSmoothGlyphs(lastTempStr, tempStr, textX, textY);
        int16_t oh = tft.fontHeight();
        if (oh < 8) oh = 28;
        int16_t oldDegX = textX + tft.textWidth(lastTempStr) + 3;
        tft.fillRect(oldDegX - 2, textY - oh / 2 - 4, 48, oh + 10, TFT_BLACK);
      }
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(tempStr, textX, textY);
      int16_t textWidth = tft.textWidth(tempStr);
      int16_t degreeX = textX + textWidth + 3;
      tft.drawString("°", degreeX, textY);
      int16_t degreeWidth = tft.textWidth("°");
      int16_t cX = degreeX + degreeWidth + 2;
      tft.drawString("C", cX, textY);
      lastTempStr = tempStr;
    }
  }
  
  // Четвертая строка: Влажность влево (44 pt, тем же шрифтом что и температура), поднята
  // РАСЧЕТ СВОБОДНОГО ПРОСТРАНСТВА:
  // - Размер экрана: 240x240 пикселей
  // - Влажность: Y=132, шрифт size=4 (44 pt) (опущена на 2px: было 130)
  // - Высота текста size=4: ~42 пикселя (8*4 + межстрочный интервал)
  // - Нижний край влажности: Y=132+42=174
  // - Свободное пространство ниже влажности (без учета прогноза):
  //   Высота: 240-174 = 66 пикселей (было 68)
  //   Ширина: 240 пикселей
  //   Площадь: 240 * 66 = 15840 пикселей² (было 16320)
  if (weatherEnabled && weather.valid) {
    // Рисуем число и % отдельно, чтобы % был ближе к цифре
    String humNumStr = String(weather.humidity);
    String humFullStr = humNumStr + " %";
    if (humFullStr != lastHumStr) {
      setTempFont();
      tft.setTextDatum(ML_DATUM);
      if (lastHumNumStr.length() > 0) {
        eraseDiffSmoothGlyphs(lastHumNumStr, humNumStr, 30, 132);
        int16_t oh = tft.fontHeight();
        if (oh < 8) oh = 28;
        int16_t oldPctX = 30 + tft.textWidth(lastHumNumStr) + 7;
        tft.fillRect(oldPctX - 2, 132 - oh / 2 - 4, 22, oh + 10, TFT_BLACK);
      }
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(humNumStr, 30, 132);
      int16_t numWidth = tft.textWidth(humNumStr);
      tft.drawString("%", 30 + numWidth + 7, 132);
      lastHumNumStr = humNumStr;
      lastHumStr = humFullStr;
    }
  }
  
  // Прогноз на 2 дня под влажностью (разделено пополам по горизонтали)
  // Свободное пространство: Y от 172 до 240 (68 пикселей), X от 0 до 240
  // Завтра: левая половина (X от 0 до 120)
  // Послезавтра: правая половина (X от 120 до 240)
  if (weatherEnabled) {
    // Оба дня прогноза пропали — затереть нижнюю зону (раньше блок не вызывался и оставались старые цифры)
    if (!(forecast[0].valid || forecast[1].valid) && lastForecastStr.length() > 0) {
      tft.fillRect(0, 144, 240, 96, TFT_BLACK);
      lastForecastStr = "";
    }
    // Отображаем прогноз, если хотя бы один день валиден
    if (forecast[0].valid || forecast[1].valid) {
      // Формируем строку прогноза для проверки изменений
      String forecastStr = "";
      if (forecast[0].valid) {
        char tempStr[16];
        sprintf(tempStr, "%.0f/%.0f", forecast[0].tempMin, forecast[0].tempMax);
        forecastStr += tempStr;
      }
      if (forecast[1].valid) {
        char tempStr[16];
        sprintf(tempStr, "%.0f/%.0f", forecast[1].tempMin, forecast[1].tempMax);
        forecastStr += tempStr;
      }
      
      // Отрисовываем только при изменении данных
      if (forecastStr != lastForecastStr) {
        // Стираем область прогноза только при изменении
        tft.fillRect(0, 144, 120, 96, TFT_BLACK); // Левая половина (завтра) - опущено на 2px (было 142)
        tft.fillRect(120, 144, 120, 96, TFT_BLACK); // Правая половина (послезавтра) - опущено на 2px (было 142)
        
        // Параметры для прогноза
        int iconSize = 56; // Размер иконки погоды как у текущего дня
        // Влажность на Y=132, высота ~42px, нижний край ~174
        // Прогноз опущен на 5px (было поднято на 35px, стало на 30px)
        int areaStartY = 174 - 30; // Начало области (144, было 142)
        
        // Вычисляем позиции для центрирования: иконка + температура + влажность (без текстового описания)
        // Область: Y от 144 до 240 (96 пикселей, было 98)
        // Иконка 56px + отступы + температура ~14px + влажность ~14px
        // Компактное размещение с минимальными отступами
        int iconY = areaStartY; // Иконка вверху без отступа (144, было 142)
        int tempY = iconY + iconSize + 4; // Температура сразу под иконкой
        int humY = tempY + 19;           // Влажность под температурой
        
        setDateFont(); // Шрифт размера 2 для компактности числовых значений
        
        // Первый день (завтра) - левая половина
        if (forecast[0].valid) {
          int centerX1 = 60; // Центр левой половины (120/2)
          
          // Иконка погоды (центрируем по горизонтали)
          int icon1X = centerX1 - iconSize/2;
          drawWeatherIcon(forecast[0].icon, icon1X, iconY, iconSize);
          
          // Температура с знаком +/- (под иконкой)
          // Очищаем всю область температуры перед отрисовкой
          tft.fillRect(centerX1 - 40, tempY - 10, 80, 20, TFT_BLACK);
          
          tft.setTextDatum(MC_DATUM); // Выравнивание по центру для пропорциональности с иконкой
          
          // Минимальная температура
          String minStr = (forecast[0].tempMin >= 0 ? "+" : "") + String(forecast[0].tempMin, 0);
          int16_t minWidth = tft.textWidth(minStr);
          String maxStr = (forecast[0].tempMax >= 0 ? "+" : "") + String(forecast[0].tempMax, 0);
          int16_t maxWidth = tft.textWidth(maxStr);
          int16_t totalWidth = minWidth + 8 + maxWidth; // Общая ширина обоих чисел с отступом
          int16_t minX = centerX1 - totalWidth / 2; // Центрируем оба числа вместе
          
          // Рисуем минимум с выравниванием слева для точного позиционирования
          tft.setTextDatum(ML_DATUM);
          tft.drawString(minStr, minX, tempY);
          
          // Максимальная температура (с отступом, без использования пробелов)
          int16_t maxX = minX + minWidth + 8; // Отступ 8 пикселей
          tft.drawString(maxStr, maxX, tempY);
          
          // Влажность под температурой
          if (forecast[0].humidity >= 0) { // Проверяем валидность влажности
            // Очищаем большую область для предотвращения артефактов
            tft.fillRect(centerX1 - 40, humY - 10, 80, 20, TFT_BLACK);
            setDateFont(); // Используем тот же шрифт что и для описания
            String humStr = String(forecast[0].humidity);
            humStr += "%"; // Добавляем % отдельно
            tft.setTextDatum(MC_DATUM); // Центрируем влажность
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString(humStr, centerX1, humY);
          } else {
            // Влажность не найдена - очищаем область
            tft.fillRect(centerX1 - 40, humY - 10, 80, 20, TFT_BLACK);
          }
        }
        
        // Второй день (послезавтра) - правая половина
        if (forecast[1].valid) {
          int centerX2 = 180; // Центр правой половины (120 + 120/2)
          
          // Иконка погоды (центрируем по горизонтали)
          int icon2X = centerX2 - iconSize/2;
          drawWeatherIcon(forecast[1].icon, icon2X, iconY, iconSize);
          
          // Температура с знаком +/- (под иконкой)
          // Очищаем всю область температуры перед отрисовкой
          tft.fillRect(centerX2 - 40, tempY - 10, 80, 20, TFT_BLACK);
          
          tft.setTextDatum(MC_DATUM); // Выравнивание по центру для пропорциональности с иконкой
          
          // Минимальная температура
          String minStr = (forecast[1].tempMin >= 0 ? "+" : "") + String(forecast[1].tempMin, 0);
          int16_t minWidth = tft.textWidth(minStr);
          String maxStr = (forecast[1].tempMax >= 0 ? "+" : "") + String(forecast[1].tempMax, 0);
          int16_t maxWidth = tft.textWidth(maxStr);
          int16_t totalWidth = minWidth + 8 + maxWidth; // Общая ширина обоих чисел с отступом
          int16_t minX = centerX2 - totalWidth / 2; // Центрируем оба числа вместе
          
          // Рисуем минимум с выравниванием слева для точного позиционирования
          tft.setTextDatum(ML_DATUM);
          tft.drawString(minStr, minX, tempY);
          
          // Максимальная температура (с отступом, без использования пробелов)
          int16_t maxX = minX + minWidth + 8; // Отступ 8 пикселей
          tft.drawString(maxStr, maxX, tempY);
          
          // Влажность под температурой
          if (forecast[1].humidity >= 0) { // Проверяем валидность влажности
            // Очищаем большую область для предотвращения артефактов
            tft.fillRect(centerX2 - 40, humY - 10, 80, 20, TFT_BLACK);
            setDateFont(); // Используем тот же шрифт что и для описания
            String humStr = String(forecast[1].humidity);
            humStr += "%"; // Добавляем % отдельно
            tft.setTextDatum(MC_DATUM); // Центрируем влажность
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.drawString(humStr, centerX2, humY);
          } else {
            // Влажность не найдена - очищаем область
            tft.fillRect(centerX2 - 40, humY - 10, 80, 20, TFT_BLACK);
          }
        }
        
        lastForecastStr = forecastStr;
      }
    }
  }
}

// Функция displayWiFiStatus удалена

// Автоматическая проверка и переподключение к WiFi
void ensureWiFiConnected() {
  // Текущее состояние модуля WiFi
  wl_status_t status = WiFi.status();
  
  // Если подключение есть, но флаг ещё не обновлён
  if (status == WL_CONNECTED) {
    if (!wifiConnected) {
      wifiConnected = true;
      Serial.println("[WiFi] Подключение установлено/восстановлено");
      Serial.print("[WiFi] IP: ");
      Serial.println(WiFi.localIP());
      
      // Инициализируем NTP один раз после успешного подключения
      if (!ntpInitialized) {
        setupNTP();
        ntpInitialized = true;
      }
      
      // Первый запрос погоды сразу после подключения
      if (weatherEnabled) {
        updateWeather();
        lastWeatherUpdate = millis();
      }
      
      // Сдвигаем таймер проверки тревоги, чтобы сразу была актуальная информация
      lastAlertUpdate = millis();

      // Проверка OTA сразу после восстановления WiFi
      checkForOtaUpdate();
      lastOtaCheck = millis();
    }
    
    return;
  }
  
  // Соединения нет
  if (wifiConnected) {
    Serial.println("[WiFi] Соединение потеряно");
    wifiConnected = false;
  }
  
  unsigned long now = millis();
  
  // Ждём интервал перед следующей попыткой
  if (now - lastWifiReconnectAttempt < wifiReconnectInterval) {
    return;
  }
  
  lastWifiReconnectAttempt = now;
  
  Serial.println("[WiFi] Нет соединения, пробую переподключиться...");
  
  // Небольшая "чистая" попытка переподключения без длинных блокировок
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
}

// Проверка наличия новой прошивки и OTA-обновление
void checkForOtaUpdate() {
  // Дополнительная защита: без Wi-Fi и URL нет смысла продолжать
  if (!wifiConnected) {
    return;
  }

  if (String(OTA_META_URL).length() == 0) {
    return;
  }

  Serial.println("[OTA] Проверка обновления прошивки...");

  // OTA_META_URL обычно HTTPS (raw.githubusercontent.com), поэтому используем TLS клиент.
  // setInsecure() упрощает подключение (без CA/сертификатов); для максимальной безопасности можно добавить проверку сертификата.
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  if (!http.begin(client, OTA_META_URL)) {
    Serial.println("[OTA] Не удалось инициализировать HTTP-клиент для метаданных");
    return;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.print("[OTA] Ошибка HTTP при получении метаданных: ");
    Serial.println(httpCode);
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  if (payload.length() == 0) {
    Serial.println("[OTA] Пустой ответ метаданных");
    return;
  }

  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("[OTA] Ошибка парсинга JSON метаданных: ");
    Serial.println(err.c_str());
    return;
  }

  const char* latestVersion = doc["version"] | "";
  const char* binUrl        = doc["url"] | "";

  if (!latestVersion || !binUrl || strlen(latestVersion) == 0 || strlen(binUrl) == 0) {
    Serial.println("[OTA] В метаданных нет полей 'version' или 'url'");
    return;
  }

  String latest = String(latestVersion);
  String current = String(FIRMWARE_VERSION);

  Serial.print("[OTA] Текущая версия: ");
  Serial.println(current);
  Serial.print("[OTA] Доступная версия: ");
  Serial.println(latest);

  auto parsePart = [](const String& s, int part) -> int {
    int start = 0;
    for (int i = 0; i < part; i++) {
      int dot = s.indexOf('.', start);
      if (dot < 0) return 0;
      start = dot + 1;
    }
    int end = s.indexOf('.', start);
    String sub = (end < 0) ? s.substring(start) : s.substring(start, end);
    return sub.toInt();
  };
  
  auto cmpSemver = [&](const String& a, const String& b) -> int {
    // return -1 if a<b, 0 if equal, 1 if a>b
    int a0 = parsePart(a, 0), b0 = parsePart(b, 0);
    if (a0 != b0) return (a0 < b0) ? -1 : 1;
    int a1 = parsePart(a, 1), b1 = parsePart(b, 1);
    if (a1 != b1) return (a1 < b1) ? -1 : 1;
    int a2 = parsePart(a, 2), b2 = parsePart(b, 2);
    if (a2 != b2) return (a2 < b2) ? -1 : 1;
    return 0;
  };
  
  int cmp = cmpSemver(latest, current);
  if (cmp <= 0) {
    Serial.println("[OTA] Обновление не требуется");
    return;
  }

  Serial.println("[OTA] Найдена новая версия, запускаю обновление...");
  Serial.print("[OTA] URL прошивки: ");
  Serial.println(binUrl);

  otaInProgress = true;
  otaProgressPercent = 0;
  showOtaScreen("OTA UPDATE", "Downloading...");
  drawOtaProgress(0);

  ESPhttpUpdate.onStart([]() {
    otaInProgress = true;
    otaProgressPercent = 0;
    showOtaScreen("OTA UPDATE", "Start...");
    drawOtaProgress(0);
  });

  ESPhttpUpdate.onProgress([](int cur, int total) {
    if (total <= 0) return;
    int p = (cur * 100) / total;
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    if (p != otaProgressPercent) {
      otaProgressPercent = p;
      drawOtaProgress(p);
    }
  });

  ESPhttpUpdate.onEnd([]() {
    drawOtaProgress(100);
    showOtaScreen("OTA UPDATE", "Reboot...");
  });

  ESPhttpUpdate.onError([](int err) {
    otaInProgress = false;
    showOtaScreen("OTA ERROR", String("Code ") + String(err));
    delay(2000);
    hideOtaScreen();
  });

  // Прошивка тоже чаще всего будет скачиваться по HTTPS (GitHub Releases)
  t_httpUpdate_return ret = ESPhttpUpdate.update(client, binUrl);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.print("[OTA] Обновление не удалось. Код: ");
      Serial.println(ESPhttpUpdate.getLastError());
      Serial.print("[OTA] Сообщение: ");
      Serial.println(ESPhttpUpdate.getLastErrorString());
      otaInProgress = false;
      showOtaScreen("OTA ERROR", "Update failed");
      delay(2000);
      hideOtaScreen();
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("[OTA] Обновление не найдено (NO_UPDATES)");
      otaInProgress = false;
      hideOtaScreen();
      break;

    case HTTP_UPDATE_OK:
      Serial.println("[OTA] Обновление успешно, перезагрузка...");
      // ESPhttpUpdate сам вызывает ESP.restart() при успешном обновлении
      break;
  }
}

// Функция для получения описания погоды на русском (полная форма)
String getWeatherDescription(const String& iconCode) {
  String iconType = iconCode.substring(0, 2);
  
  if (iconType == "01") return "Ясно";
  else if (iconType == "02") return "Малооблачно";
  else if (iconType == "03") return "Переменная облачность";
  else if (iconType == "04") return "Облачно";
  else if (iconType == "09") return "Ливень";
  else if (iconType == "10") return "Дождь";
  else if (iconType == "11") return "Гроза";
  else if (iconType == "13") return "Снег";
  else if (iconType == "50") return "Туман";
  else return "Облачно";
}

// Функция для получения сокращённого описания погоды для компактного вывода (Robot20; при необходимости дополняйте синонимы OWM)
String getShortWeatherDescription(const String& fullDescription) {
  if (fullDescription == "Ясно") return "Ясно";
  if (fullDescription == "Малооблачно") return "Мал обл";
  if (fullDescription == "Переменная облачность") return "Перем обл";
  if (fullDescription == "Облачно") return "Обл";
  if (fullDescription == "Ливень") return "Ливень";
  if (fullDescription == "Дождь") return "Дождь";
  if (fullDescription == "Гроза") return "Гроза";
  if (fullDescription == "Снег") return "Снег";
  if (fullDescription == "Туман") return "Туман";
  // По умолчанию возвращаем исходную строку, если не нашли в таблице
  return fullDescription;
}

// Функция для рисования иконки погоды (размер по умолчанию 56x56, можно задать)
void drawWeatherIcon(const String& iconCode, int x, int y, int size) {
  // Очищаем область иконки
  tft.fillRect(x, y, size, size, TFT_BLACK);
  
  // Определяем тип погоды по коду иконки
  // OpenWeatherMap коды: 01 - ясно, 02 - мало облаков, 03 - рассеянные облака, 
  // 04 - облачно, 09 - дождь, 10 - дождь с солнцем, 11 - гроза, 13 - снег, 50 - туман
  String iconType = iconCode.substring(0, 2);
  bool isDay = iconCode.charAt(2) == 'd';
  
  int centerX = x + size / 2;
  int centerY = y + size / 2;
  
  // Масштабируем все элементы пропорционально размеру
  int sunRadius = size / 7;
  int cloudRadius1 = size / 7;
  int cloudRadius2 = size / 6;
  int cloudRadius3 = size / 8;
  
  if (iconType == "01") {
    // Ясно - солнце/луна
    if (isDay) {
      // Солнце - круг с лучами
      tft.fillCircle(centerX, centerY, sunRadius, TFT_YELLOW);
      // Лучи солнца
      for (int i = 0; i < 8; i++) {
        float angle = i * PI / 4;
        int x1 = centerX + cos(angle) * (size / 4);
        int y1 = centerY + sin(angle) * (size / 4);
        int x2 = centerX + cos(angle) * (size / 3);
        int y2 = centerY + sin(angle) * (size / 3);
        tft.drawLine(x1, y1, x2, y2, TFT_YELLOW);
      }
    } else {
      // Луна - полумесяц
      tft.fillCircle(centerX - size / 28, centerY, sunRadius, TFT_WHITE);
      tft.fillCircle(centerX, centerY, sunRadius, TFT_BLACK);
    }
  } else if (iconType == "02") {
    // Мало облаков - солнце/луна с облаком
    if (isDay) {
      // Солнце в углу
      tft.fillCircle(centerX - size / 7, centerY - size / 7, size / 9, TFT_YELLOW);
    } else {
      // Луна в углу
      tft.fillCircle(centerX - size / 7, centerY - size / 7, size / 11, TFT_WHITE);
    }
    // Облако
    tft.fillCircle(centerX + size / 11, centerY + size / 11, cloudRadius1, TFT_WHITE);
    tft.fillCircle(centerX + size / 5, centerY + size / 11, cloudRadius1, TFT_WHITE);
    tft.fillRect(centerX + size / 28, centerY + size / 28, size / 4, size / 7, TFT_WHITE);
  } else if (iconType == "03" || iconType == "04") {
    // Облачно - облако
    tft.fillCircle(centerX - size / 11, centerY + size / 28, cloudRadius1, TFT_WHITE);
    tft.fillCircle(centerX + size / 11, centerY + size / 28, cloudRadius2, TFT_WHITE);
    tft.fillCircle(centerX + size / 5, centerY + size / 28, cloudRadius3, TFT_WHITE);
    tft.fillRect(centerX - size / 5, centerY + size / 28, size / 2, size / 7, TFT_WHITE);
  } else if (iconType == "09" || iconType == "10") {
    // Дождь - облако с каплями
    // Облако
    tft.fillCircle(centerX - size / 11, centerY - size / 11, cloudRadius1, TFT_WHITE);
    tft.fillCircle(centerX + size / 11, centerY - size / 11, cloudRadius2, TFT_WHITE);
    tft.fillCircle(centerX + size / 5, centerY - size / 11, cloudRadius3, TFT_WHITE);
    tft.fillRect(centerX - size / 5, centerY - size / 11, size / 2, size / 7, TFT_WHITE);
    // Капли дождя
    tft.drawLine(centerX - size / 11, centerY + size / 6, centerX - size / 11, centerY + size / 3, TFT_CYAN);
    tft.drawLine(centerX + size / 28, centerY + size / 9, centerX + size / 28, centerY + size / 4, TFT_CYAN);
    tft.drawLine(centerX + size / 7, centerY + size / 5, centerX + size / 7, centerY + size / 3, TFT_CYAN);
  } else if (iconType == "11") {
    // Гроза - облако с молнией
    // Облако
    tft.fillCircle(centerX - size / 11, centerY - size / 11, cloudRadius1, TFT_DARKGREY);
    tft.fillCircle(centerX + size / 11, centerY - size / 11, cloudRadius2, TFT_DARKGREY);
    tft.fillCircle(centerX + size / 5, centerY - size / 11, cloudRadius3, TFT_DARKGREY);
    tft.fillRect(centerX - size / 5, centerY - size / 11, size / 2, size / 7, TFT_DARKGREY);
    // Молния
    tft.fillTriangle(centerX, centerY + size / 28, centerX - size / 19, centerY + size / 7, centerX + size / 28, centerY + size / 7, TFT_YELLOW);
    tft.fillTriangle(centerX, centerY + size / 7, centerX + size / 19, centerY + size / 4, centerX - size / 28, centerY + size / 4, TFT_YELLOW);
  } else if (iconType == "13") {
    // Снег - снежинка
    // Облако
    tft.fillCircle(centerX - size / 11, centerY - size / 11, cloudRadius1, TFT_WHITE);
    tft.fillCircle(centerX + size / 11, centerY - size / 11, cloudRadius2, TFT_WHITE);
    tft.fillCircle(centerX + size / 5, centerY - size / 11, cloudRadius3, TFT_WHITE);
    tft.fillRect(centerX - size / 5, centerY - size / 11, size / 2, size / 7, TFT_WHITE);
    // Снежинка
    for (int i = 0; i < 6; i++) {
      float angle = i * PI / 3;
      int x1 = centerX + cos(angle) * (size / 19);
      int y1 = centerY + size / 6 + sin(angle) * (size / 19);
      int x2 = centerX + cos(angle) * (size / 7);
      int y2 = centerY + size / 6 + sin(angle) * (size / 7);
      tft.drawLine(x1, y1, x2, y2, TFT_WHITE);
    }
  } else if (iconType == "50") {
    // Туман - горизонтальные линии
    for (int i = 0; i < 3; i++) {
      tft.drawLine(centerX - size / 4, centerY - size / 6 + i * size / 6, centerX + size / 4, centerY - size / 6 + i * size / 6, TFT_WHITE);
    }
  } else {
    // Неизвестный тип - просто облако
    tft.fillCircle(centerX - size / 11, centerY + size / 28, cloudRadius1, TFT_WHITE);
    tft.fillCircle(centerX + size / 11, centerY + size / 28, cloudRadius2, TFT_WHITE);
    tft.fillCircle(centerX + size / 5, centerY + size / 28, cloudRadius3, TFT_WHITE);
    tft.fillRect(centerX - size / 5, centerY + size / 28, size / 2, size / 7, TFT_WHITE);
  }
}

void updateWeather() {
  if (weatherAPIKey.length() == 0) return;
  
  Serial.println("[WEATHER] Обновление погоды...");
  
  WiFiClient client;
  HTTPClient http;
  
  // Получаем текущую погоду
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + weatherCity + 
               "&appid=" + weatherAPIKey + "&units=metric&lang=ru";
  
  if (http.begin(client, url)) {
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      
      // Парсинг JSON (упрощенный)
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload);
      
      weather.temperature = doc["main"]["temp"].as<float>();
      weather.humidity = doc["main"]["humidity"].as<int>();
      weather.description = doc["weather"][0]["description"].as<String>();
      weather.icon = doc["weather"][0]["icon"].as<String>();
      weather.valid = true;
      
      Serial.print("[WEATHER] Температура: ");
      Serial.print(weather.temperature);
      Serial.println("°C");
    } else {
      Serial.print("[WEATHER] Ошибка HTTP: ");
      Serial.println(httpCode);
      weather.valid = false;
    }
    
    http.end();
  } else {
    Serial.println("[WEATHER] Ошибка подключения");
    weather.valid = false;
  }
  
  // Получаем прогноз на 2 дня (8 прогнозов по 3 часа = 24 часа = 1 день, берем первые 2 дня)
  Serial.println("[WEATHER] Получение прогноза...");
  String forecastUrl = "http://api.openweathermap.org/data/2.5/forecast?q=" + weatherCity + 
                       "&appid=" + weatherAPIKey + "&units=metric&cnt=16";
  
  if (http.begin(client, forecastUrl)) {
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
      // Используем stream парсинг для экономии памяти
      WiFiClient *stream = http.getStreamPtr();
      Serial.print("[WEATHER] Размер ответа прогноза: ");
      Serial.println(http.getSize());
      
      // Парсинг прогноза с увеличенным буфером
      DynamicJsonDocument doc(12288); // Увеличенный размер для прогноза (12KB)
      DeserializationError error = deserializeJson(doc, *stream);
      
      if (error) {
        Serial.print("[WEATHER] Ошибка парсинга прогноза: ");
        Serial.println(error.c_str());
        forecast[0].valid = false;
        forecast[1].valid = false;
      } else if (doc.containsKey("list") && doc["list"].is<JsonArray>()) {
        JsonArray list = doc["list"];
        Serial.print("[WEATHER] Получено прогнозов: ");
        Serial.println(list.size());
        
        // Собираем данные для каждого из 2 дней
        float day1Min = 999, day1Max = -999;
        float day2Min = 999, day2Max = -999;
        int day1Humidity = 0, day2Humidity = 0;
        int day1HumidityCount = 0, day2HumidityCount = 0; // Для расчета среднего
        String day1Icon = "";
        String day2Icon = "";
        
        // Первые 8 прогнозов (24 часа) = день 1 (завтра)
        // Следующие 8 прогнозов (24 часа) = день 2 (послезавтра)
        size_t maxItems = (list.size() < 16) ? list.size() : 16;
        for (size_t i = 0; i < maxItems; i++) {
          if (i >= list.size()) break;
          JsonObject item = list[i];
          if (item.containsKey("main") && item["main"].containsKey("temp") &&
              item.containsKey("weather") && item["weather"].is<JsonArray>() && 
              item["weather"].size() > 0) {
            float temp = item["main"]["temp"].as<float>();
            String icon = item["weather"][0]["icon"].as<String>();
            int humidity = -1; // -1 означает "не найдено"
            if (item["main"].containsKey("humidity") && item["main"]["humidity"].is<int>()) {
              humidity = item["main"]["humidity"].as<int>();
            }
            
            if (i < 8) {
              // День 1 (завтра)
              if (temp < day1Min) day1Min = temp;
              if (temp > day1Max) day1Max = temp;
              if (day1Icon.length() == 0) day1Icon = icon; // Берем первую иконку дня
              if (humidity >= 0) { // Учитываем и 0, если он валидный
                day1Humidity += humidity;
                day1HumidityCount++;
              }
            } else if (i < 16) {
              // День 2 (послезавтра)
              if (temp < day2Min) day2Min = temp;
              if (temp > day2Max) day2Max = temp;
              if (day2Icon.length() == 0) day2Icon = icon; // Берем первую иконку дня
              if (humidity >= 0) { // Учитываем и 0, если он валидный
                day2Humidity += humidity;
                day2HumidityCount++;
              }
            }
          }
        }
        
        // Вычисляем среднюю влажность для каждого дня
        if (day1HumidityCount > 0) {
          day1Humidity = day1Humidity / day1HumidityCount;
        } else {
          day1Humidity = -1; // Влажность не найдена
        }
        if (day2HumidityCount > 0) {
          day2Humidity = day2Humidity / day2HumidityCount;
        } else {
          day2Humidity = -1; // Влажность не найдена
        }
        
        // Сохраняем прогноз
        if (day1Min < 999 && day1Max > -999) {
          forecast[0].tempMin = day1Min;
          forecast[0].tempMax = day1Max;
          forecast[0].humidity = day1Humidity;
          forecast[0].icon = day1Icon;
          forecast[0].valid = true;
          Serial.print("[WEATHER] День 1: ");
          Serial.print(day1Min);
          Serial.print("/");
          Serial.print(day1Max);
          Serial.print(", влажность: ");
          Serial.println(day1Humidity);
        } else {
          forecast[0].valid = false;
          Serial.println("[WEATHER] День 1: нет данных");
        }
        
        if (day2Min < 999 && day2Max > -999) {
          forecast[1].tempMin = day2Min;
          forecast[1].tempMax = day2Max;
          forecast[1].humidity = day2Humidity;
          forecast[1].icon = day2Icon;
          forecast[1].valid = true;
          Serial.print("[WEATHER] День 2: ");
          Serial.print(day2Min);
          Serial.print("/");
          Serial.print(day2Max);
          Serial.print(", влажность: ");
          Serial.println(day2Humidity);
        } else {
          forecast[1].valid = false;
          Serial.println("[WEATHER] День 2: нет данных");
        }
      } else {
        Serial.println("[WEATHER] Нет массива list в ответе");
        forecast[0].valid = false;
        forecast[1].valid = false;
      }
    } else {
      Serial.print("[WEATHER] Ошибка HTTP прогноза: ");
      Serial.println(httpCode);
      forecast[0].valid = false;
      forecast[1].valid = false;
    }
    
    http.end();
  } else {
    Serial.println("[WEATHER] Ошибка подключения для прогноза");
    forecast[0].valid = false;
    forecast[1].valid = false;
  }
}

// Функция для проверки статуса воздушной тревоги
void updateAlert() {
  if (!wifiConnected) {
    alert.valid = false;
    return;
  }
  
  Serial.println("[ALERT] Проверка статуса воздушной тревоги для Одесской области...");
  
  WiFiClient client;
  HTTPClient http;
  
  // Используем API ubilling.net.ua/aerialalerts/ (рабочий API для повітряних тривог)
  // Документация: https://wiki.ubilling.net.ua/doku.php?id=aerialalertsapi
  // Формат ответа: {"states": {"Одеська": {"alertnow": true/false}, ...}}
  String url = ALERT_API_URL;
  
  Serial.print("[ALERT] Запрос к API: ");
  Serial.println(url);
  
  if (http.begin(client, url)) {
    http.setTimeout(5000); // Таймаут 5 секунд
    http.setUserAgent("ESP8266-WiFiClock/1.0");
    http.addHeader("Accept", "application/json");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // Следуем редиректам
    
    int httpCode = http.GET();
    Serial.print("[ALERT] HTTP код: ");
    Serial.println(httpCode);
    
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.print("[ALERT] Размер ответа: ");
      Serial.println(payload.length());
      if (payload.length() > 0 && payload.length() < 500) {
        Serial.print("[ALERT] Ответ: ");
        Serial.println(payload);
      }
      
      // Парсинг JSON ответа (увеличиваем размер буфера для большого ответа)
      DynamicJsonDocument doc(8192);
      DeserializationError error = deserializeJson(doc, payload);
      
      if (!error && doc.containsKey("states")) {
        alert.active = false;
        JsonObject states = doc["states"].as<JsonObject>();
        
        // Ищем Одесскую область в разных вариантах названий
        String odesaKeys[] = {"Одеська", "Одесская", "Odesa", "odesa", "51"};
        
        for (int i = 0; i < 5; i++) {
          if (states.containsKey(odesaKeys[i])) {
            JsonObject odesaState = states[odesaKeys[i]].as<JsonObject>();
            if (odesaState.containsKey("alertnow") && odesaState["alertnow"].as<bool>()) {
              alert.active = true;
              alert.region = odesaKeys[i];
              break;
            }
          }
        }
        
        // Если не нашли по точному совпадению, ищем по частичному совпадению
        if (!alert.active) {
          for (JsonPair kv : states) {
            String stateName = kv.key().c_str();
            if (stateName.indexOf("Одес") >= 0 || stateName.indexOf("Odesa") >= 0 || 
                stateName.indexOf("odesa") >= 0 || stateName.indexOf("51") >= 0) {
              JsonObject state = kv.value().as<JsonObject>();
              if (state.containsKey("alertnow") && state["alertnow"].as<bool>()) {
                alert.active = true;
                alert.region = stateName;
                break;
              }
            }
          }
        }
        
        alert.valid = true;
        alert.lastCheck = millis();
        
        if (alert.region.length() == 0) {
          alert.region = "Odesa"; // По умолчанию
        }
        
        if (alert.active) {
          Serial.println("[ALERT] ⚠️ ВОЗДУШНАЯ ТРЕВОГА АКТИВНА В ОДЕССКОЙ ОБЛАСТИ!");
        } else {
          Serial.println("[ALERT] ✓ Тревоги нет в Одесской области");
        }
      } else {
        Serial.print("[ALERT] Ошибка парсинга JSON: ");
        if (error) {
          Serial.println(error.c_str());
        } else {
          Serial.println("нет поля 'states'");
        }
        alert.valid = false;
      }
    } else {
      Serial.print("[ALERT] Ошибка HTTP: ");
      Serial.println(httpCode);
      alert.valid = false;
    }
    
    http.end();
  } else {
    Serial.println("[ALERT] Ошибка подключения к API");
    alert.valid = false;
  }
}

// Функция для рисования иконки воздушной тревоги
void drawAlertIcon(int x, int y, int size) {
  // Очищаем область иконки
  tft.fillRect(x, y, size, size, TFT_BLACK);
  
  int centerX = x + size / 2;
  int centerY = y + size / 2;
  
  // Рисуем красный треугольник (предупреждение)
  int triangleSize = size / 2;
  tft.fillTriangle(
    centerX, centerY - triangleSize,           // Верхняя точка
    centerX - triangleSize, centerY + triangleSize / 2,  // Левая нижняя
    centerX + triangleSize, centerY + triangleSize / 2,  // Правая нижняя
    TFT_RED
  );
  
  // Рисуем восклицательный знак внутри треугольника (белый)
  int exclamWidth = size / 8;
  int exclamHeight = size / 3;
  int exclamX = centerX - exclamWidth / 2;
  int exclamY = centerY - exclamHeight / 2;
  
  // Вертикальная линия восклицательного знака
  tft.fillRect(exclamX, exclamY, exclamWidth, exclamHeight, TFT_WHITE);
  // Точка внизу
  tft.fillCircle(centerX, centerY + exclamHeight / 2 + size / 12, size / 12, TFT_WHITE);
}

// Маленькая иконка тревоги под WiFi (красная если тревога, зелёная если нет)
void drawAlertIconSmall(int x, int y, int size) {
  // Очищаем область иконки
  tft.fillRect(x, y, size, size, TFT_BLACK);
  
  // Определяем цвет: красный если тревога активна, зелёный если нет
  uint16_t triangleColor = (alert.valid && alert.active) ? TFT_RED : TFT_GREEN;
  
  int centerX = x + size / 2;
  int centerY = y + size / 2;
  
  // Рисуем треугольник (предупреждение)
  int triangleSize = size / 2;
  tft.fillTriangle(
    centerX, centerY - triangleSize,           // Верхняя точка
    centerX - triangleSize, centerY + triangleSize / 2,  // Левая нижняя
    centerX + triangleSize, centerY + triangleSize / 2,  // Правая нижняя
    triangleColor
  );
  
  // Рисуем восклицательный знак внутри треугольника (белый)
  int exclamWidth = size / 8;
  int exclamHeight = size / 3;
  int exclamX = centerX - exclamWidth / 2;
  int exclamY = centerY - exclamHeight / 2;
  
  // Вертикальная линия восклицательного знака
  tft.fillRect(exclamX, exclamY, exclamWidth, exclamHeight, TFT_WHITE);
  // Точка внизу
  tft.fillCircle(centerX, centerY + exclamHeight / 2 + size / 12, size / 12, TFT_WHITE);
}

// Функция для отображения предупреждения о воздушной тревоге (теперь не используется, заменена на иконку)
void displayAlert() {
  // Функция больше не нужна, иконка отображается в displayTime()
}

void setupWebServer() {
  // Главная страница
  server.on("/", handleRoot);
  
  // API для получения настроек
  server.on("/api/config", HTTP_GET, handleGetConfig);
  
  // API для сохранения настроек
  server.on("/api/config", HTTP_POST, handlePostConfig);
  
  // API для получения статуса
  server.on("/api/status", HTTP_GET, handleGetStatus);
  
  server.begin();
  Serial.println("[WEB] Веб-сервер запущен");
  if (wifiConnected) {
    Serial.print("[WEB] Откройте: http://");
    Serial.println(WiFi.localIP());
  }
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>WiFi Clock - Настройки</title>";
  html += "<style>body{font-family:Arial;max-width:600px;margin:50px auto;padding:20px;}";
  html += "input,select{width:100%;padding:10px;margin:5px 0;box-sizing:border-box;}";
  html += "button{background:#4CAF50;color:white;padding:10px 20px;border:none;cursor:pointer;}";
  html += "button:hover{background:#45a049;}</style></head><body>";
  html += "<h1>WiFi Clock - Настройки</h1>";
  html += "<form id='configForm'>";
  html += "<h2>WiFi</h2>";
  html += "<label>SSID:</label><input type='text' name='ssid' id='ssid' required>";
  html += "<label>Пароль:</label><input type='password' name='password' id='password'>";
  html += "<h2>NTP</h2>";
  html += "<label>Сервер:</label><input type='text' name='ntpServer' id='ntpServer'>";
  html += "<label>Автоматическое летнее/зимнее время (EET/EEST):</label><input type='checkbox' name='dstAuto' id='dstAuto'>";
  html += "<label>Часовой пояс (секунды, если авто выкл.):</label><input type='number' name='gmtOffset' id='gmtOffset'>";
  html += "<h2>Погода</h2>";
  html += "<label>Включить погоду:</label><input type='checkbox' name='weatherEnabled' id='weatherEnabled'>";
  html += "<label>API ключ:</label><input type='text' name='weatherAPIKey' id='weatherAPIKey'>";
  html += "<label>Город:</label><input type='text' name='weatherCity' id='weatherCity'>";
  html += "<button type='submit'>Сохранить</button>";
  html += "</form>";
  html += "<div id='status'></div>";
  html += "<script>";
  html += "fetch('/api/config').then(r=>r.json()).then(d=>{";
  html += "document.getElementById('ssid').value=d.ssid||'';";
  html += "document.getElementById('password').value='';";
  html += "document.getElementById('ntpServer').value=d.ntpServer||'';";
  html += "document.getElementById('dstAuto').checked=d.dstAuto!==false;";
  html += "document.getElementById('gmtOffset').value=d.gmtOffset||0;";
  html += "document.getElementById('weatherEnabled').checked=d.weatherEnabled||false;";
  html += "document.getElementById('weatherAPIKey').value=d.weatherAPIKey||'';";
  html += "document.getElementById('weatherCity').value=d.weatherCity||'';";
  html += "});";
  html += "document.getElementById('configForm').onsubmit=function(e){";
  html += "e.preventDefault();";
  html += "const formData=new FormData(this);";
  html += "const data={};formData.forEach((v,k)=>data[k]=v);";
  html += "data.dstAuto=document.getElementById('dstAuto').checked;";
  html += "data.weatherEnabled=document.getElementById('weatherEnabled').checked;";
  html += "fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})";
  html += ".then(r=>r.json()).then(d=>{document.getElementById('status').innerHTML='<p style=color:green>Сохранено! Перезагрузка...</p>';setTimeout(()=>location.reload(),2000);});";
  html += "};";
  html += "setInterval(()=>fetch('/api/status').then(r=>r.json()).then(d=>{";
  html += "document.getElementById('status').innerHTML='<p>WiFi: '+(d.wifi?'Подключен':'Отключен')+' | Время: '+(d.timeSynced?'Синхронизировано':'Не синхронизировано')+'</p>';";
  html += "}),5000);";
  html += "</script></body></html>";
  
  server.send(200, "text/html", html);
}

void handleGetConfig() {
  DynamicJsonDocument doc(512);
  doc["ssid"] = wifiSSID;
  doc["ntpServer"] = ntpServer;
  doc["gmtOffset"] = gmtOffset_sec;
  doc["dstAuto"] = dstAuto;
  doc["weatherEnabled"] = weatherEnabled;
  doc["weatherAPIKey"] = weatherAPIKey;
  doc["weatherCity"] = weatherCity;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handlePostConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }
  
  DynamicJsonDocument doc(512);
  deserializeJson(doc, server.arg("plain"));
  
  if (doc.containsKey("ssid")) wifiSSID = doc["ssid"].as<String>();
  if (doc.containsKey("password") && doc["password"].as<String>().length() > 0) {
    wifiPassword = doc["password"].as<String>();
  }
  if (doc.containsKey("ntpServer")) ntpServer = doc["ntpServer"].as<String>();
  if (doc.containsKey("gmtOffset")) gmtOffset_sec = doc["gmtOffset"].as<long>();
  if (doc.containsKey("dstAuto")) dstAuto = doc["dstAuto"].as<bool>();
  if (doc.containsKey("weatherEnabled")) weatherEnabled = doc["weatherEnabled"].as<bool>();
  if (doc.containsKey("weatherAPIKey")) weatherAPIKey = doc["weatherAPIKey"].as<String>();
  if (doc.containsKey("weatherCity")) weatherCity = doc["weatherCity"].as<String>();
  
  // Переподключение к WiFi если изменился SSID
  bool wifiChanged = (WiFi.SSID() != wifiSSID);
  bool ntpChanged = doc.containsKey("ntpServer") || doc.containsKey("gmtOffset") || doc.containsKey("dstAuto");
  bool weatherChanged = doc.containsKey("weatherEnabled") || doc.containsKey("weatherAPIKey") || doc.containsKey("weatherCity");
  
  if (wifiChanged) {
    ntpInitialized = false;
    // Переподключение к WiFi
    WiFi.disconnect();
    delay(100);
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
    wifiConnected = false;
    timeSynced = false;
  }
  
  // Сброс флагов при изменении настроек
  if (ntpChanged) {
    ntpInitialized = false;
    timeSynced = false;
  }
  
  if (weatherChanged) {
  }
  
  // NTP и погода инициализируются автоматически после проверки интернета в loop()
  
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleGetStatus() {
  DynamicJsonDocument doc(256);
  doc["wifi"] = wifiConnected;
  doc["timeSynced"] = timeSynced;
  doc["ip"] = wifiConnected ? WiFi.localIP().toString() : "";
  doc["rssi"] = wifiConnected ? WiFi.RSSI() : 0;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}
