#include "pico/time.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "st7789/ST7789_TFT.hpp"

#define TEST_DELAY1 1000
#define TEST_DELAY2 2000

ST7789_TFT myTFT;

static void Setup(void)
{
    stdio_init_all();
    TFT_MILLISEC_DELAY(TEST_DELAY1);
    printf("TFT start\r\n");

    // Hardware SPI on spi0
    uint32_t TFT_SCLK_FREQ = 8000; // kHz = 8 MHz
    myTFT.TFTInitSPIType(TFT_SCLK_FREQ, spi0);

    // GPIO according to library README
    int8_t SDIN_TFT = 19;
    int8_t SCLK_TFT = 18;
    int8_t DC_TFT   = 3;
    int8_t CS_TFT   = 2;
    int8_t RST_TFT  = 17;
    myTFT.TFTSetupGPIO(RST_TFT, DC_TFT, CS_TFT, SCLK_TFT, SDIN_TFT);

    // Start with the same geometry as the official hello example
    uint16_t OFFSET_COL = 0;
    uint16_t OFFSET_ROW = 0;
    uint16_t TFT_WIDTH  = 240;
    uint16_t TFT_HEIGHT = 280;
    myTFT.TFTInitScreenSize(OFFSET_COL, OFFSET_ROW, TFT_WIDTH, TFT_HEIGHT);

    myTFT.TFTST7789Initialize();
}

int main(void)
{
    Setup();

    printf("Library version: %u\r\n", myTFT.TFTLibVerNumGet());

    myTFT.TFTfillScreen(ST7789_RED);
    TFT_MILLISEC_DELAY(TEST_DELAY2);

    myTFT.TFTfillScreen(ST7789_GREEN);
    TFT_MILLISEC_DELAY(TEST_DELAY2);

    myTFT.TFTfillScreen(ST7789_BLUE);
    TFT_MILLISEC_DELAY(TEST_DELAY2);

    myTFT.TFTfillScreen(ST7789_BLACK);
    myTFT.TFTFontNum(myTFT.TFTFont_Default);

    char text[] = "ST7789 OK";
    myTFT.TFTdrawText(20, 40, text, ST7789_WHITE, ST7789_BLACK, 2);

    while (true) {
        sleep_ms(1000);
    }
}
