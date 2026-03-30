# Локальная копия TFT_eSPI (вендор)

Базовая версия: **2.5.43** (как у `bodmer/TFT_eSPI@^2.5.43` до переноса в репозиторий).

Библиотека лежит в `lib/TFT_eSPI` и **не подтягивается** из реестра PlatformIO — обновления Bodmer вносите вручную (копия + патч ниже).

## Патч ESP8266

Файл `Processors/TFT_eSPI_ESP8266.h`: при `SMOOTH_FONT` **не** включается `FONT_FS_AVAILABLE` и не подключается `FS.h`, чтобы не тянуть устаревший `SPIFFS` в `Smooth_font.h`.

Сглаженные шрифты из массивов (`loadFont(const uint8_t[])`) работают как раньше. Загрузка `.vlw` с файловой системы в этой конфигурации недоступна.

## Настройки дисплея

**Источник правды:** `include/User_Setup.h` (PlatformIO подхватывает `<User_Setup.h>` из `include/`). В `lib/TFT_eSPI/User_Setup.h` и `lib/User_Setup.h` — редиректы на него.

Обязательно **`#define SMOOTH_FONT`** для `loadFont()` / `unloadFont()` (Robot).
