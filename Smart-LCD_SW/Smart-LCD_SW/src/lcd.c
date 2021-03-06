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
#include <string.h>
#include <math.h>

#include "gfx_mono/sysfont.h"
#include "twi.h"
#include "main.h"

#include "lcd.h"

extern float				g_temp;
extern float				g_adc_light;
extern uint8_t				g_lcd_contrast_pm;
extern status_t				g_status;
extern showData_t			g_showData;
extern buttons_t			g_buttons;
extern uint8_t				g_SmartLCD_mode;
extern gfx_coord_t			g_lcd_pencil_x;
extern gfx_coord_t			g_lcd_pencil_y;
extern char					g_strbuf[48];


#define ANIMATION_TRAIN_WAGGON_CNT		4
#define ANIMATION_TRAIN_BLANK_LEN       (1 + 15 + ANIMATION_TRAIN_WAGGON_CNT * (3 + 8) + 1)

static uint8_t s_lcd_ram_read_nonvalid = 0;
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

void lcd_contrast_update(void)
{
		lcd_bus_write_cmd(0b10000001);								// Set Gain and PM (A)
		lcd_bus_write_cmd(C_LCD_GAIN_BM | (g_lcd_contrast_pm & 0x3F));	// Set Gain and PM (B)
}

void lcd_enable(uint8_t on)
{
	if (!on) {
		lcd_bus_write_cmd(0b10101110);								// Disable DC[2] (Display)

	} else {
		lcd_bus_write_cmd(0b00101000 | C_LCD_PWR_CTRL);				// Set Power Control
		lcd_bus_write_cmd(0b00100000 | C_LCD_MR_TC);				// Set MR and TC
		lcd_bus_write_cmd(0b11101000 | C_LCD_BIASRATIO);			// Set Bias Ratio
		lcd_contrast_update();

		lcd_bus_write_cmd(0b11000000 | C_LCD_MAPPING);				// Set Mapping
		lcd_bus_write_cmd(0b10001000 | C_LCD_AC);					// Set RAM Address Control

		lcd_bus_write_cmd(0b01000000);								// Set Start Line (0)
		lcd_bus_write_cmd(0b10010000);								// Set Fixed Lines (0)

		lcd_bus_write_cmd(0b10110000);								// Set Page Address (0)
		lcd_bus_write_cmd(0b00000000);								// Set Column Address LSB (0)
		lcd_bus_write_cmd(0b00010000);								// Set Column Address MSB (0)
		s_lcd_ram_read_nonvalid = true;

		lcd_bus_write_cmd(0b11101111);								// Set Cursor Mode
		lcd_bus_write_cmd(0b11101110);								// Reset Cursor Mode (now CR := CA)
		lcd_bus_write_cmd(0b10100100);								// Disable DC[1] (all pixel on)
		lcd_bus_write_cmd(0b10100110);								// Disable DC[0] (all pixel inverse)
		lcd_bus_write_cmd(0b10101111);								// Enable  DC[2] (Display)
	}
}

void lcd_page_set(uint8_t page)
{
	if ((0 <= page) && (page < GFX_MONO_LCD_PAGES)) {
		lcd_bus_write_cmd(0b10110000 | page);						// Set Page Address

		s_lcd_ram_read_nonvalid = 1;
	}
}

void lcd_col_set(uint8_t col)
{
	if ((0 <= col) && (col < GFX_MONO_LCD_WIDTH)) {
		lcd_bus_write_cmd(0b00000000 | ( col       & 0x0f));		// Set Column Address LSB
		lcd_bus_write_cmd(0b00010000 | ((col >> 4) & 0x0f));		// Set Column Address MSB

		s_lcd_ram_read_nonvalid = 1;
	}
}

void lcd_cr(void)
{
	lcd_bus_write_cmd(0b00000000);									// Set Column Address LSB (0)
	lcd_bus_write_cmd(0b00010000);									// Set Column Address MSB (0)

	s_lcd_ram_read_nonvalid = 1;
}

void lcd_home(void)
{
	lcd_bus_write_cmd(0b10110000);									// Set Page Address (0)
	lcd_cr();
}

void lcd_cls(void)
{
	/* Blank LCD RAM */
	for (uint8_t page = 0; page < GFX_MONO_LCD_PAGES; ++page) {
		lcd_bus_write_cmd(0b10110000 | page);						// Set Page Address
		lcd_bus_write_cmd(0b00000000);								// Set Column Address LSB (0)
		lcd_bus_write_cmd(0b00010000);								// Set Column Address MSB (0)

		for (uint8_t cnt = GFX_MONO_LCD_WIDTH; cnt; --cnt) {		// Clear all columns of that page
			lcd_bus_write_ram(0);
		}
	}

	/* Set cursor to home position */
	lcd_home();
}

