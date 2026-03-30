// User_Setup.h для дисплея ST7789V 240x240 на ESP8266
// Копия для PlatformIO: <User_Setup.h> подхватывается из include/ (см. lib/TFT_eSPI/User_Setup.h — редирект)

#define USER_SETUP_INFO "User_Setup for ST7789 240x240 ESP8266"

// ##################################################################################
//
// Section 1. Call up the right driver file and any options for it
//
// ##################################################################################

// Only define one driver, the other ones must be commented out
#define ST7789_DRIVER      // Full configuration option for ST7789

// ##################################################################################
//
// Section 2. Define the pins that are used to interface with the display here
//
// ##################################################################################

// For NodeMCU - use pin numbers in the form PIN_Dx where Dx is the NodeMCU pin designation
#define TFT_MISO  PIN_D6  // Automatically assigned with ESP8266 if not defined
#define TFT_MOSI  PIN_D7  // GPIO13 - SPI данные
#define TFT_SCLK  PIN_D5  // GPIO14 - SPI такт

// CS заземлен (GND) - не используется
#define TFT_CS    -1      // Chip select заземлен

// Правильные пины подключения
#define TFT_DC    PIN_D3  // GPIO0 - Data Command control pin
#define TFT_RST   PIN_D4  // GPIO2 - Reset pin

// Подсветка
#define TFT_BL    PIN_D1  // GPIO5 - LED back-light

// ##################################################################################
//
// Section 3. Define the way the display is connected to the ESP8266
//
// ##################################################################################

// For ST7789, ST7735, ILI9163 and GC9A01 ONLY, define the pixel width and height in portrait orientation
#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// ##################################################################################
//
// Section 4. Not used for ESP8266
//
// ##################################################################################

// ##################################################################################
//
// Section 5. ESP8266 SPI pin definitions
//
// ##################################################################################

// The ESP8266 has 3 SPI interfaces:
//   SPI - Uses the default SPI pins, can use any GPIO for CS/DC
//   HSPI - Uses GPIO12 (MISO), GPIO13 (MOSI), GPIO14 (SCLK), can use any GPIO for CS/DC
//   VSPI - Not available on ESP8266

// For ESP8266, the default SPI pins are:
//   MISO = GPIO12 (D6)
//   MOSI = GPIO13 (D7)
//   SCLK = GPIO14 (D5)

// ##################################################################################
//
// Section 6. Define the SPI clock frequency
//
// ##################################################################################

// Define the SPI clock frequency, this affects the graphics rendering speed.
// With a ST7789 display 20MHz works OK
#define SPI_FREQUENCY  20000000

// Optional reduced SPI frequency for reading TFT
#define SPI_READ_FREQUENCY  20000000

// ##################################################################################
//
// Section 7. Other options
//
// ##################################################################################

// For ST7789, ST7735 and ILI9341 ONLY, define the colour order IF the blue and red are swapped on your display
// Try ONE option at a time to find the correct colour order for your display
// #define TFT_RGB_ORDER TFT_RGB  // Colour order Red-Green-Blue
// #define TFT_RGB_ORDER TFT_BGR  // Colour order Blue-Green-Red

// If colours are inverted (white shows as black) then uncomment one of the next 2 lines
// #define TFT_INVERSION_ON
// #define TFT_INVERSION_OFF

// Подсветка: на многих модулях ST7789 240×240 BLK активен при LOW (как в displayInit ранее).
// Должно совпадать с фактической схемой; иначе tft.init() включит «выключенный» уровень → чёрный экран.
#define TFT_BACKLIGHT_ON LOW

// Сглаженные шрифты из массивов (Robot60/20/35) — обязательно для loadFont()/unloadFont()
#define SMOOTH_FONT
