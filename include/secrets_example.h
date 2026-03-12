// Пример файла с секретами для прошивки WiFi Clock.
// НЕ ИСПОЛЬЗУЕТСЯ НАПРЯМУЮ — скопируйте его в secrets.h и заполните своими данными.
//
// ВНИМАНИЕ: include/secrets.h добавлен в .gitignore и не должен попадать в репозиторий.
// На реальном устройстве вы можете позже изменить Wi-Fi и другие параметры
// через веб-интерфейс, но эти значения нужны как стартовые по умолчанию.

#pragma once

#include <Arduino.h>

// Wi-Fi по умолчанию (можно переопределить через веб-интерфейс)
#define WIFI_SSID        "YOUR_WIFI_SSID"
#define WIFI_PASSWORD    "YOUR_WIFI_PASSWORD"

// NTP сервер по умолчанию
#define NTP_SERVER       "pool.ntp.org"

// OpenWeatherMap API ключ и город по умолчанию
#define WEATHER_API_KEY  "YOUR_OPENWEATHER_API_KEY"
#define WEATHER_CITY     "YOUR_CITY_NAME"

// URL API воздушной тревоги по умолчанию
#define ALERT_API_URL    "http://ubilling.net.ua/aerialalerts/"