void lcd_reset(void)
{
	/* Call lcd_init() which resets LCD and boots up again */
	lcd_init();
}

void lcd_write(const char *strbuf, gfx_coord_t pos_x, gfx_coord_t pos_y)
{
	if (!strbuf) {
		return;
	}

	uint8_t len = strnlen(strbuf, 255);
	if ((0 < len) && (len < 255)) {
		gfx_mono_draw_string(strbuf, pos_x, pos_y, &sysfont);
	}
}


void lcd_show_template(void)
{
	snprintf(g_strbuf, sizeof(g_strbuf), "ClkState: 0x");
	gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  0 * LCD_SHOW_LINE_HEIGHT, &sysfont);

	snprintf(g_strbuf, sizeof(g_strbuf), "Date    :");
	gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  1 * LCD_SHOW_LINE_HEIGHT, &sysfont);

	snprintf(g_strbuf, sizeof(g_strbuf), "Time    : 00:00.00 UTC");
	gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  2 * LCD_SHOW_LINE_HEIGHT, &sysfont);

	snprintf(g_strbuf, sizeof(g_strbuf), "Deviat'n: %04d.%03d ppb", 0, 0);
	gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  3 * LCD_SHOW_LINE_HEIGHT, &sysfont);

	snprintf(g_strbuf, sizeof(g_strbuf), "PWM     : %3d.%03d/256 =%3d.%03d%%", 0, 0, 0, 0);
	gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  4 * LCD_SHOW_LINE_HEIGHT, &sysfont);

	snprintf(g_strbuf, sizeof(g_strbuf), "PullVolt: %1d.%03d V", 0, 0);
	gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  5 * LCD_SHOW_LINE_HEIGHT, &sysfont);

	snprintf(g_strbuf, sizeof(g_strbuf), "PhaseVolt: %1d.%03d V", 0, 0);
	gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT + 20 * LCD_SHOW_CLMN_WIDTH,  LCD_SHOW_LINE_TOP +  5 * LCD_SHOW_LINE_HEIGHT, &sysfont);

	snprintf(g_strbuf, sizeof(g_strbuf), "SatUse  : West=%02d East=%02d Used=%02d sats", 0, 0, 0);
	gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  6 * LCD_SHOW_LINE_HEIGHT, &sysfont);

	snprintf(g_strbuf, sizeof(g_strbuf), "Sat DOP : %02d.%02d", 0, 0);
	gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  7 * LCD_SHOW_LINE_HEIGHT, &sysfont);

	snprintf(g_strbuf, sizeof(g_strbuf), "SatState: FI=%1d M2=%1d", 0, 0);
	gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  8 * LCD_SHOW_LINE_HEIGHT, &sysfont);

	snprintf(g_strbuf, sizeof(g_strbuf), "Sat Lat : %c  %02d%c%02d.%04d'", ' ', 0, 0x7e, 0, 0);
	gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP +  9 * LCD_SHOW_LINE_HEIGHT, &sysfont);

	snprintf(g_strbuf, sizeof(g_strbuf), "Sat Lon : %c %03d%c%02d.%04d'", ' ', 0, 0x7e, 0, 0);
	gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP + 10 * LCD_SHOW_LINE_HEIGHT, &sysfont);

	snprintf(g_strbuf, sizeof(g_strbuf), "Sat Hgt : %04d.%01d m", 0, 0);
	gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP + 11 * LCD_SHOW_LINE_HEIGHT, &sysfont);

	snprintf(g_strbuf, sizeof(g_strbuf), "Phase: %+04d%c", 0, 0x7e);
	gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT + 26 * LCD_SHOW_CLMN_WIDTH,  LCD_SHOW_LINE_TOP +  11 * LCD_SHOW_LINE_HEIGHT, &sysfont);

	gfx_mono_draw_string("PhaseOfs:", LCD_SHOW_LINE_LEFT,  LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT, &sysfont);
}

