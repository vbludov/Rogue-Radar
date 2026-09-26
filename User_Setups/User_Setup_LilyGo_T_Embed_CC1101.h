// LilyGO T-Embed CC1101: ST7789 170 x 320 on the board shared SPI bus.

#define USER_SETUP_ID 214

#define ST7789_DRIVER

#define TFT_WIDTH  170
#define TFT_HEIGHT 320

// ESP32-S3 direct register writes are unstable on the default FSPI path with
// newer Arduino-ESP32 packages. LilyGO's reference setup uses HSPI.
#define USE_HSPI_PORT

#define TFT_INVERSION_ON

#define TFT_BL     21
#define TFT_MISO   10
#define TFT_MOSI    9
#define TFT_SCLK   11
#define TFT_CS     41
#define TFT_DC     16
#define TFT_RST    -1

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000

// Required when TFT, SD and CC1101 take turns on the same physical bus.
#define SUPPORT_TRANSACTIONS
