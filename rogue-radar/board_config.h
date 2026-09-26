#pragma once

// Board capabilities and pin assignments. The original T-Embed remains the
// default; define ROGUE_RADAR_BOARD_T_EMBED_CC1101 for the CC1101 hardware.
#if defined(ROGUE_RADAR_BOARD_T_EMBED_CC1101)

#define RR_BOARD_NAME          "T-Embed CC1101"
#define RR_SHARED_SPI          1
#define RR_HAS_GPS             0
#define RR_HAS_RECORDER        0
#define RR_HAS_BATTERY_METER   0
#define RR_HAS_POWER_OFF       0
#define RR_HAS_SPEAKER         1

// Switched 3.3 V rail, display and encoder.
#define RR_POWER_EN_PIN        15
#define POWER_PIN              RR_POWER_EN_PIN
#define LCD_BL_PIN             21
#define ENCODER_A               4
#define ENCODER_B               5
#define ENCODER_BTN             0

// The CC1101 board uses an eight-pixel WS2812 chain. APA102 is unsupported.
#define WS2812_PIN             14
#define APA102_DI              -1
#define APA102_CLK             -1
#define NUM_LEDS                8

// LCD, SD and CC1101 share one SPI bus and must be selected exclusively.
#define RR_SPI_SCLK            11
#define RR_SPI_MISO            10
#define RR_SPI_MOSI             9
#define RR_TFT_CS              41
#define RR_TFT_DC              16
#define RR_TFT_RST             -1
#define RR_SD_CS               13
#define RR_CC1101_CS           12
#define RR_CC1101_IO0           3
#define RR_CC1101_IO2          38
#define RR_CC1101_SW1          47
#define RR_CC1101_SW0          48
#define RR_NRF24_CS            44

#define SD_CS                  RR_SD_CS
#define SD_SCLK                RR_SPI_SCLK
#define SD_MISO                RR_SPI_MISO
#define SD_MOSI                RR_SPI_MOSI

// On-board speaker I2S and control I2C.
#define SOUND_I2S_BCLK         46
#define SOUND_I2S_WCLK         40
#define SOUND_I2S_DOUT          7
#define RR_I2C_SDA              8
#define RR_I2C_SCL             18
#define BATTERY_ADC_PIN        -1

// Disabled capabilities still receive sentinel pins so common declarations
// compile while their setup and UI are gated by the RR_HAS_* macros.
#define GPS_RX_PIN             -1
#define GPS_TX_PIN             -1
#define AUDIO_RECORDER_MIC_BCLK -1
#define AUDIO_RECORDER_MIC_LRCK -1
#define AUDIO_RECORDER_MIC_DIN  -1
#define AUDIO_RECORDER_MIC_MCLK -1

#else

#define RR_BOARD_NAME          "T-Embed Non CC1101"
#define RR_SHARED_SPI          0
#define RR_HAS_GPS             1
#define RR_HAS_RECORDER        1
#define RR_HAS_BATTERY_METER   1
#define RR_HAS_POWER_OFF       1
#define RR_HAS_SPEAKER         1

#define RR_POWER_EN_PIN        46
#define POWER_PIN              RR_POWER_EN_PIN
#define LCD_BL_PIN             15
#define ENCODER_A               1
#define ENCODER_B               2
#define ENCODER_BTN             0

#define WS2812_PIN             -1
#define APA102_DI              42
#define APA102_CLK             45
#define NUM_LEDS                7

#define RR_SPI_SCLK            40
#define RR_SPI_MISO            38
#define RR_SPI_MOSI            41
#define RR_TFT_CS              10
#define RR_TFT_DC              13
#define RR_TFT_RST              9
#define RR_SD_CS               39
#define RR_CC1101_CS           -1
#define RR_CC1101_IO0          -1
#define RR_CC1101_IO2          -1
#define RR_CC1101_SW1          -1
#define RR_CC1101_SW0          -1
#define RR_NRF24_CS            -1

#define SD_CS                  RR_SD_CS
#define SD_SCLK                RR_SPI_SCLK
#define SD_MISO                RR_SPI_MISO
#define SD_MOSI                RR_SPI_MOSI

#define SOUND_I2S_BCLK          7
#define SOUND_I2S_WCLK          5
#define SOUND_I2S_DOUT          6
#define RR_I2C_SDA             18
#define RR_I2C_SCL              8
#define BATTERY_ADC_PIN         4

#define GPS_RX_PIN             44
#define GPS_TX_PIN             43
#define AUDIO_RECORDER_MIC_BCLK 47
#define AUDIO_RECORDER_MIC_LRCK 21
#define AUDIO_RECORDER_MIC_DIN  14
#define AUDIO_RECORDER_MIC_MCLK 48

#endif