static void lcd_show_new_clk_state(uint8_t clk_state, uint16_t phaseVolt1000, int16_t phaseDeg100)
{
	const uint8_t maxdiff = 76;
	const uint8_t mid_x = 150;
	const int16_t maxPhasePossible = 18000;
	const int16_t maxPhaseToShow   =  4500;
	static uint8_t clk_state_old = 0;

	if (phaseDeg100 > maxPhasePossible) {
		phaseDeg100 = maxPhasePossible;
	} else if (phaseDeg100 < -maxPhasePossible) {
		phaseDeg100 = -maxPhasePossible;
	}

	int16_t phaseDegGraph100 = phaseDeg100;
	if (phaseDegGraph100 > maxPhaseToShow) {
		phaseDegGraph100 = maxPhasePossible;
	} else if (phaseDegGraph100 < -maxPhaseToShow) {
		phaseDegGraph100 = -maxPhaseToShow;
	}

	int diff = (int) ((float)maxdiff * (phaseDegGraph100 / (float)maxPhaseToShow));
	int ldiff = diff < 0 ?  diff : 0;
	int rdiff = diff > 0 ?  diff : 0;

	uint8_t ox = mid_x;
	int dx = diff;
	if (dx < 0) {
		ox += dx;
		dx = -dx;
	}

	snprintf(g_strbuf, sizeof(g_strbuf), "%1X", clk_state);
	gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT + 12 * LCD_SHOW_CLMN_WIDTH,  LCD_SHOW_LINE_TOP +  0 * LCD_SHOW_LINE_HEIGHT, &sysfont);

	snprintf(g_strbuf, sizeof(g_strbuf), "%1d.%03d", phaseVolt1000 / 1000, phaseVolt1000 % 1000);
	gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT + 31 * LCD_SHOW_CLMN_WIDTH,  LCD_SHOW_LINE_TOP +  5 * LCD_SHOW_LINE_HEIGHT, &sysfont);

	snprintf(g_strbuf, sizeof(g_strbuf), "%+04d", phaseDeg100 / 100);
	gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT + 33 * LCD_SHOW_CLMN_WIDTH,  LCD_SHOW_LINE_TOP + 11 * LCD_SHOW_LINE_HEIGHT, &sysfont);

	if ((clk_state_old != clk_state) || !dx) {
		gfx_mono_generic_draw_filled_rect(mid_x - maxdiff -4, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 1,  (maxdiff << 1) +8, 5, GFX_PIXEL_CLR);
	}

	if (dx) {
		switch (clk_state) {
			case 0xf:
				gfx_mono_generic_draw_filled_rect(mid_x - maxdiff -4, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 1,  maxdiff + ldiff +4, 5, GFX_PIXEL_CLR);
				if (diff < 0) {
					gfx_mono_generic_draw_line(ox - 4, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 3,
											   ox,     LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 1,
											   GFX_PIXEL_SET);
					gfx_mono_generic_draw_line(ox - 4, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 3,
											   ox,     LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 5,
											   GFX_PIXEL_SET);
				}
				gfx_mono_generic_draw_filled_rect(ox, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 1, dx, 5, GFX_PIXEL_SET);
				gfx_mono_generic_draw_filled_rect(mid_x + rdiff, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 1,  maxdiff - rdiff +4, 5, GFX_PIXEL_CLR);
				if (diff > 0) {
					gfx_mono_generic_draw_line(mid_x + rdiff + 3, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 3,
											   mid_x + rdiff - 1, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 1,
											   GFX_PIXEL_SET);
					gfx_mono_generic_draw_line(mid_x + rdiff + 3, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 3,
											   mid_x + rdiff - 1, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 5,
											   GFX_PIXEL_SET);
				}
			break;

			case 0x7:
				gfx_mono_generic_draw_filled_rect(mid_x - maxdiff -4, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 2,  maxdiff + ldiff +4, 3, GFX_PIXEL_CLR);
				if (diff < 0) {
					gfx_mono_generic_draw_line(ox - 4, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 3,
											   ox,     LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 2,
											   GFX_PIXEL_SET);
					gfx_mono_generic_draw_line(ox - 4, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 3,
											   ox,     LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 4,
											   GFX_PIXEL_SET);
				}
				gfx_mono_generic_draw_filled_rect(ox, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 2,  dx, 3, GFX_PIXEL_SET);
				gfx_mono_generic_draw_filled_rect(mid_x + rdiff, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 2,  maxdiff - rdiff +4, 3, GFX_PIXEL_CLR);
				if (diff > 0) {
					gfx_mono_generic_draw_line(mid_x + rdiff + 3, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 3,
											   mid_x + rdiff - 1, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 2,
											   GFX_PIXEL_SET);
					gfx_mono_generic_draw_line(mid_x + rdiff + 3, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 3,
											   mid_x + rdiff - 1, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 4,
											   GFX_PIXEL_SET);
				}
			break;

			case 0x3:
			case 0x2:
			case 0x1:
				gfx_mono_generic_draw_filled_rect(mid_x - maxdiff, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 3,  maxdiff << 1, 1, GFX_PIXEL_SET);
			break;
		}

	} else {
		gfx_mono_generic_draw_line( mid_x, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 1,
									mid_x, LCD_SHOW_LINE_TOP + 12 * LCD_SHOW_LINE_HEIGHT + 5,
									GFX_PIXEL_SET);
	}

	clk_state_old = clk_state;
}

