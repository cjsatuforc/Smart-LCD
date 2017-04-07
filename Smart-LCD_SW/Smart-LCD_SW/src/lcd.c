/**
 * \file
 *
 * \brief LCD module
 *
 */

/**
 * \mainpage LCD
 *
 * \par LCD communication and command module
 *
 * This module communicates with the LCD device via a 8 bit wide databus.
 * The initialization and a command-set is managed by this module. See
 * specification "UC1608_20041104" for details.
 *
 * \par Content
 *
 * -# Include the ASF header files (through asf.h)
 *
 */

/*
 * Include header files for all drivers that have been imported from
 * Atmel Software Framework (ASF).
 */
/*
 * Support and FAQ: visit <a href="http://www.atmel.com/design-support/">Atmel Support</a>
 */
#include <asf.h>
#include <stdio.h>
#include <math.h>

#include "gfx_mono/sysfont.h"
#include "main.h"

#include "lcd.h"

extern float				g_temp;

extern status_t				g_status;

extern showData_t			g_showData;


extern uint8_t				g_u8_DEBUG11,
							g_u8_DEBUG12,
							g_u8_DEBUG13;

extern uint_fast32_t		g_u32_DEBUG21;

extern float				g_f_DEBUG31;


#define ANIMATION_TRAIN_WAGGON_CNT		4
#define ANIMATION_TRAIN_BLANK_LEN       (1 + 15 + ANIMATION_TRAIN_WAGGON_CNT * (3 + 8) + 1)

static uint8_t s_lcd_ram_read_nonvalid = 0;
static char    s_lcd_prepare_buf[48];
static	int    s_animation_train_origin = 0;
static 	int8_t s_animation_dx = 0;
static float   s_animation_time_last_temp  = 0.f;
static float   s_animation_time_last_train = 0.f;
static uint8_t s_animation_train_left[ANIMATION_TRAIN_BLANK_LEN];
static uint8_t s_animation_train_right[ANIMATION_TRAIN_BLANK_LEN];


// hold a copy of a font size in the PROG memory section
SYSFONT_DEFINE_GLYPHS;


uint8_t lcd_bus_read_status(void)
{
	uint8_t data;
	irqflags_t flags = cpu_irq_save();

	PORTD = 0xff;													// Enable pull-ups (when bus-drivers are disabled)
	DDRD  = 0x00;													// Disable bus-drivers
	ioport_set_pin_level(LCD_CD, false);							// Select command-interface
	ioport_set_pin_level(LCD_RW, true);								// Bus-read
	ioport_set_pin_level(LCD_EN, true);								// Bus-enable
	ioport_set_pin_level(LCD_EN, false);							// Bus-disable
	data = PIND;													// Access needs 50ns: therefore take 2 cycles with 33ns each

	cpu_irq_restore(flags);
	return data;
}

void lcd_bus_write_cmd(uint8_t cmd)
{
	irqflags_t flags = cpu_irq_save();

	PORTD = cmd;													// Data to be written
	DDRD  = 0xff;													// Enable bus-drivers
	ioport_set_pin_level(LCD_CD, false);							// Select command-interface
	ioport_set_pin_level(LCD_RW, false);							// Bus-write
	ioport_set_pin_level(LCD_EN, true);								// Bus-enable
	ioport_set_pin_level(LCD_EN, false);							// Bus-disable

	cpu_irq_restore(flags);
}

void lcd_bus_write_ram(uint8_t data)
{
	irqflags_t flags = cpu_irq_save();

	PORTD = data;													// Data to be written
	DDRD  = 0xff;													// Enable bus-drivers
	ioport_set_pin_level(LCD_CD, true);								// Select RAM-interface
	ioport_set_pin_level(LCD_RW, false);							// Bus-write
	ioport_set_pin_level(LCD_EN, true);								// Bus-enable
	ioport_set_pin_level(LCD_EN, false);							// Bus-disable

	cpu_irq_restore(flags);
}

