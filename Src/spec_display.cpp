#include "spec_display.h"

#include "mbed.h"
#include "mbed_io_ex.h"
#include "lcd.h"
#include "SPI_TFT_ILI9341.h"
#include "Arial12x12.h"
#include "Arial28x28.h"

static PinName s_lcd_cs (LCD_CS_PORT,  LCD_CS_PIN);
static PinName s_lcd_dc (LCD_DC_PORT,  LCD_DC_PIN);
static PinName s_lcd_rst(LCD_RST_PORT, LCD_RST_PIN);

static SPI_HandleTypeDef s_lcd_spi = {.Instance = LCD_SPI};

static PinName s_lcd_mosi(LCD_SDI_PORT, LCD_SDI_PIN, &s_lcd_spi);
static PinName s_lcd_miso(LCD_SDO_PORT, LCD_SDO_PIN, &s_lcd_spi);
static PinName s_lcd_sclk(LCD_SCK_PORT, LCD_SCK_PIN, &s_lcd_spi);

static SPI_TFT_ILI9341* s_lcd;

static uint16_t s_spec_bmp[SPEC_LEN];
static uint16_t s_color_ramp[256];
static int s_next_row = LCD_H - 1;

struct rgb {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

struct vrgb {
	uint16_t   v;
	struct rgb color;
};

struct vrgb s_rainbow[] = {
	{0,     {68, 34, 153}},
	{0x18,  {68, 68, 221}},
	{0x38,  {17, 170, 187}},
	{0x58,  {34, 204, 170}},
	{0x78,  {105, 208, 37}},
	{0x88,  {170, 204, 34}},
	{0xc0,  {210, 90, 16}},
	{0x100, {238, 17, 0}}
};

static inline uint16_t pack_rgb(uint8_t r, uint8_t g, uint8_t b)
{
	// 5 red | 6 green | 5 blue
	return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static inline int interpolate(int x, int x0, int x1, int y0, int y1)
{
	return y1 * (x - x0) / (x1 - x0) + y0 * (x1 - x) / (x1 - x0);
}

static inline uint16_t color_ramp(uint8_t v)
{
	int const n = sizeof(s_rainbow)/sizeof(s_rainbow[0]);
	int i;
	for (i = n - 1; i >= 0; --i) {
		if (v >= s_rainbow[i].v)
			break;
	}
	int v0 = s_rainbow[i].v, v1 = s_rainbow[i+1].v;
	struct rgb const* rgb0 = &s_rainbow[i].color;
	struct rgb const* rgb1 = &s_rainbow[i+1].color;
	uint8_t r = interpolate(v, v0, v1, rgb0->r, rgb1->r);
	uint8_t g = interpolate(v, v0, v1, rgb0->g, rgb1->g);
	uint8_t b = interpolate(v, v0, v1, rgb0->b, rgb1->b);
	return pack_rgb(r, g, b);
}

#define COLOR_MAP_W 128
#define COLOR_MAP_H 32

static void show_info(void)
{
	static uint16_t color_map_bmp[COLOR_MAP_W*COLOR_MAP_H];
	int i, j;
	for (i = 0; i < COLOR_MAP_H; ++i)
		for (j = 0; j < COLOR_MAP_W; ++j)
			color_map_bmp[i*COLOR_MAP_W+j] = s_color_ramp[2*j];

	s_lcd->background(Black);
	s_lcd->cls();
	s_lcd->foreground(White);
	s_lcd->set_font((unsigned char*)Arial28x28);
	s_lcd->locate(57, 100);
	s_lcd->puts("Spectrum");

	s_lcd->Bitmap((LCD_W - COLOR_MAP_W) / 2, 50, COLOR_MAP_W, COLOR_MAP_H, (unsigned char*)color_map_bmp);
	s_lcd->set_font((unsigned char*)Arial12x12);
	s_lcd->locate(35, 60);
	s_lcd->puts("0");
	s_lcd->locate(195, 60);
	s_lcd->puts("90dB");
	s_lcd->locate(4, 4);
	s_lcd->puts("0 Hz");
}

void spec_display_init(void)
{
	for (int i = 0; i < 256; ++i) {
		s_color_ramp[i] = color_ramp(i);
	}

	__HAL_RCC_GPIOE_CLK_ENABLE();
	__HAL_RCC_GPIOH_CLK_ENABLE();
	__HAL_RCC_SPI1_CLK_ENABLE();

	pin_init_out(s_lcd_cs);
	pin_init_out(s_lcd_dc);
	pin_init_out(s_lcd_rst);

	pin_init_af(s_lcd_mosi, LCD_SPI_AF);
	pin_init_af(s_lcd_miso, LCD_SPI_AF);
	pin_init_af(s_lcd_sclk, LCD_SPI_AF);

	// SPI baudrate is set to 40 MHz (PCLK1/SPI_BaudRatePrescaler = 80/2 = 40 MHz) 
	spi_handle_init(&s_lcd_spi, 8, 0, SPI_BAUDRATEPRESCALER_2);

	s_lcd = new SPI_TFT_ILI9341(s_lcd_mosi, s_lcd_miso, s_lcd_sclk, s_lcd_cs, s_lcd_rst, s_lcd_dc);
	s_lcd->set_orientation(2);

	show_info();
}

/* Quick & dirty transformation to logarithmic scale */
static inline uint8_t to_log_scale(float32_t f)
{
	int const offset = 1250;
	int x;
	union {
		float32_t f;
		uint32_t  u;
	} v;
	v.f = f;
	x = ((v.u >> 20) & 0x7ff) - offset;
	if (x < 0)
		return 0;
	if (x > 255)
		return 255;
	return x;
}

void spec_display_show(float32_t spec[SPEC_LEN])
{
	float32_t const *pSpec = spec, *pEnd = pSpec + SPEC_LEN;
	uint16_t* pBmp = s_spec_bmp;
	for (; pSpec < pEnd; ++pSpec) {
		uint8_t l = to_log_scale(*pSpec);
		*pBmp++ = s_color_ramp[l];
	}
	s_lcd->Bitmap(0, s_next_row, SPEC_LEN, 1, (unsigned char*)s_spec_bmp);
	if (--s_next_row < 0) {
		s_next_row = LCD_H - 1;
	}
	s_lcd->set_scrolling_offset(LCD_H - 1 - s_next_row);
}