uint8_t lcd_show_new_smartlcd_data(void)
{
	uint8_t len;
	gfx_coord_t l_pencil_x, l_pencil_y, l_to_x, l_to_y, l_width, l_height, l_radius;
	gfx_mono_color_t l_pixelType;
	uint8_t i;
	char buf[8];

	irqflags_t flags = cpu_irq_save();

	switch (g_showData.cmd) {
		case TWI_SMART_LCD_CMD_RESET:
			gfx_mono_lcd_uc1608_cache_clear();
			lcd_reset();
			g_showData.cmd = 0;
			cpu_irq_restore(flags);
			return TWI_SMART_LCD_CMD_CLS;
		break;

		case TWI_SMART_LCD_CMD_CLS:
			gfx_mono_lcd_uc1608_cache_clear();
			lcd_cls();
			g_showData.cmd = 0;
			cpu_irq_restore(flags);
			return TWI_SMART_LCD_CMD_CLS;
		break;

		case TWI_SMART_LCD_CMD_SET_PIXEL_TYPE:
			g_showData.pixelType = (gfx_mono_color_t) g_showData.data[0];
			g_showData.cmd = 0;
			cpu_irq_restore(flags);
			return TWI_SMART_LCD_CMD_SET_PIXEL_TYPE;
		break;

		case TWI_SMART_LCD_CMD_SET_POS_X_Y:
			g_showData.pencil_x = (gfx_coord_t) g_showData.data[0];
			g_showData.pencil_y = (gfx_coord_t) g_showData.data[1];
			g_showData.cmd = 0;
			cpu_irq_restore(flags);
			return TWI_SMART_LCD_CMD_SET_POS_X_Y;
		break;

		case TWI_SMART_LCD_CMD_WRITE:
			len = g_showData.data[0];
			for (i = 0; i < len; ++i) {
				buf[i] = g_showData.data[1+i];
			}
			buf[len] = 0;
			l_pencil_x = g_showData.pencil_x;
			l_pencil_y = g_showData.pencil_y;
			lcd_write(buf, l_pencil_x, l_pencil_y);
			gfx_mono_lcd_uc1608_cache_clear();
			g_showData.cmd = 0;
			cpu_irq_restore(flags);
			return TWI_SMART_LCD_CMD_WRITE;
		break;

		case TWI_SMART_LCD_CMD_DRAW_LINE:							// Draw line from current pencil position to next position (x, y)
			l_pencil_x = g_showData.pencil_x;
			l_pencil_y = g_showData.pencil_y;
			l_to_x = g_showData.data[0];
			l_to_y = g_showData.data[1];
			l_pixelType = g_showData.data[2];
			if (l_pixelType == 255) {
				l_pixelType = g_showData.pixelType;
			}
			gfx_mono_generic_draw_line(l_pencil_x, l_pencil_y, l_to_x, l_to_y, l_pixelType);
			gfx_mono_lcd_uc1608_cache_clear();
			g_showData.pencil_x = l_to_x;
			g_showData.pencil_y = l_to_y;
			g_showData.cmd = 0;
			cpu_irq_restore(flags);
			return TWI_SMART_LCD_CMD_DRAW_LINE;
		break;

		case TWI_SMART_LCD_CMD_DRAW_RECT:							// Draw rectangular frame with pencil's start position with dimension (width, height)
			l_pencil_x = g_showData.pencil_x;
			l_pencil_y = g_showData.pencil_y;
			l_width = g_showData.data[0];
			l_height = g_showData.data[1];
			l_pixelType = g_showData.data[2];
			if (l_pixelType == 255) {
				l_pixelType = g_showData.pixelType;
			}
			gfx_mono_generic_draw_rect(l_pencil_x, l_pencil_y, l_width, l_height, l_pixelType);
			gfx_mono_lcd_uc1608_cache_clear();
			g_showData.cmd = 0;
			cpu_irq_restore(flags);
			return TWI_SMART_LCD_CMD_DRAW_RECT;
		break;

		case TWI_SMART_LCD_CMD_DRAW_FILLED_RECT:					// Draw filled rectangular frame with pencil's start position with dimension (width, height)
			l_pencil_x = g_showData.pencil_x;
			l_pencil_y = g_showData.pencil_y;
			l_width = g_showData.data[0];
			l_height = g_showData.data[1];
			l_pixelType = g_showData.data[2];
			if (l_pixelType == 255) {
				l_pixelType = g_showData.pixelType;
			}
			gfx_mono_generic_draw_filled_rect(l_pencil_x, l_pencil_y, l_width, l_height, l_pixelType);
			gfx_mono_lcd_uc1608_cache_clear();
			g_showData.cmd = 0;
			cpu_irq_restore(flags);
			return TWI_SMART_LCD_CMD_DRAW_FILLED_RECT;
		break;

		case TWI_SMART_LCD_CMD_DRAW_CIRC:							// Draw circle or ellipse from the pencil's center point with (radius)
			l_pencil_x = g_showData.pencil_x;
			l_pencil_y = g_showData.pencil_y;
			l_radius = g_showData.data[0];
			l_pixelType = g_showData.data[1];
			if (l_pixelType == 255) {
				l_pixelType = g_showData.pixelType;
			}
			gfx_mono_generic_draw_circle(l_pencil_x, l_pencil_y, l_radius, l_pixelType, GFX_QUADRANT0 | GFX_QUADRANT1 | GFX_QUADRANT2 | GFX_QUADRANT3);
			gfx_mono_lcd_uc1608_cache_clear();
			g_showData.cmd = 0;
			cpu_irq_restore(flags);
			return TWI_SMART_LCD_CMD_DRAW_CIRC;
		break;

		case TWI_SMART_LCD_CMD_DRAW_FILLED_CIRC:					// Draw filled circle or ellipse from the pencil's center point with (radius)
			l_pencil_x = g_showData.pencil_x;
			l_pencil_y = g_showData.pencil_y;
			l_radius = g_showData.data[0];
			l_pixelType = g_showData.data[1];
			if (l_pixelType == 255) {
				l_pixelType = g_showData.pixelType;
			}
			gfx_mono_generic_draw_filled_circle(l_pencil_x, l_pencil_y, l_radius, l_pixelType, GFX_QUADRANT0 | GFX_QUADRANT1 | GFX_QUADRANT2 | GFX_QUADRANT3);
			gfx_mono_lcd_uc1608_cache_clear();
			g_showData.cmd = 0;
			cpu_irq_restore(flags);
			return TWI_SMART_LCD_CMD_DRAW_FILLED_CIRC;
		break;
	}

	cpu_irq_restore(flags);
	return 0;
}