uint8_t lcd_bus_read_ram(void)
{
	uint8_t data;
	irqflags_t flags = cpu_irq_save();

	PORTD = 0xff;													// Enable pull-ups (when bus-drivers are disabled)
	DDRD  = 0x00;													// Disable bus-drivers
	ioport_set_pin_level(LCD_CD, true);								// Select RAM-interface
	ioport_set_pin_level(LCD_RW, true);								// Bus-read
	ioport_set_pin_level(LCD_EN, true);								// Bus-enable
	ioport_set_pin_level(LCD_EN, false);							// Bus-disable

	if (s_lcd_ram_read_nonvalid) {
		data = PIND;												// Discard void data from pipeline
		ioport_set_pin_level(LCD_EN, true);							// Bus-enable
		ioport_set_pin_level(LCD_EN, false);						// Bus-disable
	}
	data = PIND;													// Access needs 50ns: therefore take 2 cycles with 33ns each

	s_lcd_ram_read_nonvalid = false;								// since here read returns valid data

	cpu_irq_restore(flags);
	return data;
}


uint8_t lcd_bounds_x(int x)
{
	if (x < 0) {
		return 0;
	} else if (x >= GFX_MONO_LCD_WIDTH) {
		return (uint8_t) GFX_MONO_LCD_WIDTH - 1;
	} else {
		return (uint8_t) x;
	}
}

uint8_t lcd_bounds_y(int y)
{
	if (y < 0) {
		return 0;
		} else if (y >= GFX_MONO_LCD_HEIGHT) {
		return (uint8_t) GFX_MONO_LCD_HEIGHT - 1;
		} else {
		return (uint8_t) y;
	}
}


void lcd_enable(uint8_t on)
{
	if (!on) {
		lcd_bus_write_cmd(0b10101110);									// Disable DC[2] (Display)

	} else {
		lcd_bus_write_cmd(0b00101000 | C_LCD_PWR_CTRL);					// Set Power Control
		lcd_bus_write_cmd(0b00100000 | C_LCD_MR_TC);					// Set MR and TC
		lcd_bus_write_cmd(0b11101000 | C_LCD_BIASRATIO);				// Set Bias Ratio
		lcd_bus_write_cmd(0b10000001);									// Set Gain and PM (A)
		lcd_bus_write_cmd(C_LCD_GAIN_PM);								// Set Gain and PM (B)

		lcd_bus_write_cmd(0b11000000 | C_LCD_MAPPING);					// Set Mapping
		lcd_bus_write_cmd(0b10001000 | C_LCD_AC);						// Set RAM Address Control

		lcd_bus_write_cmd(0b01000000);									// Set Start Line (0)
		lcd_bus_write_cmd(0b10010000);									// Set Fixed Lines (0)

		lcd_bus_write_cmd(0b10110000);									// Set Page Address (0)
		lcd_bus_write_cmd(0b00000000);									// Set Column Address LSB (0)
		lcd_bus_write_cmd(0b00010000);									// Set Column Address MSB (0)
		s_lcd_ram_read_nonvalid = true;

		lcd_bus_write_cmd(0b11101111);									// Set Cursor Mode
		lcd_bus_write_cmd(0b11101110);									// Reset Cursor Mode (now CR := CA)
		lcd_bus_write_cmd(0b10100100);									// Disable DC[1] (all pixel on)
		lcd_bus_write_cmd(0b10100110);									// Disable DC[0] (all pixel inverse)
		lcd_bus_write_cmd(0b10101111);									// Enable  DC[2] (Display)
	}
}

void lcd_page_set(uint8_t page)
{
	if ((0 <= page && page) < (GFX_MONO_LCD_PAGES)) {
		lcd_bus_write_cmd(0b10110000 | page);					// Set Page Address

		s_lcd_ram_read_nonvalid = 1;
	}
}

void lcd_col_set(uint8_t col)
{
	if ((0 <= col) && (col < GFX_MONO_LCD_WIDTH)) {
		lcd_bus_write_cmd(0b00000000 | ( col       & 0x0f));	// Set Column Address LSB
		lcd_bus_write_cmd(0b00010000 | ((col >> 4) & 0x0f));	// Set Column Address MSB

		s_lcd_ram_read_nonvalid = 1;
	}
}

