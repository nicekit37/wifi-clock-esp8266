#include <Arduino.h>
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
long gmtOffset_sec = 7200;       // GMT+2 (Украина, зима)
int daylightOffset_sec = 0;

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
const unsigned long updateInterval = 1000; // Обновление времени каждую секунду
const unsigned long weatherUpdateInterval = 600000; // Обновление погоды каждые 10 минут
const unsigned long alertUpdateInterval = 30000; // Обновление статуса тревоги каждые 30 секунд
const unsigned long wifiReconnectInterval = 30000; // Пауза между попытками переподключения к WiFi (мс)

unsigned long lastWifiReconnectAttempt = 0;

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
bool lastWifiState = false;

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
  }
  
  // Запуск веб-сервера
  setupWebServer();
  
  // Очистка экрана и начальная отрисовка
  tft.fillScreen(TFT_BLACK);
  
  Serial.println("\n========================================");
  Serial.println("Инициализация завершена");
  Serial.println("========================================\n");
}

void loop() {
  uint32_t now = millis();
  
  // Автоматическое переподключение к WiFi, если сеть появилась позже
  ensureWiFiConnected();
  
  // Обработка веб-сервера
  server.handleClient();
  
  // Обновление времени каждую секунду
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

// Функции для установки шрифтов (легко заменить на сглаженные шрифты в будущем)
void setTimeFont() {
  // Выгружаем предыдущий шрифт перед загрузкой нового
  tft.unloadFont();
  // Используем шрифт Robot60 из заголовочного файла (только для времени)
  tft.loadFont(Robot60);
}

void setDateFont() {
  // Выгружаем предыдущий шрифт перед загрузкой нового
  tft.unloadFont();
  // Используем шрифт Robot20 для даты
  tft.loadFont(Robot20);
}

void setTempFont() {
  // Выгружаем предыдущий шрифт перед загрузкой нового
  tft.unloadFont();
  // Используем шрифт Robot35 для температуры и влажности
  tft.loadFont(Robot35);
}

void setHumidityFont() {
  // Выгружаем предыдущий шрифт перед загрузкой нового
  tft.unloadFont();
  // Для анти-алиасинга: раскомментировать и загрузить файл шрифта
  // tft.loadFont("/fonts/NotoSans12");
  // tft.setFreeFont(FF16);
  tft.setTextSize(2); // 24 pt - стандартный шрифт
}

void displayInit() {
  Serial.println("[DISPLAY] Инициализация дисплея...");
  
  // Инициализация файловой системы для сглаженных шрифтов
  if (LittleFS.begin()) {
    Serial.println("[DISPLAY] LittleFS инициализирована");
  } else {
    Serial.println("[DISPLAY] Ошибка инициализации LittleFS (сглаженные шрифты недоступны)");
  }
  
  // Инициализация подсветки
  #ifdef TFT_BL
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, LOW); // LOW = включено
  #endif
  
  // Инициализация дисплея
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  
  // Настройка текста
  tft.setTextDatum(MC_DATUM);
  
  Serial.println("[DISPLAY] Дисплей инициализирован");
}