uint8_t lcd_show_new_refosc_data(void)
{
	static uint8_t idx = 1;

	/* First entries are showed first, when modified */

	irqflags_t flags = cpu_irq_save();

	/* Always */
	if (g_showData.newTime) {
		g_showData.newTime = false;
		snprintf(g_strbuf, sizeof(g_strbuf), "%02d:%02d.%02d",
		g_showData.time_hour, g_showData.time_minute, g_showData.time_second);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT + 10 * LCD_SHOW_CLMN_WIDTH,  LCD_SHOW_LINE_TOP +  2 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		gfx_mono_lcd_uc1608_cache_clear();
		return 255;
	}

	/* Always */
	if (g_showData.newDate) {
		g_showData.newDate = false;
		snprintf(g_strbuf, sizeof(g_strbuf), "%02d.%02d.%04d",
		g_showData.date_day, g_showData.date_month, g_showData.date_year);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT + 10 * LCD_SHOW_CLMN_WIDTH,  LCD_SHOW_LINE_TOP +  1 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		gfx_mono_lcd_uc1608_cache_clear();
		return 254;
	}

	/* Always */
	if (g_showData.newClkState) {
		g_showData.newClkState = false;
		cpu_irq_restore(flags);
		lcd_show_new_clk_state(g_showData.clkState_clk_state, g_showData.clkState_phaseVolt1000, g_showData.clkState_phaseDeg100);
		gfx_mono_lcd_uc1608_cache_clear();
		return 253;
	}


	/* Slot 1 */
	if (g_showData.newSatUse && (idx <= 1)) {
		g_showData.newSatUse = false;
		snprintf(g_strbuf, sizeof(g_strbuf), "%02d East=%02d Used=%02d",
		g_showData.satUse_west, g_showData.satUse_east, g_showData.satUse_used);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT + 15 * LCD_SHOW_CLMN_WIDTH,  LCD_SHOW_LINE_TOP +  6 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		gfx_mono_lcd_uc1608_cache_clear();
		idx = 2;
		return 1;
	}

	/* Slot 2 */
	if (g_showData.newSatDop && (idx <= 2)) {
		g_showData.newSatDop = false;
		snprintf(g_strbuf, sizeof(g_strbuf), "%02d.%02d",
		(int) (g_showData.satDop_dop100 / 100.0f), g_showData.satDop_dop100 % 100);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT + 10 * LCD_SHOW_CLMN_WIDTH,  LCD_SHOW_LINE_TOP +  7 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		gfx_mono_lcd_uc1608_cache_clear();
		idx = 3;
		return 2;
	}

	/* Slot 3 */
	if (g_showData.newPosState && (idx <= 3)) {
		g_showData.newPosState = false;
		snprintf(g_strbuf, sizeof(g_strbuf), "%1d M2=%1d",
		g_showData.posState_fi, g_showData.posState_m2);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT + 13 * LCD_SHOW_CLMN_WIDTH,  LCD_SHOW_LINE_TOP +  8 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		gfx_mono_lcd_uc1608_cache_clear();
		idx = 4;
		return 3;
	}

	/* Slot 4 */
	if (g_showData.newPosLat && (idx <= 4)) {
		g_showData.newPosLat = false;
		snprintf(g_strbuf, sizeof(g_strbuf), "%c  %02d%c%02d.%04d",
		g_showData.posLat_sgn, g_showData.posLat_deg, 0x7e, g_showData.posLat_min_int, g_showData.posLat_min_frac10000);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT + 10 * LCD_SHOW_CLMN_WIDTH,  LCD_SHOW_LINE_TOP +  9 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		gfx_mono_lcd_uc1608_cache_clear();
		idx = 5;
		return 4;
	}

	/* Slot 5 */
	if (g_showData.newPosLon && (idx <= 5)) {
		g_showData.newPosLon = false;
		snprintf(g_strbuf, sizeof(g_strbuf), "%c %03d%c%02d.%04d",
		g_showData.posLon_sgn, g_showData.posLon_deg, 0x7e, g_showData.posLon_min_int, g_showData.posLon_min_frac10000);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT + 10 * LCD_SHOW_CLMN_WIDTH,  LCD_SHOW_LINE_TOP + 10 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		gfx_mono_lcd_uc1608_cache_clear();
		idx = 6;
		return 5;
	}

	/* Slot 6 */
	if (g_showData.newPosHeight && (idx <= 6)) {
		g_showData.newPosHeight = false;
		snprintf(g_strbuf, sizeof(g_strbuf), "%04d.%1d",
		g_showData.pos_height_int, g_showData.pos_height_frac10);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT + 10 * LCD_SHOW_CLMN_WIDTH,  LCD_SHOW_LINE_TOP + 11 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		gfx_mono_lcd_uc1608_cache_clear();
		idx = 7;
		return 6;
	}

	/* Slot 7 */
	if (g_showData.newPpb && (idx <= 7)) {
		g_showData.newPpb = false;
		snprintf(g_strbuf, sizeof(g_strbuf), "%04d.%03d",
		g_showData.ppb_int, g_showData.ppb_frac1000);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT + 10 * LCD_SHOW_CLMN_WIDTH,  LCD_SHOW_LINE_TOP +  3 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		gfx_mono_lcd_uc1608_cache_clear();
		idx = 8;
		return 7;
	}

	/* Slot 8 */
	if (g_showData.newPwm && (idx <= 8)) {
		const float exp_256_to_1000 = 1000.0f / 256.0f;
		uint8_t pwm_int = g_showData.pwm_int;
		uint8_t pwm_frac256 = g_showData.pwm_frac256;
		g_showData.newPwm = false;
		cpu_irq_restore(flags);

		snprintf(g_strbuf, sizeof(g_strbuf), "%3d.%03d",
		pwm_int, (int) (pwm_frac256 * exp_256_to_1000));
		gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT + 10 * LCD_SHOW_CLMN_WIDTH,  LCD_SHOW_LINE_TOP +  4 * LCD_SHOW_LINE_HEIGHT, &sysfont);

		float f_pwm = pwm_int;
		f_pwm += pwm_frac256 / 256.0f;
		f_pwm *= 100.0f / 256.0f;
		snprintf(g_strbuf, sizeof(g_strbuf), "%3d.%03d",
		(int) f_pwm, (int) ((f_pwm - floorf(f_pwm)) * 1000.0f));
		gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT + 23 * LCD_SHOW_CLMN_WIDTH,  LCD_SHOW_LINE_TOP +  4 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		gfx_mono_lcd_uc1608_cache_clear();
		idx = 9;
		return 8;
	}

	/* Slot 9 */
	if (g_showData.newPv && (idx <= 9)) {
		g_showData.newPv = false;
		snprintf(g_strbuf, sizeof(g_strbuf), "%1d.%03d",
		g_showData.pv_int, g_showData.pv_frac1000);
		cpu_irq_restore(flags);
		gfx_mono_draw_string(g_strbuf, LCD_SHOW_LINE_LEFT + 10 * LCD_SHOW_CLMN_WIDTH,  LCD_SHOW_LINE_TOP +  5 * LCD_SHOW_LINE_HEIGHT, &sysfont);
		gfx_mono_lcd_uc1608_cache_clear();
		idx = 10;
		return 9;
	}

	/* When no updates are due, turn back to first position */
	idx = 1;

	cpu_irq_restore(flags);
	return 0;
}


