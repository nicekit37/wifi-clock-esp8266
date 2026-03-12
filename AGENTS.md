# AGENTS.md

> Project map for AI agents. Keep this file up-to-date as the project evolves.

## Project Overview
WiFi‑часы на ESP8266 с TFT‑дисплеем, синхронизацией времени по NTP, показом погоды/прогноза и статуса воздушной тревоги, настраиваемые через встроенный веб‑интерфейс.

## Tech Stack
- **Language:** C++ (Arduino)
- **Framework:** Arduino (ESP8266, PlatformIO)
- **Board:** ESP8266 (esp12e)
- **Display:** TFT_eSPI
- **Filesystem:** LittleFS
- **Networking:** ESP8266WiFi, ESP8266WebServer, WiFiClientSecure
- **Data Format:** JSON (ArduinoJson)

## Project Structure
```
.
├─ platformio.ini           # Конфигурация PlatformIO для ESP8266/esp12e
├─ src/
│  ├─ main.cpp              # Основная прошивка часов (логика WiFi, NTP, погоды, тревоги, UI)
│  ├─ Robot20.h             # Графика/шрифты для мелкого текста
│  ├─ Robot35.h             # Графика/шрифты для среднего текста
│  └─ Robot60.h             # Графика/шрифты для крупных часов
├─ lib/
│  ├─ User_Setup.h          # Настройки TFT_eSPI под конкретный дисплей и пины ESP8266
│  └─ README                # Шаблон PlatformIO для пользовательских библиотек
├─ .pio/                    # Каталог сборки PlatformIO и загруженных библиотек
└─ .ai-factory/
   └─ DESCRIPTION.md        # Спецификация проекта и технологического стека
```

## Key Entry Points
| File           | Purpose |
|----------------|---------|
| `src/main.cpp` | Точка входа прошивки (`setup()`/`loop()`), логика часов, сети, погоды и тревоги. |
| `platformio.ini` | Конфигурация сборки/загрузки для платы ESP8266 (esp12e) в PlatformIO. |
| `lib/User_Setup.h` | Аппаратно‑зависимые настройки TFT_eSPI (разрешение, пины, параметры дисплея). |
| `.ai-factory/DESCRIPTION.md` | Описание проекта, стек и архитектурные заметки высокого уровня. |

## Documentation
| Document | Path | Description |
|----------|------|-------------|
| DESCRIPTION | `.ai-factory/DESCRIPTION.md` | Основное описание проекта и технологического стека. |

## AI Context Files
| File | Purpose |
|------|---------|
| `AGENTS.md` | Эта карта проекта для AI‑агентов. |
| `.ai-factory/DESCRIPTION.md` | Спецификация проекта и стека. |
| `.ai-factory/ARCHITECTURE.md` | Архитектурные решения и рекомендации по структуре кода. |

## Agent Rules
- Не изменяйте автоматически файлы в `.pio/` — это артефакты сборки и внешние библиотеки.
- При работе с дисплеем учитывайте аппаратные настройки из `lib/User_Setup.h`.
- Для сетевых функций и API‑запросов переиспользуйте уже существующие паттерны в `src/main.cpp` (WiFi, HTTP, JSON).
- При добавлении новых фич старайтесь минимизировать использование глобального состояния и по возможности группировать связанную логику во вспомогательные функции.