void displayVersion() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  
  // Название
  tft.setTextSize(3);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("WiFi Clock", 120, 70);
  
  // Версия
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  String versionStr = "v" + String(FIRMWARE_VERSION);
  tft.drawString(versionStr, 120, 100);
  
  // Информация о системе
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String cpuStr = String(ESP.getCpuFreqMHz()) + " MHz";
  tft.drawString(cpuStr, 120, 125);
  
  String memStr = String(ESP.getFreeHeap() / 1024) + " KB RAM";
  tft.drawString(memStr, 120, 140);
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
  Serial.print("[NTP] Часовой пояс: GMT");
  Serial.print((gmtOffset_sec >= 0 ? "+" : ""));
  Serial.println(gmtOffset_sec / 3600);
  
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer.c_str());
  
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
      // Время ещё не синхронизировано
      if (!debugPrinted) {
        Serial.println("[TIME] Время ещё не синхронизировано, пропускаем отображение");
      }
      debugPrinted = true;
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
  
  // Время всегда на одной позиции (иконка тревоги заменяет иконку погоды, не перекрывает время)
  int timeY = 30;
  
  // Верхняя строка: Время слева (72 pt) + Иконка Wi-Fi справа
  setTimeFont(); // Установка шрифта для времени (легко заменить на сглаженный)
  tft.setTextDatum(ML_DATUM); // Выравнивание слева
  
  // Часы (обновляем только при изменении)
  // Позиция X=10 для часов (ширина ~50 пикселей для размера 6 с двумя цифрами)
  if (timeinfo.tm_hour != lastHour) {
    if (lastHour >= 0) {
      char hourStr[3];
      sprintf(hourStr, "%02d", lastHour);
      tft.setTextColor(TFT_BLACK, TFT_BLACK);
      tft.drawString(hourStr, 10, timeY);
    }
    char hourStr[3];
    sprintf(hourStr, "%02d", timeinfo.tm_hour);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(hourStr, 10, timeY);
    lastHour = timeinfo.tm_hour;
  }
  
  // Двоеточие (мигает каждые 500мс, обновляется отдельно)
  // Позиция X=75 после часов (10 + 50 + 15 для увеличенного отступа)
  static bool colonVisible = true;
  static unsigned long lastColonToggle = 0;
  unsigned long nowMillis = millis();
  if (nowMillis - lastColonToggle >= 500) {
    colonVisible = !colonVisible;
    lastColonToggle = nowMillis;
  }
  

  
  if (colonVisible) {
      // Сначала затираем черный прямоугольник черным (на всякий случай)
      // Рисуем двоеточие
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.drawString(":", 77, 27);
    } else {
      // Закрашиваем область двоеточия черным цветом (прямоугольник точно по размеру двоеточия)
      tft.fillRect(80, 0 , 10, 45, TFT_BLACK); // Черный прямоугольник вместо двоеточия), colonRectY, 10, 35, TFT_BLACK); // Черный прямоугольник вместо двоеточия
    }
  
  // Минуты (обновляем только при изменении)
  // Позиция X=111 после двоеточия (75 + 3 + 33 для увеличенного отступа)
  if (timeinfo.tm_min != lastMinute) {
    if (lastMinute >= 0) {
      char minStr[3];
      sprintf(minStr, "%02d", lastMinute);
      tft.setTextColor(TFT_BLACK, TFT_BLACK);
      tft.drawString(minStr, 93, timeY);
    }
    char minStr[3];
    sprintf(minStr, "%02d", timeinfo.tm_min);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(minStr, 93, timeY);
    lastMinute = timeinfo.tm_min;
  }
  
  // Секунды (обновляем только при изменении, шрифтом Robot20, сверху после минут)
  if (timeinfo.tm_sec != lastSecond) {
    // Вычисляем позицию секунд: после минут, сверху
    int minWidth = tft.textWidth("00"); // Ширина минут (примерно)
    int secX = 93 + minWidth + 5; // Позиция X после минут с отступом 5px
    int secY = timeY - 15; // Позиция Y выше минут на 15 пикселей
    
    // Стираем старые секунды
    if (lastSecond >= 0) {
      setDateFont(); // Robot20 для секунд
      tft.setTextDatum(ML_DATUM);
      char secStr[3];
      sprintf(secStr, "%02d", lastSecond);
      tft.setTextColor(TFT_BLACK, TFT_BLACK);
      tft.drawString(secStr, secX, secY);
    }
    
    // Рисуем новые секунды
    setDateFont(); // Robot20 для секунд
    tft.setTextDatum(ML_DATUM);
    char secStr[3];
    sprintf(secStr, "%02d", timeinfo.tm_sec);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(secStr, secX, secY);
    
    lastSecond = timeinfo.tm_sec;
    
    // Возвращаем шрифт для времени (Robot60) на случай, если дальше используется
    setTimeFont();
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
      tft.setTextColor(TFT_BLACK, TFT_BLACK);
      tft.drawString(lastDateStr, 10, 67);
    }
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(dateStr, 10, 67);
    lastDateStr = dateStr;
    lastDay = timeinfo.tm_mday;
    lastWeekday = timeinfo.tm_wday;
  }
  
  // Иконка погоды справа с подписью, по центру между датой и температурой (крупнее, отступ от края)
  if (weatherEnabled && weather.valid) {
    // Дата на Y=70, температура на Y=100, центр между ними: Y=85
    // Иконка 56x56 + подпись ~18px + отступ 4px = ~78px общая высота
    // Центр блока на Y=85, значит верх иконки на Y=85-39≈46, опущено на 7px = 53
    int iconSize = 56; // Увеличена с 48 до 56
    int iconY = 75; // Опущено на 7 пикселей (было 46)
    
    // Устанавливаем шрифт для измерения ширины текста
    tft.setTextSize(2); // Увеличенный шрифт для подписи
    
    // Находим самое длинное описание для правильного центрирования иконки (используем самую длинную сокращённую форму)
    String longestDesc = "Перем обл"; // Самая длинная сокращённая подпись (заглавные — шрифт без строчных)
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
    
    // Обновляем иконку погоды только если она изменилась
    if (weather.icon != lastWeatherIcon) {
      // Стираем старую иконку и подпись (увеличенная область, учитывая возможное смещение)
      // Учитываем, что текст может быть шире иконки
      int clearWidth = (maxTextWidth > iconSize) ? maxTextWidth + 10 : iconSize + 10;
      tft.fillRect(iconX - 5, iconY, clearWidth, 78, TFT_BLACK);
      
      // Рисуем иконку (увеличенную)
      drawWeatherIcon(weather.icon, iconX, iconY, iconSize);
      
      // Рисуем подпись под иконкой (крупнее)
      tft.setTextDatum(TC_DATUM); // Выравнивание по центру сверху
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      int textY = iconY + 53; // Под иконкой с отступом 4px
      tft.drawString(weatherDesc, textCenterX, textY);
      
      lastWeatherIcon = weather.icon; // Обновляем только если показываем погоду
    }
  }
  
  // Третья строка: Температура влево (44 pt) с окружностью и буквой C, под датой
  if (weatherEnabled && weather.valid) {
    String tempStr = (weather.temperature >= 0 ? "+" : "") + String(weather.temperature, 0);
    if (tempStr != lastTempStr) {
      setTempFont(); // Установка шрифта для температуры (легко заменить на сглаженный)
      tft.setTextDatum(ML_DATUM); // Выравнивание слева
      
      // Стираем старую температуру, символ градуса и букву C
      if (lastTempStr.length() > 0) {
        tft.setTextColor(TFT_BLACK, TFT_BLACK);
        // Стираем текст, символ градуса и букву C (увеличенная область по высоте и ширине)
        tft.fillRect(10, 88, 130, 36, TFT_BLACK);
      }
      
      // Рисуем новую температуру
      int16_t textX = 10;
      int16_t textY = 100; // Поднято под дату
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(tempStr, textX, textY);
      
      // Рисуем символ градуса и букву C после температуры
      int16_t textWidth = tft.textWidth(tempStr);
      int16_t degreeX = textX + textWidth + 3; // 3 пикселя отступа от текста
      tft.drawString("°", degreeX, textY); // Символ градуса
      int16_t degreeWidth = tft.textWidth("°");
      int16_t cX = degreeX + degreeWidth + 2; // 2 пикселя отступа от символа градуса
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
      setTempFont(); // Тот же шрифт что и температура
      tft.setTextDatum(ML_DATUM); // Выравнивание слева
      
      // Стираем старую влажность (прямоугольной областью, чтобы не оставалось артефактов)
      if (lastHumStr.length() > 0) {
        tft.setTextColor(TFT_BLACK, TFT_BLACK);
        // Прямоугольник покрывает число и символ %, но не задевает прогноз ниже
        tft.fillRect(26, 124, 120, 32, TFT_BLACK);
      }
      
      // Рисуем число
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(humNumStr, 30, 132);
      
      // Рисуем только символ % (без пробела) с небольшим отступом от цифры
      int16_t numWidth = tft.textWidth(humNumStr);
      
      // Позиция % - небольшой положительный отступ для визуального разделения
      // Рисуем только "%" без пробела, чтобы черный фон не перекрывал цифры
      tft.drawString("%", 30 + numWidth + 7, 132); // +2 пикселя отступа - небольшой промежуток, но компактно (опущено на 2px: было 130)
      
      lastHumStr = humFullStr;
    }
  }
  
  // Прогноз на 2 дня под влажностью (разделено пополам по горизонтали)
  // Свободное пространство: Y от 172 до 240 (68 пикселей), X от 0 до 240
  // Завтра: левая половина (X от 0 до 120)
  // Послезавтра: правая половина (X от 120 до 240)
  if (weatherEnabled) {
    // Отображаем прогноз, если хотя бы один день валиден
    if (forecast[0].valid || forecast[1].valid) {
      static String lastForecastStr = "";
      
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

// Функция для получения сокращённого описания погоды для компактного вывода
// Используем заглавные первые буквы — в шрифте есть заглавные кириллические (Пн, Пт), строчные могут отображаться как прямоугольник
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
  String url = "http://ubilling.net.ua/aerialalerts/";
  
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
  html += "<label>Часовой пояс (секунды):</label><input type='number' name='gmtOffset' id='gmtOffset'>";
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
  html += "document.getElementById('gmtOffset').value=d.gmtOffset||0;";
  html += "document.getElementById('weatherEnabled').checked=d.weatherEnabled||false;";
  html += "document.getElementById('weatherAPIKey').value=d.weatherAPIKey||'';";
  html += "document.getElementById('weatherCity').value=d.weatherCity||'';";
  html += "});";
  html += "document.getElementById('configForm').onsubmit=function(e){";
  html += "e.preventDefault();";
  html += "const formData=new FormData(this);";
  html += "const data={};formData.forEach((v,k)=>data[k]=v);";
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
  if (doc.containsKey("weatherEnabled")) weatherEnabled = doc["weatherEnabled"].as<bool>();
  if (doc.containsKey("weatherAPIKey")) weatherAPIKey = doc["weatherAPIKey"].as<String>();
  if (doc.containsKey("weatherCity")) weatherCity = doc["weatherCity"].as<String>();
  
  // Переподключение к WiFi если изменился SSID
  bool wifiChanged = (WiFi.SSID() != wifiSSID);
  bool ntpChanged = doc.containsKey("ntpServer") || doc.containsKey("gmtOffset");
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