static void s_lcd_test_lines(void)
{
	const int oy = 10;
	const int h = 18;
	const int w = GFX_MONO_LCD_WIDTH;

	static int loop = 0;
	static uint8_t sw = 2;

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
	float t;

	irqflags_t flags = cpu_irq_save();
	t = g_temp;
	cpu_irq_restore(flags);

	if (t < 0.f) {
		t = 0.f;
	}

	if (fabsf(t - t_last) < 0.01f) {
		return;
	}
	t_last = t;

	snprintf(g_strbuf, sizeof(g_strbuf), " %2d.%02d ", (int) t, ((int) (t * 100.0f)) % 100);
	gfx_mono_draw_string(g_strbuf, 70 + 8 *6, 58 +  2 *10, &sysfont);
}

static void s_lcd_test_light(void)
{
	static float l_last = 0.0f;
	float l;

	irqflags_t flags = cpu_irq_save();
	l = g_adc_light;
	cpu_irq_restore(flags);

	if (fabsf(l - l_last) < 0.1f) {
		return;
	}
	l_last = l;

	snprintf(g_strbuf, sizeof(g_strbuf), " %04d.%1d ", (int) l, ((int) (l * 10.0f)) % 10);
	gfx_mono_draw_string(g_strbuf, 70 + 8 *6, 58 +  3 *10, &sysfont);
}

