#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClient.h>
#include <time.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>

// Секреты (Wi‑Fi, API ключи) живут в include/secrets.h (не коммитится).
// Для сборки из репозитория (CI/GitHub Actions) используем fallback на secrets_example.h.
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #include "secrets_example.h"
#endif

// WiFi настройки
extern String wifiSSID;
extern String wifiPassword;

// NTP настройки
extern String ntpServer;
extern long gmtOffset_sec;
extern int daylightOffset_sec;

// Погода настройки
extern String weatherAPIKey;
extern String weatherCity;
extern bool weatherEnabled;

// Глобальные объекты
extern TFT_eSPI tft;
extern ESP8266WebServer server;

// Переменные состояния
extern bool wifiConnected;
extern bool timeSynced;
extern unsigned long lastUpdateTime;
extern unsigned long lastWeatherUpdate;
extern unsigned long lastAlertUpdate;

extern const unsigned long updateInterval;
extern const unsigned long weatherUpdateInterval;
extern const unsigned long alertUpdateInterval;

extern const unsigned long wifiReconnectInterval;
extern unsigned long lastWifiReconnectAttempt;

extern const unsigned long otaCheckInterval;
extern unsigned long lastOtaCheck;

extern bool ntpInitialized;

// Кэш для подписи текущей погоды (для корректной перерисовки)
extern String lastWeatherDesc;

// Структура данных погоды
struct WeatherData {
  float temperature;
  int humidity;
  String description;
  String icon;
  bool valid;
};

// Структура для прогноза на день
struct ForecastDay {
  float tempMin;
  float tempMax;
  int humidity; // -1 означает "не найдено"
  String icon;
  bool valid;
};

extern WeatherData weather;
extern ForecastDay forecast[2]; // Прогноз на 2 дня

// Структура данных о воздушной тревоге
struct AlertData {
  bool active;      // Активна ли тревога
  String region;    // Регион (опционально)
  unsigned long lastCheck; // Время последней проверки
  bool valid;       // Валидны ли данные
};

extern AlertData alert;

// Константы
#define FIRMWARE_VERSION "1.0.4"

// Функции
void setTimeFont();      // Установка шрифта для времени
void setDateFont();      // Установка шрифта для даты
void setTempFont();      // Установка шрифта для температуры
void setHumidityFont();  // Установка шрифта для влажности
void displayInit();
void displayVersion();
void setupNTP();
void updateTime();
void displayTime();
String getWeatherDescription(const String& iconCode);
String getShortWeatherDescription(const String& fullDescription);
void drawWeatherIcon(const String& iconCode, int x, int y, int size = 56);
void updateWeather();
void updateAlert();  // Проверка статуса воздушной тревоги
void drawAlertIcon(int x, int y, int size); // Рисование иконки воздушной тревоги
void drawAlertIconSmall(int x, int y, int size); // Рисование маленькой иконки тревоги под WiFi
void setupWebServer();
void handleRoot();
void handleGetConfig();
void handlePostConfig();
void handleGetStatus();

void ensureWiFiConnected();
void checkForOtaUpdate();

#endif // MAIN_H