void lcd_cr(void)
{
	lcd_bus_write_cmd(0b00000000);								// Set Column Address LSB (0)
	lcd_bus_write_cmd(0b00010000);								// Set Column Address MSB (0)

	s_lcd_ram_read_nonvalid = 1;
}

void lcd_home(void)
{
	lcd_bus_write_cmd(0b10110000);								// Set Page Address (0)
	lcd_cr();
}

void lcd_cls(void)
{
	/* Blank LCD RAM */
	for (uint8_t page = 0; page < GFX_MONO_LCD_PAGES; ++page) {
		lcd_bus_write_cmd(0b10110000 | page);						// Set Page Address
		lcd_bus_write_cmd(0b00000000);								// Set Column Address LSB (0)
		lcd_bus_write_cmd(0b00010000);								// Set Column Address MSB (0)

		for (uint8_t cnt = GFX_MONO_LCD_WIDTH; cnt; --cnt) {		// clear all columns of that page
			lcd_bus_write_ram(0);
		}
	}	

	/* Set cursor to home position */
	lcd_home();
}


static void lcd_show_new_clk_state(uint8_t clk_state, int16_t phase100)
{
	if (phase100 > 18000) {
		phase100 = 18000;
		} else if (phase100 < -18000) {
		phase100 = -18000;
	}

	int dx = (int) (90.0f * (phase100 / 18000.0f));

	uint8_t ox = 150;
	if (dx < 0) {
		ox += dx;
		dx = -dx;
	}

	snprintf(s_lcd_prepare_buf, sizeof(s_lcd_prepare_buf), "ClkState: 0x%1X", clk_state);
	gfx_mono_draw_string(s_lcd_prepare_buf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  0 * LCD_SHOW_LINE_HEIGHT, &sysfont);
	gfx_mono_draw_string("Phase   :", 0,  120, &sysfont);

	gfx_mono_generic_draw_filled_rect(60, 120,  180, 7, GFX_PIXEL_CLR);
	if (dx) {
		switch (clk_state) {
			case 0xf:
			gfx_mono_generic_draw_filled_rect(ox, 120, dx, 7, GFX_PIXEL_SET);
			break;

			case 0x7:
			gfx_mono_generic_draw_rect(ox, 120,  dx, 7, GFX_PIXEL_SET);
			break;

			case 0x3:
			case 0x2:
			case 0x1:
			gfx_mono_generic_draw_filled_rect(60, 120,  180, 3, GFX_PIXEL_SET);
			break;
		}

		} else {
		gfx_mono_generic_draw_line(150, 121, 150, 125, GFX_PIXEL_SET);
	}
}