static void s_lcd_test_buttons(void)
{
	static int32_t cntr_old = 0;
	static uint8_t push_old = 0;

	irqflags_t flags = cpu_irq_save();
	buttons_t l_buttons = g_buttons;
	cpu_irq_restore(flags);

	int32_t cntr_now = l_buttons.counter << 2 | l_buttons.rotEnd_quad;
	int32_t delta    = cntr_now - cntr_old;
	if (delta || (l_buttons.pushBut != push_old)) {
		cntr_old = cntr_now;
		push_old = l_buttons.pushBut;

		char b_button   = l_buttons.pushBut  ?  'B' : '.';
		char b_rotEnc_I = l_buttons.rotEnc_I ?  'I' : '.';
		char b_rotEnc_Q = l_buttons.rotEnc_Q ?  'Q' : '.';
		char up_down    = delta > 0 ?  '^' : delta < 0 ?  'v' : '-';

		snprintf(g_strbuf, sizeof(g_strbuf), " %c %c %c %+06ld:%d %c", b_button, b_rotEnc_I, b_rotEnc_Q, l_buttons.counter, l_buttons.rotEnd_quad, up_down);
		gfx_mono_draw_string(g_strbuf, 70 + 8 *6, 58 +  4 *10, &sysfont);
	}
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
		s_animation_train_right[ANIMATION_TRAIN_BLANK_LEN - idx - 1] = s_animation_train_left[idx];	// x-mirror
	}

	/* prepare free line for train */
	gfx_mono_generic_draw_filled_rect(0, (GFX_MONO_LCD_PAGES - 1) * GFX_MONO_LCD_PIXELS_PER_BYTE, GFX_MONO_LCD_WIDTH, GFX_MONO_LCD_PIXELS_PER_BYTE, GFX_PIXEL_CLR);
}

void lcd_animation_loop(void)
{
	irqflags_t flags;
	uint8_t l_doAnimation;

	flags = cpu_irq_save();
	l_doAnimation = g_status.doAnimation;							// TWI command TWI_SMART_LCD_CMD_SET_MODE can unset this flag
	cpu_irq_restore(flags);

	while (l_doAnimation && s_animation_dx) {
		float now = get_abs_time();

		if ((now - s_animation_time_last_train) >= 0.04f) {			// 25x per sec
			s_animation_time_last_train = now;
			s_animation_train_origin += s_animation_dx;

			if (s_animation_train_origin <= (-10 - ANIMATION_TRAIN_BLANK_LEN)) {
				s_animation_dx = 1;
			} else if (s_animation_train_origin >= (GFX_MONO_LCD_WIDTH + 10)) {
				s_animation_dx = -1;
			}

			if (s_animation_dx < 0) {
				/* Draw train left */
				if (s_animation_train_origin >= 0 && s_animation_train_origin < GFX_MONO_LCD_WIDTH) {
					gfx_mono_lcd_uc1608_put_page(s_animation_train_left, GFX_MONO_LCD_PAGES - 1, s_animation_train_origin, ANIMATION_TRAIN_BLANK_LEN);	// full width
				} else if (-ANIMATION_TRAIN_BLANK_LEN < s_animation_train_origin && s_animation_train_origin < 0) {
					gfx_mono_lcd_uc1608_put_page(s_animation_train_left - s_animation_train_origin, GFX_MONO_LCD_PAGES - 1, 0, ANIMATION_TRAIN_BLANK_LEN + s_animation_train_origin);	// left: reduced width
				}

				} else {
				/* Draw train right */
				if (s_animation_train_origin >= 0 && s_animation_train_origin < GFX_MONO_LCD_WIDTH) {
					gfx_mono_lcd_uc1608_put_page(s_animation_train_right, GFX_MONO_LCD_PAGES - 1, s_animation_train_origin, ANIMATION_TRAIN_BLANK_LEN);	// full width
				} else if (-ANIMATION_TRAIN_BLANK_LEN < s_animation_train_origin && s_animation_train_origin < 0) {
					gfx_mono_lcd_uc1608_put_page(s_animation_train_right - s_animation_train_origin, GFX_MONO_LCD_PAGES - 1, 0, ANIMATION_TRAIN_BLANK_LEN + s_animation_train_origin);	// left: reduced width
				}
			}
		}

		task(now);													// calculate backlight PWM and temperature value
		if ((now - s_animation_time_last_temp) >= 0.50f) {			// 2x per sec
			s_animation_time_last_temp = now;
			s_lcd_test_temp();
			s_lcd_test_light();
			s_lcd_test_buttons();
		}

		s_lcd_test_lines();											// Every cycle

		flags = cpu_irq_save();
		l_doAnimation = g_status.doAnimation;						// TWI command TWI_SMART_LCD_CMD_SET_MODE can unset this flag
		cpu_irq_restore(flags);
	}

	flags = cpu_irq_save();
	g_status.doAnimation = false;
	cpu_irq_restore(flags);
}

void lcd_test(uint8_t pattern_bm)
{
	irqflags_t flags;

	flags = cpu_irq_save();
	g_status.doAnimation = false;
	g_status.isAnimationStopped = false;
	cpu_irq_restore(flags);

	if (pattern_bm & (1 << 0)) {
		// TEST 1
		lcd_page_set(0);
		lcd_cr();
		for (int i = 0; i < GFX_MONO_LCD_WIDTH; ++i) {
			lcd_bus_write_ram(i);
		}
	}

	if (pattern_bm & (1 << 1)) {
		// TEST 2
		lcd_page_set(1);
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
		gfx_mono_draw_pixel(25, 60 +  0, GFX_PIXEL_SET);
		gfx_mono_draw_pixel( 0, 60 + 23, GFX_PIXEL_SET);
		gfx_mono_draw_pixel(50, 60 + 23, GFX_PIXEL_SET);
		gfx_mono_draw_pixel(25, 60 + 45, GFX_PIXEL_SET);
	}

	if (pattern_bm & (1 << 3)) {
		// TEST 4
		gfx_mono_generic_draw_line (        10, 60 + 10, 40, 60 + 34, GFX_PIXEL_SET);
		gfx_mono_generic_draw_line (        10, 60 + 34, 40, 60 + 10, GFX_PIXEL_SET);
	}

	if (pattern_bm & (1 << 4)) {
		// TEST 5
		gfx_mono_generic_draw_rect(          0, 60 +  0, 20, 20, GFX_PIXEL_SET);
		gfx_mono_generic_draw_filled_rect(  30, 60 +  0, 20, 20, GFX_PIXEL_SET);
	}

	if (pattern_bm & (1 << 5)) {
		// TEST 6
		gfx_mono_generic_draw_filled_circle(10, 60 + 34, 10, GFX_PIXEL_SET, GFX_WHOLE);
		gfx_mono_generic_draw_circle(       40, 60 + 34, 10, GFX_PIXEL_SET, GFX_WHOLE);
	}

	if (pattern_bm & (1 << 6)) {
		// TEST 7
		gfx_mono_draw_string("DF4IAH Smart-LCD",			70, 37, &sysfont);

		snprintf(g_strbuf, sizeof(g_strbuf), "Version: 20%3d%3d", VERSION_HIGH, VERSION_LOW);
		gfx_mono_draw_string(g_strbuf,						70, 58 +  0 *10, &sysfont);

		snprintf(g_strbuf, sizeof(g_strbuf), "I2C-Ver: 0x%02x", I2C_VERSION);
		gfx_mono_draw_string(g_strbuf,						70, 58 +  1 *10, &sysfont);

		gfx_mono_draw_string("Temp.  : xx.xx  'C",			70, 58 +  2 *10, &sysfont);
		gfx_mono_draw_string("Light  : xxxx.x AD",			70, 58 +  3 *10, &sysfont);
		gfx_mono_draw_string("Buttons: . . .  00000:0 -",	70, 58 +  4 *10, &sysfont);
	}

	if (pattern_bm & (1 << 7)) {
		// TEST 8
		lcd_animation_prepare();

		flags = cpu_irq_save();
		g_status.doAnimation = true;
		cpu_irq_restore(flags);

		lcd_animation_loop();
	}
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