uint8_t lcd_show_new_data(void)
{
	static uint8_t idx = 1;

	/* First entries are showed first, when modified */

	irqflags_t flags = cpu_irq_save();
	cpu_irq_disable();

	/* Always */
	if (g_showData.newTime) {
		g_showData.newTime = false;
		snprintf(s_lcd_prepare_buf, sizeof(s_lcd_prepare_buf), "UTC     : %02d:%02d.%02d",
		g_showData.time_hour, g_showData.time_minute, g_showData.time_second);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(s_lcd_prepare_buf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  2 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		return 255;
	}

	/* Always */
	if (g_showData.newDate) {
		g_showData.newDate = false;
		snprintf(s_lcd_prepare_buf, sizeof(s_lcd_prepare_buf), "Date    : %02d.%02d.%04d",
		g_showData.date_day, g_showData.date_month, g_showData.date_year);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(s_lcd_prepare_buf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  1 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		return 254;
	}

	/* Always */
	if (g_showData.newClkState) {
		g_showData.newClkState = false;
		cpu_irq_restore(flags);
		lcd_show_new_clk_state(g_showData.clkState_clk_state, g_showData.clkState_phase100);
		return 253;
	}


	/* Slot 1 */
	if (g_showData.newPpm && (idx <= 1)) {
		g_showData.newPpm = false;
		snprintf(s_lcd_prepare_buf, sizeof(s_lcd_prepare_buf), "Precis. : %03d.%03d ppm",
		g_showData.ppm_int, g_showData.ppm_frac1000);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(s_lcd_prepare_buf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  3 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		idx = 2;
		return 1;
	}

	/* Slot 2 */
	if (g_showData.newPwm && (idx <= 2)) {
		g_showData.newPwm = false;
		snprintf(s_lcd_prepare_buf, sizeof(s_lcd_prepare_buf), "PWM     : %3d.%03d / 256 %%",
		g_showData.pwm_int, g_showData.pwm_frac1000);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(s_lcd_prepare_buf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  4 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		idx = 3;
		return 2;
	}

	/* Slot 3 */
	if (g_showData.newPv && (idx <= 3)) {
		g_showData.newPv = false;
		snprintf(s_lcd_prepare_buf, sizeof(s_lcd_prepare_buf), "PullVolt: %1d.%03d V",
		g_showData.pv_int, g_showData.pv_frac1000);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(s_lcd_prepare_buf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  5 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		idx = 4;
		return 3;
	}

	/* Slot 4 */
	if (g_showData.newSatUse && (idx <= 4)) {
		g_showData.newSatUse = false;
		snprintf(s_lcd_prepare_buf, sizeof(s_lcd_prepare_buf), "SatUse  : West=%02d East=%02d Used=%02d sats",
		g_showData.satUse_west, g_showData.satUse_east, g_showData.satUse_used);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(s_lcd_prepare_buf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  6 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		idx = 5;
		return 4;
	}

	/* Slot 5 */
	if (g_showData.newSatDop && (idx <= 5)) {
		g_showData.newSatDop = false;
		snprintf(s_lcd_prepare_buf, sizeof(s_lcd_prepare_buf), "Sat DOP : %03d.%02d",
		(int) (g_showData.satDop_dop100 / 100.0f), g_showData.satDop_dop100 % 100);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(s_lcd_prepare_buf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  7 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		idx = 6;
		return 5;
	}

	/* Slot 6 */
	if (g_showData.newPosState && (idx <= 6)) {
		g_showData.newPosState = false;
		snprintf(s_lcd_prepare_buf, sizeof(s_lcd_prepare_buf), "SatState: FI=%1d M2=%1d",
		g_showData.posState_fi, g_showData.posState_m2);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(s_lcd_prepare_buf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  8 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		idx = 7;
		return 6;
	}

	/* Slot 7 */
	if (g_showData.newPosLat && (idx <= 7)) {
		g_showData.newPosLat = false;
		snprintf(s_lcd_prepare_buf, sizeof(s_lcd_prepare_buf), "Sat Lat : %c  %02d^%02d.%04d'",
		g_showData.posLat_sgn, g_showData.posLat_deg, g_showData.posLat_min_int, g_showData.posLat_min_frac10000);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(s_lcd_prepare_buf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  9 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		idx = 8;
		return 7;
	}

	/* Slot 8 */
	if (g_showData.newPosLon && (idx <= 8)) {
		g_showData.newPosLon = false;
		snprintf(s_lcd_prepare_buf, sizeof(s_lcd_prepare_buf), "Sat Lon : %c %03d^%02d.%04d'",
		g_showData.posLon_sgn, g_showData.posLon_deg, g_showData.posLon_min_int, g_showData.posLon_min_frac10000);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(s_lcd_prepare_buf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP + 10 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		idx = 9;
		return 8;
	}

	/* Slot 9 */
	if (g_showData.newPosHeight && (idx <= 9)) {
		g_showData.newPosHeight = false;
		snprintf(s_lcd_prepare_buf, sizeof(s_lcd_prepare_buf), "Sat Hgt : %04d m",
		g_showData.pos_heigth);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(s_lcd_prepare_buf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP + 11 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		idx = 10;
		return 9;
	}

	/* Turn back to first position */
	if (idx >= 10) {
		idx = 1;
	}

	cpu_irq_restore(flags);
	return 0;
}


static void s_lcd_test_lines(void)
{
	const int oy = 10;
	const int h = 18;
	const int w = GFX_MONO_LCD_WIDTH;

	static int loop = 0;
	static uint8_t sw = 0;

	if (loop++ < h) {
		uint8_t y11 = oy + loop;
		uint8_t y12 = oy + h - loop - 1;
		gfx_mono_generic_draw_line (0, y11, w - 1, y12, sw % 3);

	} else if (loop < (h + w)) {
		uint8_t x21 = (loop - h);
		uint8_t x22 = w - (loop - h) - 1;
		gfx_mono_generic_draw_line (x21, oy + h - 1, x22, oy, sw % 3);

	} else {
		loop = 0;
		if (++sw >= 3) {
			sw = 0;
		}
	}
}

static void s_lcd_test_temp(void)
{
	static float t_last = 0.0f;
	float	t;

	s_task();

	if (!g_status.doAnimation) {
		return;
	}

	irqflags_t flags = cpu_irq_save();
	t = g_temp;
	cpu_irq_restore(flags);

	if (t < 0.f) {
		t = 0.f;
	}

	if (fabs(t - t_last) < 0.01f) {
		return;
	}
	t_last = t;

	snprintf(s_lcd_prepare_buf, sizeof(s_lcd_prepare_buf), " T=%2d.%02d^C ", (int) t, ((int) (t * 100.0f)) % 100);
	gfx_mono_draw_string(s_lcd_prepare_buf, 180, 70, &sysfont);
}

void lcd_animation_prepare(void)
{
	int idx;

	// set initial values
	s_animation_train_origin = -ANIMATION_TRAIN_BLANK_LEN;
	s_animation_dx = 1;

	/* prepare train */	
	for (idx = 0; idx < ANIMATION_TRAIN_BLANK_LEN; ++idx) {
		if (!idx) {
			s_animation_train_left[idx] = 0;

		} else if (idx < 3) {
			s_animation_train_left[  idx] = 0b11111000;
			s_animation_train_left[++idx] = 0b11011000;
		} else if (idx <  5) {
			s_animation_train_left[  idx] = 0b11011111;
		} else if (idx < 11) {
			s_animation_train_left[  idx] = 0b11011000;
		} else if (idx < 16) {
			s_animation_train_left[  idx] = 0b11111000;

		} else if (idx == ANIMATION_TRAIN_BLANK_LEN - 1) {
			s_animation_train_left[  idx] = 0;

		} else if (!((idx - 16) % 11)) {
			s_animation_train_left[  idx] = 0b01000000;
			s_animation_train_left[++idx] = 0b01000000;
			s_animation_train_left[++idx] = 0b01000000;
			s_animation_train_left[++idx] = 0b11111000;
			s_animation_train_left[++idx] = 0b11111000;
			s_animation_train_left[++idx] = 0b11001000;
			s_animation_train_left[++idx] = 0b11111000;
			s_animation_train_left[++idx] = 0b11111000;
			s_animation_train_left[++idx] = 0b11001000;
			s_animation_train_left[++idx] = 0b11111000;
			s_animation_train_left[++idx] = 0b11111000;
		} else {
			s_animation_train_left[idx] = 0;
		}
	}

	for (idx = 0; idx < ANIMATION_TRAIN_BLANK_LEN; ++idx) {
		s_animation_train_right[ANIMATION_TRAIN_BLANK_LEN - idx - 1] = s_animation_train_left[idx];		// x-mirror
	}

	/* prepare free line for train */
	gfx_mono_generic_draw_filled_rect(0, (GFX_MONO_LCD_PAGES - 1) * GFX_MONO_LCD_PIXELS_PER_BYTE, GFX_MONO_LCD_WIDTH, GFX_MONO_LCD_PIXELS_PER_BYTE, GFX_PIXEL_CLR);
}

void lcd_animation_loop(void)
{
	if (s_animation_dx) {
		float now = get_abs_time();

		if ((now - s_animation_time_last_train) >= 0.04f) {  // 25x per sec
			s_animation_time_last_train = now;
			s_animation_train_origin += s_animation_dx;

			if (s_animation_train_origin <= (-10 - ANIMATION_TRAIN_BLANK_LEN)) {
				s_animation_dx = 1;
			} else if (s_animation_train_origin >= (GFX_MONO_LCD_WIDTH + 10)) {
				s_animation_dx = -1;
			}

			if (s_animation_dx < 0) {
				// Draw train left
				if (s_animation_train_origin >= 0 && s_animation_train_origin < GFX_MONO_LCD_WIDTH) {
					gfx_mono_lcd_uc1608_put_page(s_animation_train_left, GFX_MONO_LCD_PAGES - 1, s_animation_train_origin, ANIMATION_TRAIN_BLANK_LEN);				// full width
				} else if (-ANIMATION_TRAIN_BLANK_LEN < s_animation_train_origin && s_animation_train_origin < 0) {
					gfx_mono_lcd_uc1608_put_page(s_animation_train_left - s_animation_train_origin, GFX_MONO_LCD_PAGES - 1, 0, ANIMATION_TRAIN_BLANK_LEN + s_animation_train_origin);	// left: reduced width
				}

				} else {
				// Draw train right
				if (s_animation_train_origin >= 0 && s_animation_train_origin < GFX_MONO_LCD_WIDTH) {
					gfx_mono_lcd_uc1608_put_page(s_animation_train_right, GFX_MONO_LCD_PAGES - 1, s_animation_train_origin, ANIMATION_TRAIN_BLANK_LEN);				// full width
				} else if (-ANIMATION_TRAIN_BLANK_LEN < s_animation_train_origin && s_animation_train_origin < 0) {
					gfx_mono_lcd_uc1608_put_page(s_animation_train_right - s_animation_train_origin, GFX_MONO_LCD_PAGES - 1, 0, ANIMATION_TRAIN_BLANK_LEN + s_animation_train_origin);	// left: reduced width
				}
			}
		}

		if ((now - s_animation_time_last_temp) >= 0.50f) {  // 2x per sec
			s_animation_time_last_temp = now;
			s_lcd_test_temp();
		}

		s_lcd_test_lines();  // Every cycle
	}	
}

void lcd_test(uint8_t pattern_bm)
{
	if (pattern_bm & (1 << 0)) {
		// TEST 1
		for (int i = 0; i < GFX_MONO_LCD_WIDTH; ++i) {
			lcd_bus_write_ram(i);
		}
	}

	if (pattern_bm & (1 << 1)) {
		// TEST 2
		lcd_page_set(2);
		lcd_cr();
		for (int i = 0, pos = 231; i < GFX_MONO_LCD_WIDTH; ++i, ++pos) {
			if (!(i % 7)) {
				lcd_bus_write_ram(0);
			}
			lcd_bus_write_ram(PROGMEM_READ_BYTE(&(sysfont_glyphs[pos])));
		}
	}

	if (pattern_bm & (1 << 2)) {
		// TEST 3
		gfx_mono_draw_pixel(4 + 0, 40 + 0, GFX_PIXEL_SET);
		gfx_mono_draw_pixel(4 + 1, 40 + 1, GFX_PIXEL_SET);
		gfx_mono_draw_pixel(4 + 2, 40 + 2, GFX_PIXEL_SET);
		gfx_mono_draw_pixel(4 + 3, 40 + 3, GFX_PIXEL_SET);
		gfx_mono_draw_pixel(4 + 2, 40 + 4, GFX_PIXEL_SET);
		gfx_mono_draw_pixel(4 + 1, 40 + 5, GFX_PIXEL_SET);
		gfx_mono_draw_pixel(4 + 2, 40 + 6, GFX_PIXEL_SET);
		gfx_mono_draw_pixel(4 + 3, 40 + 7, GFX_PIXEL_SET);
		gfx_mono_draw_pixel(4 + 4, 40 + 8, GFX_PIXEL_SET);
	}

	if (pattern_bm & (1 << 3)) {
		// TEST 4
		gfx_mono_generic_draw_line (0, 16, 239, 31, GFX_PIXEL_SET);
	}

	if (pattern_bm & (1 << 4)) {
		// TEST 5
		gfx_mono_generic_draw_rect(        70, 48, 40, 40, GFX_PIXEL_SET);
		gfx_mono_generic_draw_filled_rect(170, 48, 40, 40, GFX_PIXEL_SET);
	}

	if (pattern_bm & (1 << 5)) {
		// TEST 6
		gfx_mono_generic_draw_circle(       10, 80, 10, GFX_PIXEL_SET, GFX_WHOLE);
		gfx_mono_generic_draw_filled_circle(40, 80, 10, GFX_PIXEL_SET, GFX_WHOLE);
	}

	if (pattern_bm & (1 << 6)) {
		// TEST 7
		gfx_mono_draw_string("DF4IAH Smart-LCD", 70, 32, &sysfont);
	}

	if (pattern_bm & (1 << 7)) {
		// TEST 8
		lcd_animation_prepare();

		g_status.doAnimation = true;
		lcd_animation_loop();
	}
}


void lcd_10mhz_ref_osc_show_clk_state(uint8_t clk_state, int16_t phase100)
{
	irqflags_t flags = cpu_irq_save();
	cpu_irq_disable();
	if (g_showData.clkState_clk_state != clk_state ||
			g_showData.clkState_phase100 != phase100) {
		g_showData.newClkState = true;
		g_showData.clkState_clk_state = clk_state;
		g_showData.clkState_phase100 = phase100;
	}
	cpu_irq_restore(flags);
}

void lcd_10mhz_ref_osc_show_date(uint16_t year, int8_t month, uint8_t day)
{
	irqflags_t flags = cpu_irq_save();
	cpu_irq_disable();
	if (g_showData.date_year != year ||
			g_showData.date_month != month ||
			g_showData.date_day != day) {
		g_showData.newDate = true;
		g_showData.date_year = year;
		g_showData.date_month = month;
		g_showData.date_day = day;
	}
	cpu_irq_restore(flags);
}

void lcd_10mhz_ref_osc_show_time(uint8_t hour, int8_t minute, uint8_t second)
{
	irqflags_t flags = cpu_irq_save();
	cpu_irq_disable();
	if (g_showData.time_hour != hour ||
			g_showData.time_minute != minute ||
			g_showData.time_second != second) {
		g_showData.newTime = true;
		g_showData.time_hour = hour;
		g_showData.time_minute = minute;
		g_showData.time_second = second;
	}
	cpu_irq_restore(flags);
}

void lcd_10mhz_ref_osc_show_ppm(int16_t ppm_int, uint16_t ppm_frac1000)
{
	irqflags_t flags = cpu_irq_save();
	cpu_irq_disable();
	if (g_showData.ppm_int != ppm_int ||
			g_showData.ppm_frac1000 != ppm_frac1000) {
		g_showData.newPpm = true;
		g_showData.ppm_int = ppm_int;
		g_showData.ppm_frac1000 = ppm_frac1000;
	}
	cpu_irq_restore(flags);
}

void lcd_10mhz_ref_osc_show_pwm(uint8_t pwm_int, uint8_t pwm_frac1000)
{
	irqflags_t flags = cpu_irq_save();
	cpu_irq_disable();
	if (g_showData.pwm_int != pwm_int ||
			g_showData.pwm_frac1000 != pwm_frac1000) {
		g_showData.newPwm = true;
		g_showData.pwm_int = pwm_int;
		g_showData.pwm_frac1000 = pwm_frac1000;
	}
	cpu_irq_restore(flags);
}

void lcd_10mhz_ref_osc_show_pv(uint8_t pv_int, uint16_t pv_frac1000)
{
	irqflags_t flags = cpu_irq_save();
	cpu_irq_disable();
	if (g_showData.pv_int != pv_int ||
			g_showData.pv_frac1000 != pv_frac1000) {
		g_showData.newPv = true;
		g_showData.pv_int = pv_int;
		g_showData.pv_frac1000 = pv_frac1000;
	}
	cpu_irq_restore(flags);
}

void lcd_10mhz_ref_osc_show_sat_use(uint8_t sat_west, uint8_t sat_east, uint8_t sat_used)
{
	irqflags_t flags = cpu_irq_save();
	cpu_irq_disable();
	if (g_showData.satUse_west != sat_west ||
			g_showData.satUse_east != sat_east ||
			g_showData.satUse_used != sat_used) {
		g_showData.newSatUse = true;
		g_showData.satUse_west = sat_west;
		g_showData.satUse_east = sat_east;
		g_showData.satUse_used = sat_used;
	}
	cpu_irq_restore(flags);
}

void lcd_10mhz_ref_osc_show_sat_dop(uint16_t sat_dop100)
{
	irqflags_t flags = cpu_irq_save();
	cpu_irq_disable();
	if (g_showData.satDop_dop100 != sat_dop100) {
		g_showData.newSatDop = true;
		g_showData.satDop_dop100 = sat_dop100;
	}
	cpu_irq_restore(flags);
}

void lcd_10mhz_ref_osc_show_pos_state(uint8_t state_fi, uint8_t state_m2)
{
	irqflags_t flags = cpu_irq_save();
	cpu_irq_disable();
	if (g_showData.posState_fi != state_fi ||
			g_showData.posState_m2 != state_m2) {
		g_showData.newPosState = true;
		g_showData.posState_fi = state_fi;
		g_showData.posState_m2 = state_m2;
	}
	cpu_irq_restore(flags);
}

void lcd_10mhz_ref_osc_show_pos_lat(uint8_t lat_sgn, uint8_t lat_deg, uint8_t lat_min_int, uint16_t lat_min_frac10000)
{
	irqflags_t flags = cpu_irq_save();
	cpu_irq_disable();
	if (g_showData.posLat_sgn != lat_sgn ||
			g_showData.posLat_deg != lat_deg ||
			g_showData.posLat_min_int != lat_min_int ||
			g_showData.posLat_min_int != lat_min_int ||
			g_showData.posLat_min_frac10000 != lat_min_frac10000) {
		g_showData.newPosLat = true;
		g_showData.posLat_sgn = lat_sgn;
		g_showData.posLat_deg = lat_deg;
		g_showData.posLat_min_int = lat_min_int;
		g_showData.posLat_min_frac10000 = lat_min_frac10000;
	}
	cpu_irq_restore(flags);
}

void lcd_10mhz_ref_osc_show_pos_lon(uint8_t lon_sgn, uint8_t lon_deg, uint8_t lon_min_int, uint16_t lon_min_frac10000)
{
	irqflags_t flags = cpu_irq_save();
	cpu_irq_disable();
	if (g_showData.posLon_sgn != lon_sgn ||
			g_showData.posLon_deg != lon_deg ||
			g_showData.posLon_deg != lon_deg ||
			g_showData.posLon_min_int != lon_min_int ||
			g_showData.posLon_min_frac10000 != lon_min_frac10000) {
		g_showData.newPosLon = true;
		g_showData.posLon_sgn = lon_sgn;
		g_showData.posLon_deg = lon_deg;
		g_showData.posLon_min_int = lon_min_int;
		g_showData.posLon_min_frac10000 = lon_min_frac10000;
	}
	cpu_irq_restore(flags);
}

void lcd_10mhz_ref_osc_show_pos_height(int16_t height)
{
	irqflags_t flags = cpu_irq_save();
	cpu_irq_disable();
	if (g_showData.pos_heigth != height) {
		g_showData.newPosHeight = true;
		g_showData.pos_heigth = height;
	}
	cpu_irq_restore(flags);
}


const void* lcd_get_sysfont(void)
{
	return &sysfont;
}


uint8_t lcd_init(void)
{
	uint8_t data;

	/* INIT sequence */
	lcd_bus_write_cmd(0b11100010);									// Reset display
	delay_ms(20);													// Wait for the panel to get ready in case one is attached to the bus

	data = lcd_bus_read_status();									// Get current status
	if (!(data & C_LCD_STATUS_M)) {
		/* LCD panel reacts correctly - resume with INIT sequence */
		lcd_enable(true);
		lcd_cls();													// Clear screen
		return 0;													// Return OK

	} else {
		return 1;													// Return failure
	}
}

void lcd_shutdown(void)
{
	lcd_bus_write_cmd(0b11100010);									// Reset display
	delay_ms(2);													// Wait for the energy to dissipate
}
