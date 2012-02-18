/**
 ******************************************************************************
 * @addtogroup OpenPilotModules OpenPilot Modules
 * @{
 * @addtogroup OSDGENModule osdgen Module
 * @brief Process OSD information
 * @{
 *
 * @file       osdgen.c
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
 * @brief      OSD gen module, handles OSD draw
 * @see        The GNU Public License (GPL) Version 3
 *
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

// ****************

#include "openpilot.h"
#include "osdgen.h"
#include "attitudeactual.h"
#include "gpsposition.h"
#include "homelocation.h"
#include "gpstime.h"
#include "gpssatellites.h"
#include "osdsettings.h"

#include "fonts.h"
#include "font12x18.h"
#include "WMMInternal.h"

static uint16_t angleA=0;
static int16_t angleB=90;
static int16_t angleC=0;
static int16_t sum=2;

static int16_t m_pitch=0;
static int16_t m_roll=0;
static int16_t m_yaw=0;
static int16_t m_batt=0;
static int16_t m_alt=0;

static uint8_t m_gpsStatus=0;
static int32_t m_gpsLat=0;
static int32_t m_gpsLon=0;
static float m_gpsAlt=0;
static float m_gpsSpd=0;

extern uint16_t *draw_buffer_level;
extern uint16_t *draw_buffer_mask;
extern uint16_t *disp_buffer_level;
extern uint16_t *disp_buffer_mask;


TTime time;

// ****************
// Private functions

static void osdgenTask(void *parameters);

// ****************
// Private constants
#define LONG_TIME 0xffff
xSemaphoreHandle osdSemaphore = NULL;

#define STACK_SIZE_BYTES            1096

#define TASK_PRIORITY               (tskIDLE_PRIORITY + 4)
#define UPDATE_PERIOD 100

// ****************
// Private variables

static xTaskHandle osdgenTaskHandle;




// simple routines
/*
uint8_t getCharData(uint16_t charPos) {
	if (charPos >= CHAR_ARRAY_OFFSET && charPos < CHAR_ARRAY_MAX) {
	  return (oem6x8[charPos - CHAR_ARRAY_OFFSET]);
	}
	else {
		return 0x00;
	}
}*/

// prints text into draw_buffer_level, 8x2
/*uint8_t printTextFB(uint16_t x, uint16_t y, const char* str) {
	uint8_t length = strlen(str);
	if (x + length >= TEXT_LINE_MAX_CHARS) {
		length = TEXT_LINE_MAX_CHARS;
	}
	for(uint8_t i = 0; i < TEXT_CHAR_HEIGHT; i++)
	{
		uint8_t c=0;
		uint16_t charPos;
		for(int j=0; j<length; j+=2)
		{
			uint16_t word=0;
			charPos = str[j] * TEXT_CHAR_HEIGHT + i;
			word = getCharData(charPos)<<8;
			charPos = str[j+1] * TEXT_CHAR_HEIGHT + i;
			word |= getCharData(charPos);
			draw_buffer_level[((y+i)*GRAPHICS_WIDTH)+(x + c)] = word;
			c++;
		}
	}
	return length;
}*/

uint8_t printText16(uint16_t x, uint16_t y, const char* str) {

	uint8_t length = strlen(str);

	for(int j=0; j<length; j++)
	{
		write_char16(str[j],12*j+x,y);
	}

	return length;
}


void printTime(uint16_t x, uint16_t y) {
	char temp[9]={0};
	sprintf(temp,"%02d:%02d:%02d",time.hour,time.min,time.sec);
	//printTextFB(x,y,temp);
	write_string(temp, x, y, 1, 0, TEXT_VA_TOP, TEXT_HA_LEFT, 0, 0);
}

uint8_t printCharFB(uint16_t ch, uint16_t x, uint16_t y) {
	for(uint8_t i = 0; i < 18; i++)
	{
		uint8_t c=0;
			draw_buffer_level[((y+i)*GRAPHICS_WIDTH)+(x+c)] = font_frame16x18[ch*18+i];
			draw_buffer_mask[((y+i)*GRAPHICS_WIDTH)+(x+c)] = font_mask16x18[ch*18+i];
	}
	return 1;
}

void write_char16(char ch, unsigned int x, unsigned int y)
{
    int yy, addr_temp, row, row_temp, xshift;
    uint16_t and_mask, or_mask, level_bits;
    char lookup = 0;
    // Compute starting address (for x,y) of character.
    int addr = CALC_BUFF_ADDR(x, y);
    int wbit = CALC_BIT_IN_WORD(x);
    // If font only supports lowercase or uppercase, make the letter
    // lowercase or uppercase.
    // How big is the character? We handle characters up to 8 pixels
    // wide for now. Support for large characters may be added in future.
    {
            // Ensure we don't overflow.
            if(x + wbit > DISP_WIDTH)
                    return;
            // Load data pointer.
            row = ch * 18;
            row_temp = row;
            addr_temp = addr;
            xshift = 16 - 16;
            // We can write mask words easily.
            for(yy = y; yy < y + 18; yy++)
            {
                    write_word_misaligned_OR(draw_buffer_mask, font_mask16x18[row] << xshift, addr, wbit);
                    addr += DISP_WIDTH / 16;
                    row++;
            }
            // Level bits are more complicated. We need to set or clear
            // level bits, but only where the mask bit is set; otherwise,
            // we need to leave them alone. To do this, for each word, we
            // construct an AND mask and an OR mask, and apply each individually.
            row = row_temp;
            addr = addr_temp;
            for(yy = y; yy < y + 18; yy++)
            {
                    level_bits = font_frame16x18[row];
                    //if(!(flags & FONT_INVERT)) // data is normally inverted
                            level_bits = ~level_bits;
                    or_mask = font_mask16x18[row] << xshift;
                    and_mask = (font_mask16x18[row] & level_bits) << xshift;
                    write_word_misaligned_OR(draw_buffer_level, or_mask, addr, wbit);
                    // If we're not bold write the AND mask.
                    //if(!(flags & FONT_BOLD))
                            write_word_misaligned_NAND(draw_buffer_level, and_mask, addr, wbit);
                    addr += DISP_WIDTH / 16;
                    row++;
            }
    }
}



static unsigned short logo_bits[] = {
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0700,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x3fc0, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xfff0, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xfff0, 0x0001, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0xfff0, 0x0007, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0xfff8, 0x000f, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0xfff8, 0x001f, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0xfff8, 0x007f, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0xfffc, 0x00ff, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xfffc,
   0x01ff, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xfffc, 0x07ff,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xfffe, 0x07ff, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xfffe, 0x1fff, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0xfffe, 0x3fff, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0xfffe, 0xffff, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0xffff, 0xffff, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0xffff, 0xffff, 0x0003, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0xffff, 0xfff0, 0x000f, 0x0000, 0x0000, 0x0000, 0x0000, 0x8000, 0x7fff,
   0xffc0, 0x001f, 0x0000, 0x0000, 0x0000, 0x0000, 0x8000, 0x3fff, 0xff80,
   0x003f, 0x0000, 0x0000, 0x0000, 0x0000, 0x8000, 0x3fff, 0xfe00, 0x00ff,
   0x0000, 0x0000, 0x0000, 0x0000, 0x8000, 0x3fff, 0xf800, 0x00ff, 0x0000,
   0x0000, 0x0000, 0x0003, 0xc000, 0x3fff, 0xf000, 0x03ff, 0x0000, 0x0008,
   0xc000, 0x001f, 0xc000, 0x1fff, 0xe000, 0x07ff, 0x0000, 0x00f8, 0xf800,
   0x003f, 0xc000, 0x1fff, 0x8000, 0x0fff, 0x0000, 0x07e0, 0xfe00, 0x00ff,
   0xe000, 0x1fff, 0x0000, 0x3fff, 0x0000, 0xbfc0, 0xffc0, 0x00ff, 0xe000,
   0x1fff, 0x0000, 0x7ffc, 0x0000, 0xff00, 0xfff7, 0x007f, 0xe000, 0x0fff,
   0x0000, 0xfff0, 0x0000, 0xfe00, 0xffff, 0x007f, 0xf000, 0x0fff, 0x0000,
   0xffe0, 0x0001, 0xf800, 0xffff, 0xc07f, 0xf000, 0x0fff, 0x0000, 0xffc0,
   0x0001, 0xf000, 0xffff, 0xc07f, 0xf003, 0x0fff, 0x0000, 0xff00, 0x0003,
   0xc000, 0xffff, 0xe03f, 0xf00f, 0x0fff, 0x0000, 0xfe00, 0x0007, 0x8000,
   0xffff, 0xe03f, 0xf83f, 0x07ff, 0x0000, 0xf800, 0x0007, 0x0000, 0xffff,
   0xe03f, 0xf87f, 0x07ff, 0x0000, 0xf000, 0x000f, 0x0000, 0xfffc, 0xf03f,
   0xe1ff, 0x03ff, 0x0000, 0xf000, 0x000f, 0x0000, 0xfffc, 0xf07f, 0x87ff,
   0x03ff, 0x0000, 0xf000, 0x0007, 0x0000, 0xfffc, 0xf0ff, 0x1fff, 0x03ff,
   0x0000, 0xe000, 0x000f, 0x0000, 0xfff8, 0xf0ff, 0x3fff, 0x03f8, 0x0000,
   0xe000, 0x001f, 0x0000, 0xfff0, 0xf07f, 0xffff, 0x01e0, 0x0000, 0xe000,
   0x000f, 0x0000, 0xfff0, 0xf87f, 0xffff, 0x03c7, 0x0000, 0xe000, 0x000f,
   0x0000, 0xfff0, 0xf87f, 0xffff, 0x010f, 0x0000, 0xe000, 0x001f, 0x0000,
   0xffe0, 0xf87f, 0xffff, 0x003f, 0x0000, 0xe000, 0x000f, 0x0000, 0xffe0,
   0xfc7f, 0xffff, 0x00ff, 0x0000, 0xe000, 0x000f, 0x0000, 0xffc0, 0xfc0f,
   0xffff, 0x01ff, 0x0000, 0xf000, 0x000f, 0x0000, 0xffc0, 0xfc07, 0xffff,
   0x0fff, 0x0000, 0xf000, 0x001f, 0x0000, 0xffc0, 0xfc07, 0xffff, 0x3fff,
   0x0000, 0xf000, 0x000f, 0x0000, 0xff80, 0xfe07, 0xffff, 0xffff, 0x0000,
   0xf000, 0x000f, 0x0000, 0xff80, 0xfc03, 0xffff, 0xffff, 0x0001, 0xf000,
   0x000f, 0x0000, 0xff00, 0xfe03, 0xffff, 0xffff, 0x0007, 0xf000, 0x001f,
   0x0000, 0xff00, 0xfe03, 0xffff, 0xffff, 0x001f, 0xf000, 0x000f, 0x0000,
   0xfc00, 0xfe01, 0xffff, 0xffff, 0x007f, 0xf000, 0x000f, 0x0000, 0xf000,
   0xff03, 0xffff, 0xffff, 0x01ff, 0xf000, 0x000f, 0x0000, 0x8000, 0xff00,
   0xffff, 0xffff, 0x03ff, 0xf000, 0x001f, 0x0000, 0x0000, 0xff00, 0xffff,
   0xffff, 0x0fff, 0xf000, 0x000f, 0x0000, 0x0000, 0xff80, 0xffff, 0xffff,
   0x3fff, 0xf000, 0x000f, 0x0000, 0x0000, 0xfc00, 0xffff, 0xffff, 0xffff,
   0xf800, 0x000f, 0x0000, 0x0000, 0xf000, 0xffff, 0xffff, 0xffff, 0xf003,
   0x001f, 0x0000, 0x0000, 0x8000, 0xffff, 0xffff, 0xffff, 0xf007, 0x000f,
   0x0000, 0x0000, 0x0000, 0xfffc, 0xffff, 0xffff, 0xf83f, 0x000f, 0x0000,
   0x0000, 0x0000, 0xfff0, 0xffff, 0xffff, 0xf87f, 0x000f, 0x0000, 0x0000,
   0x0000, 0xff00, 0xffff, 0xffff, 0xffff, 0x001f, 0x0000, 0x0000, 0x0000,
   0xf800, 0xffff, 0xffff, 0xffff, 0x000f, 0x0000, 0x0000, 0x0000, 0xe000,
   0xffff, 0xffff, 0xffff, 0x000f, 0x0000, 0x0000, 0x0000, 0x8000, 0xfffe,
   0xffff, 0xffff, 0x000f, 0x0000, 0x0000, 0x0000, 0x0000, 0xfff8, 0xffff,
   0xffff, 0x001f, 0x0000, 0x0000, 0x0000, 0x002e, 0xffe0, 0xffff, 0xffff,
   0x000f, 0x0000, 0x0000, 0x0000, 0x00fc, 0xff00, 0xffff, 0xffff, 0x000f,
   0x0000, 0x0000, 0x0000, 0x07fe, 0xf800, 0xffff, 0xffff, 0x000f, 0x0000,
   0x0000, 0x0000, 0x5ffe, 0xc000, 0xffff, 0xffff, 0x001f, 0x0000, 0x0000,
   0x0000, 0xffff, 0x0001, 0xffff, 0xffff, 0x000f, 0x0000, 0x0000, 0x0000,
   0xffff, 0x0001, 0xfff8, 0xffff, 0x000f, 0x0000, 0x0000, 0x0000, 0xffff,
   0x0001, 0xffc0, 0xffff, 0x000f, 0x0000, 0x0000, 0x8000, 0xffff, 0x0001,
   0xfe00, 0xffff, 0x000f, 0x0000, 0x0000, 0x8000, 0xffff, 0x0000, 0xf800,
   0xffff, 0x0007, 0x0000, 0x0000, 0x8000, 0xffff, 0x0000, 0x8000, 0xffff,
   0x0007, 0x0000, 0x0000, 0x8000, 0xffff, 0x0000, 0x0000, 0xfffe, 0x0003,
   0x0000, 0x0000, 0xc000, 0x7fff, 0x0000, 0x0000, 0xfff0, 0x0003, 0x0000,
   0x0000, 0xc000, 0x7fff, 0x0000, 0x0000, 0xff80, 0x0001, 0x0000, 0x0000,
   0xc000, 0x7fff, 0x0000, 0x0000, 0xfe00, 0x0000, 0x0000, 0x0000, 0xe000,
   0x7fff, 0x0000, 0x0000, 0x1000, 0x0000, 0x0000, 0x0000, 0xe000, 0x7fff,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xe000, 0x3fff, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xf000, 0x3fff, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0xf000, 0x3fff, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0xf000, 0x1fff, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0xc000, 0x1fff, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x8000, 0x1fff, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0ffe, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0ff8, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0fe0,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0fc0, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0f00, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0400, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x8800, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x9c00, 0x0001, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0xc800, 0x0001, 0x0060, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x8000, 0x0001, 0x0060, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0xc000, 0x0001, 0x07f0, 0xf800, 0xf07f, 0xf07f, 0xf07f, 0xf07f,
   0x8c7f, 0xffc1, 0x07f1, 0xf800, 0xf87f, 0xf87f, 0xf8ff, 0xf8ff, 0xccff,
   0xffc1, 0x00e3, 0x1c00, 0x1ce0, 0x1ce0, 0x1ce0, 0x3ce0, 0x8ce0, 0x00e1,
   0x0067, 0x0c00, 0x0cc0, 0x0cc0, 0x1cc0, 0x1dc0, 0xcdc0, 0x0061, 0x0066,
   0x0c00, 0x0cc0, 0xfdc0, 0x0dff, 0x0dc0, 0x8dc0, 0x0071, 0x0066, 0x0c00,
   0x0cc0, 0xfdc0, 0x0dff, 0x0dc0, 0xcdc0, 0x0071, 0x0066, 0x1c00, 0x1cc0,
   0x1cc0, 0x0c00, 0x0dc0, 0x8dc0, 0x0061, 0x00e6, 0x1c00, 0x0ce8, 0x5ce8,
   0x1c55, 0x1dc0, 0xcce8, 0x20e1, 0x00c7, 0xf800, 0xffff, 0xf8ff, 0x0c7f,
   0xffc0, 0x8cff, 0xffe7, 0x07c3, 0xf000, 0xffff, 0xf07f, 0x0c7f, 0xffc0,
   0x0c7f, 0xff87, 0x0781, 0x0000, 0x0c00, 0x0000, 0x0c00, 0xdf40, 0x000d,
   0x0000, 0x0000, 0x0000, 0x0c00, 0x0000, 0x0000, 0x0c00, 0x0000, 0x0000,
   0x0000, 0x0000, 0x1c00, 0x0000, 0x0000, 0x0c00, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0c00, 0x0000, 0x0000, 0x1c00, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 };


uint16_t mirror(uint16_t source)
{
	int result = ((source & 0x8000) >> 15) | ((source & 0x4000) >> 13) |
			((source & 0x2000) >> 11) | ((source & 0x1000) >> 9) |
			((source & 0x0800) >> 7) | ((source & 0x0400) >> 5) |
			((source & 0x0200) >> 3) | ((source & 0x0100) >> 1) |
			((source & 0x0080) << 1) | ((source & 0x0040) << 3) |
			((source & 0x0020) << 5) | ((source & 0x0010) << 7) |
			((source & 0x0008) << 9) | ((source & 0x0004) << 11) |
			((source & 0x0002) << 13) | ((source & 0x0001) << 15);

	   return result;
}

void clearGraphics() {
	for (uint16_t x = 0; x < GRAPHICS_WIDTH*GRAPHICS_HEIGHT; ++x) {
		  draw_buffer_level[x] = 0x0000;
		  draw_buffer_mask[x] = 0x0000;
	}
}

void copyimage(uint16_t offsetx, uint16_t offsety) {
	offsetx=offsetx/16;
	int i=0;
	  for (uint16_t y = offsety; y < (128+offsety); y++) {
		  for (uint16_t x = offsetx; x < (8+offsetx); x++) {
			  draw_buffer_level[y*GRAPHICS_WIDTH+x] = draw_buffer_mask[y*GRAPHICS_WIDTH+x]  = mirror(logo_bits[(y-offsety)*8+(x-offsetx)]);
			  i+=2;
		  }
	}
}

uint8_t validPos(uint16_t x, uint16_t y) {
	if (x >= GRAPHICS_WIDTH_REAL || y >= GRAPHICS_HEIGHT) {
		return 0;
	}
	return 1;
}

void setPixel(uint16_t x, uint16_t y, uint8_t state) {
	if (!validPos(x, y)) {
		return;
	}
	uint8_t bitPos = 15-(x%16);
	uint16_t tempf = draw_buffer_level[y*GRAPHICS_WIDTH+x/16];
	uint16_t tempm = draw_buffer_mask[y*GRAPHICS_WIDTH+x/16];
	if (state == 0) {
		tempf &= ~(1<<bitPos);
		tempm &= ~(1<<bitPos);
	}
	else if (state == 1) {
		tempf |= (1<<bitPos);
		tempm |= (1<<bitPos);
	}
	else {
		tempf ^= (1<<bitPos);
		tempm ^= (1<<bitPos);
	}
	draw_buffer_level[y*GRAPHICS_WIDTH+x/16] = tempf;
	draw_buffer_mask[y*GRAPHICS_WIDTH+x/16] = tempm;
}

// Credit for this one goes to wikipedia! :-)
void drawCircle(uint16_t x0, uint16_t y0, uint16_t radius) {
	  int f = 1 - radius;
	  int ddF_x = 1;
	  int ddF_y = -2 * radius;
	  int x = 0;
	  int y = radius;

	  setPixel(x0, y0 + radius,1);
	  setPixel(x0, y0 - radius,1);
	  setPixel(x0 + radius, y0,1);
	  setPixel(x0 - radius, y0,1);

	  while(x < y)
	  {
	    // ddF_x == 2 * x + 1;
	    // ddF_y == -2 * y;
	    // f == x*x + y*y - radius*radius + 2*x - y + 1;
	    if(f >= 0)
	    {
	      y--;
	      ddF_y += 2;
	      f += ddF_y;
	    }
	    x++;
	    ddF_x += 2;
	    f += ddF_x;
	    setPixel(x0 + x, y0 + y,1);
	    setPixel(x0 - x, y0 + y,1);
	    setPixel(x0 + x, y0 - y,1);
	    setPixel(x0 - x, y0 - y,1);
	    setPixel(x0 + y, y0 + x,1);
	    setPixel(x0 - y, y0 + x,1);
	    setPixel(x0 + y, y0 - x,1);
	    setPixel(x0 - y, y0 - x,1);
	  }
}

void swap(uint16_t* a, uint16_t* b) {
	uint16_t temp = *a;
	*a = *b;
	*b = temp;
}


void drawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
	uint8_t steep = abs(y1 - y0) > abs(x1 - x0);
	if (steep) {
		swap(&x0, &y0);
		swap(&x1, &y1);
	}
	if (x0 > x1) {
		swap(&x0, &x1);
		swap(&y0, &y1);
	}
	int16_t deltax = x1 - x0;
	int16_t deltay = abs(y1 - y0);
	int16_t error = deltax / 2;
	int16_t ystep;
	int16_t y = y0;
	if (y0 < y1) {
		ystep = 1;
	}
	else {
		ystep = -1;
	}
	for (uint16_t x = x0; x <= x1; ++x) {
		if (steep) {
			setPixel(y, x, 1);
		}
		else {
			setPixel(x, y, 1);
		}
		error = error - deltay;
		if (error < 0) {
			y = y + ystep;
			error = error + deltax;
		}
	}
}

const static int8_t sinData[91] = {
  0, 2, 3, 5, 7, 9, 10, 12, 14, 16, 17, 19, 21, 22, 24, 26, 28, 29, 31, 33,
  34, 36, 37, 39, 41, 42, 44, 45, 47, 48, 50, 52, 53, 54, 56, 57, 59, 60, 62,
  63, 64, 66, 67, 68, 69, 71, 72, 73, 74, 75, 77, 78, 79, 80, 81, 82, 83, 84,
  85, 86, 87, 87, 88, 89, 90, 91, 91, 92, 93, 93, 94, 95, 95, 96, 96, 97, 97,
  97, 98, 98, 98, 99, 99, 99, 99, 100, 100, 100, 100, 100, 100};

static int8_t mySin(uint16_t angle) {
	uint16_t pos = 0;
	pos = angle % 360;
	int8_t mult = 1;
	// 180-359 is same as 0-179 but negative.
	if (pos >= 180) {
		pos = pos - 180;
		mult = -1;
	}
	// 0-89 is equal to 90-179 except backwards.
	if (pos >= 90) {
		pos = 180 - pos;
	}
	return mult * (int8_t)(sinData[pos]);
}

static int8_t myCos(uint16_t angle) {
	return mySin(angle + 90);
}

//fill the draw_buffer_level with junk, used for debugging
/*void fillFrameBuffer(void)
{
	uint16_t i;
	uint16_t j;
	for(i = 0; i<(BUFFER_VERT_SIZE); i++)
	{
		for(j = 0; j<(BUFFER_LINE_LENGTH); j++)
		{
			if(j < LEFT_MARGIN)
			{
				draw_buffer_level[i*GRAPHICS_WIDTH+j] = 0;
			} else if (j > 32){
				draw_buffer_level[i*GRAPHICS_WIDTH+j] = 0;
			} else {
				draw_buffer_level[i*GRAPHICS_WIDTH+j] = i*j;
			}
		}
	}
}*/


/// Draws four points relative to the given center point.
///
/// \li centerX + X, centerY + Y
/// \li centerX + X, centerY - Y
/// \li centerX - X, centerY + Y
/// \li centerX - X, centerY - Y
///
/// \param centerX the x coordinate of the center point
/// \param centerY the y coordinate of the center point
/// \param deltaX the difference between the centerX coordinate and each pixel drawn
/// \param deltaY the difference between the centerY coordinate and each pixel drawn
/// \param color the color to draw the pixels with.
void plotFourQuadrants(int32_t centerX, int32_t centerY, int32_t deltaX, int32_t deltaY)
{
    setPixel(centerX + deltaX, centerY + deltaY,1);      // Ist      Quadrant
    setPixel(centerX - deltaX, centerY + deltaY,1);      // IInd     Quadrant
    setPixel(centerX - deltaX, centerY - deltaY,1);      // IIIrd    Quadrant
    setPixel(centerX + deltaX, centerY - deltaY,1);      // IVth     Quadrant
}

/// Implements the midpoint ellipse drawing algorithm which is a bresenham
/// style DDF.
///
/// \param centerX the x coordinate of the center of the ellipse
/// \param centerY the y coordinate of the center of the ellipse
/// \param horizontalRadius the horizontal radius of the ellipse
/// \param verticalRadius the vertical radius of the ellipse
/// \param color the color of the ellipse border
void ellipse(int centerX, int centerY, int horizontalRadius, int verticalRadius)
{
    int64_t doubleHorizontalRadius = horizontalRadius * horizontalRadius;
    int64_t doubleVerticalRadius = verticalRadius * verticalRadius;

    int64_t error = doubleVerticalRadius - doubleHorizontalRadius * verticalRadius + (doubleVerticalRadius >> 2);

    int x = 0;
    int y = verticalRadius;
    int deltaX = 0;
    int deltaY = (doubleHorizontalRadius << 1) * y;

    plotFourQuadrants(centerX, centerY, x, y);

    while(deltaY >= deltaX)
    {
          x++;
          deltaX += (doubleVerticalRadius << 1);

          error +=  deltaX + doubleVerticalRadius;

          if(error >= 0)
          {
               y--;
               deltaY -= (doubleHorizontalRadius << 1);

               error -= deltaY;
          }
          plotFourQuadrants(centerX, centerY, x, y);
    }

    error = (int64_t)(doubleVerticalRadius * (x + 1 / 2.0) * (x + 1 / 2.0) + doubleHorizontalRadius * (y - 1) * (y - 1) - doubleHorizontalRadius * doubleVerticalRadius);

    while (y>=0)
    {
          error += doubleHorizontalRadius;
          y--;
          deltaY -= (doubleHorizontalRadius<<1);
          error -= deltaY;

          if(error <= 0)
          {
               x++;
               deltaX += (doubleVerticalRadius << 1);
               error += deltaX;
          }

          plotFourQuadrants(centerX, centerY, x, y);
    }
}


void drawArrow(uint16_t x, uint16_t y, uint16_t angle, uint16_t size)
{
	int16_t a = myCos(angle);
	int16_t b = mySin(angle);
	a = (a * (size/2)) / 100;
	b = (b * (size/2)) / 100;
	drawLine((x)-1 - b, (y)-1 + a, (x)-1 + b, (y)-1 - a); //Direction line
	//drawLine((GRAPHICS_SIZE/2)-1 + a/2, (GRAPHICS_SIZE/2)-1 + b/2, (GRAPHICS_SIZE/2)-1 - a/2, (GRAPHICS_SIZE/2)-1 - b/2); //Arrow bottom line
	drawLine((x)-1 + b, (y)-1 - a, (x)-1 - a/2, (y)-1 - b/2); // Arrow "wings"
	drawLine((x)-1 + b, (y)-1 - a, (x)-1 + a/2, (y)-1 + b/2);
}

void drawBox(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
	drawLine(x1, y1, x2, y1); //top
	drawLine(x1, y1, x1, y2); //left
	drawLine(x2, y1, x2, y2); //right
	drawLine(x1, y2, x2, y2); //bottom
}

// simple routines

// SUPEROSD routines, modified

/**
 * write_pixel: Write a pixel at an x,y position to a given surface.
 *
 * @param       buff    pointer to buffer to write in
 * @param       x               x coordinate
 * @param       y               y coordinate
 * @param       mode    0 = clear bit, 1 = set bit, 2 = toggle bit
 */
void write_pixel(uint16_t *buff, unsigned int x, unsigned int y, int mode)
{
        CHECK_COORDS(x, y);
        // Determine the bit in the word to be set and the word
        // index to set it in.
        int bitnum = CALC_BIT_IN_WORD(x);
        int wordnum = CALC_BUFF_ADDR(x, y);
        // Apply a mask.
        uint16_t mask = 1 << (15 - bitnum);
        WRITE_WORD_MODE(buff, wordnum, mask, mode);
}

/**
 * write_pixel_lm: write the pixel on both surfaces (level and mask.)
 * Uses current draw buffer.
 *
 * @param       x               x coordinate
 * @param       y               y coordinate
 * @param       mmode   0 = clear, 1 = set, 2 = toggle
 * @param       lmode   0 = black, 1 = white, 2 = toggle
 */
void write_pixel_lm(unsigned int x, unsigned int y, int mmode, int lmode)
{
        CHECK_COORDS(x, y);
        // Determine the bit in the word to be set and the word
        // index to set it in.
        int bitnum = CALC_BIT_IN_WORD(x);
        int wordnum = CALC_BUFF_ADDR(x, y);
        // Apply the masks.
        uint16_t mask = 1 << (15 - bitnum);
        WRITE_WORD_MODE(draw_buffer_mask, wordnum, mask, mmode);
        WRITE_WORD_MODE(draw_buffer_level, wordnum, mask, lmode);
}


/**
 * write_hline: optimised horizontal line writing algorithm
 *
 * @param       buff    pointer to buffer to write in
 * @param       x0              x0 coordinate
 * @param       x1              x1 coordinate
 * @param       y               y coordinate
 * @param       mode    0 = clear, 1 = set, 2 = toggle
 */
void write_hline(uint16_t *buff, unsigned int x0, unsigned int x1, unsigned int y, int mode)
{
        CLIP_COORDS(x0, y);
        CLIP_COORDS(x1, y);
        if(x0 > x1)
        {
                SWAP(x0, x1);
        }
        if(x0 == x1) return;
        /* This is an optimised algorithm for writing horizontal lines.
         * We begin by finding the addresses of the x0 and x1 points. */
        int addr0 = CALC_BUFF_ADDR(x0, y);
        int addr1 = CALC_BUFF_ADDR(x1, y);
        int addr0_bit = CALC_BIT_IN_WORD(x0);
        int addr1_bit = CALC_BIT_IN_WORD(x1);
        int mask, mask_l, mask_r, i;
        /* If the addresses are equal, we only need to write one word
         * which is an island. */
        if(addr0 == addr1)
        {
                mask = COMPUTE_HLINE_ISLAND_MASK(addr0_bit, addr1_bit);
                WRITE_WORD_MODE(buff, addr0, mask, mode);
        }
        /* Otherwise we need to write the edges and then the middle. */
        else
        {
                mask_l = COMPUTE_HLINE_EDGE_L_MASK(addr0_bit);
                mask_r = COMPUTE_HLINE_EDGE_R_MASK(addr1_bit);
                WRITE_WORD_MODE(buff, addr0, mask_l, mode);
                WRITE_WORD_MODE(buff, addr1, mask_r, mode);
                // Now write 0xffff words from start+1 to end-1.
                for(i = addr0 + 1; i <= addr1 - 1; i++)
                {
                        WRITE_WORD_MODE(buff, i, 0xffff, mode);
                }
        }
}

/**
 * write_hline_lm: write both level and mask buffers.
 *
 * @param       x0              x0 coordinate
 * @param       x1              x1 coordinate
 * @param       y               y coordinate
 * @param       lmode   0 = clear, 1 = set, 2 = toggle
 * @param       mmode   0 = clear, 1 = set, 2 = toggle
 */
void write_hline_lm(unsigned int x0, unsigned int x1, unsigned int y, int lmode, int mmode)
{
        // TODO: an optimisation would compute the masks and apply to
        // both buffers simultaneously.
        write_hline(draw_buffer_level, x0, x1, y, lmode);
        write_hline(draw_buffer_mask, x0, x1, y, mmode);
}

/**
 * write_hline_outlined: outlined horizontal line with varying endcaps
 * Always uses draw buffer.
 *
 * @param       x0                      x0 coordinate
 * @param       x1                      x1 coordinate
 * @param       y                       y coordinate
 * @param       endcap0         0 = none, 1 = single pixel, 2 = full cap
 * @param       endcap1         0 = none, 1 = single pixel, 2 = full cap
 * @param       mode            0 = black outline, white body, 1 = white outline, black body
 * @param       mmode           0 = clear, 1 = set, 2 = toggle
 */
void write_hline_outlined(unsigned int x0, unsigned int x1, unsigned int y, int endcap0, int endcap1, int mode, int mmode)
{
        int stroke, fill;
        SETUP_STROKE_FILL(stroke, fill, mode)
        if(x0 > x1)
        {
                SWAP(x0, x1);
        }
        // Draw the main body of the line.
        write_hline_lm(x0 + 1, x1 - 1, y - 1, stroke, mmode);
        write_hline_lm(x0 + 1, x1 - 1, y + 1, stroke, mmode);
        write_hline_lm(x0 + 1, x1 - 1, y, fill, mmode);
        // Draw the endcaps, if any.
        DRAW_ENDCAP_HLINE(endcap0, x0, y, stroke, fill, mmode);
        DRAW_ENDCAP_HLINE(endcap1, x1, y, stroke, fill, mmode);
}

/**
 * write_vline: optimised vertical line writing algorithm
 *
 * @param       buff    pointer to buffer to write in
 * @param       x               x coordinate
 * @param       y0              y0 coordinate
 * @param       y1              y1 coordinate
 * @param       mode    0 = clear, 1 = set, 2 = toggle
 */
void write_vline(uint16_t *buff, unsigned int x, unsigned int y0, unsigned int y1, int mode)
{
        unsigned int a;
        CLIP_COORDS(x, y0);
        CLIP_COORDS(x, y1);
        if(y0 > y1)
        {
                SWAP(y0, y1);
        }
        if(y0 == y1) return;
        /* This is an optimised algorithm for writing vertical lines.
         * We begin by finding the addresses of the x,y0 and x,y1 points. */
        int addr0 = CALC_BUFF_ADDR(x, y0);
        int addr1 = CALC_BUFF_ADDR(x, y1);
        /* Then we calculate the pixel data to be written. */
        int bitnum = CALC_BIT_IN_WORD(x);
        uint16_t mask = 1 << (15 - bitnum);
        /* Run from addr0 to addr1 placing pixels. Increment by the number
         * of words n each graphics line. */
        for(a = addr0; a <= addr1; a += DISP_WIDTH / 16)
        {
                WRITE_WORD_MODE(buff, a, mask, mode);
        }
}

/**
 * write_vline_lm: write both level and mask buffers.
 *
 * @param       x               x coordinate
 * @param       y0              y0 coordinate
 * @param       y1              y1 coordinate
 * @param       lmode   0 = clear, 1 = set, 2 = toggle
 * @param       mmode   0 = clear, 1 = set, 2 = toggle
 */
void write_vline_lm(unsigned int x, unsigned int y0, unsigned int y1, int lmode, int mmode)
{
        // TODO: an optimisation would compute the masks and apply to
        // both buffers simultaneously.
        write_vline(draw_buffer_level, x, y0, y1, lmode);
        write_vline(draw_buffer_mask, x, y0, y1, mmode);
}

/**
 * write_vline_outlined: outlined vertical line with varying endcaps
 * Always uses draw buffer.
 *
 * @param       x                       x coordinate
 * @param       y0                      y0 coordinate
 * @param       y1                      y1 coordinate
 * @param       endcap0         0 = none, 1 = single pixel, 2 = full cap
 * @param       endcap1         0 = none, 1 = single pixel, 2 = full cap
 * @param       mode            0 = black outline, white body, 1 = white outline, black body
 * @param       mmode           0 = clear, 1 = set, 2 = toggle
 */
void write_vline_outlined(unsigned int x, unsigned int y0, unsigned int y1, int endcap0, int endcap1, int mode, int mmode)
{
        int stroke, fill;
        if(y0 > y1)
        {
                SWAP(y0, y1);
        }
        SETUP_STROKE_FILL(stroke, fill, mode);
        // Draw the main body of the line.
        write_vline_lm(x - 1, y0 + 1, y1 - 1, stroke, mmode);
        write_vline_lm(x + 1, y0 + 1, y1 - 1, stroke, mmode);
        write_vline_lm(x, y0 + 1, y1 - 1, fill, mmode);
        // Draw the endcaps, if any.
        DRAW_ENDCAP_VLINE(endcap0, x, y0, stroke, fill, mmode);
        DRAW_ENDCAP_VLINE(endcap1, x, y1, stroke, fill, mmode);
}

/**
 * write_filled_rectangle: draw a filled rectangle.
 *
 * Uses an optimised algorithm which is similar to the horizontal
 * line writing algorithm, but optimised for writing the lines
 * multiple times without recalculating lots of stuff.
 *
 * @param       buff    pointer to buffer to write in
 * @param       x               x coordinate (left)
 * @param       y               y coordinate (top)
 * @param       width   rectangle width
 * @param       height  rectangle height
 * @param       mode    0 = clear, 1 = set, 2 = toggle
 */
void write_filled_rectangle(uint16_t *buff, unsigned int x, unsigned int y, unsigned int width, unsigned int height, int mode)
{
        int yy, addr0_old, addr1_old;
        CHECK_COORDS(x, y);
        CHECK_COORD_X(x + width);
        CHECK_COORD_Y(y + height);
        if(width <= 0 || height <= 0) return;
        // Calculate as if the rectangle was only a horizontal line. We then
        // step these addresses through each row until we iterate `height` times.
        int addr0 = CALC_BUFF_ADDR(x, y);
        int addr1 = CALC_BUFF_ADDR(x + width, y);
        int addr0_bit = CALC_BIT_IN_WORD(x);
        int addr1_bit = CALC_BIT_IN_WORD(x + width);
        int mask, mask_l, mask_r, i;
        // If the addresses are equal, we need to write one word vertically.
        if(addr0 == addr1)
        {
                mask = COMPUTE_HLINE_ISLAND_MASK(addr0_bit, addr1_bit);
                while(height--)
                {
                        WRITE_WORD_MODE(buff, addr0, mask, mode);
                        addr0 += DISP_WIDTH / 16;
                }
        }
        // Otherwise we need to write the edges and then the middle repeatedly.
        else
        {
                mask_l = COMPUTE_HLINE_EDGE_L_MASK(addr0_bit);
                mask_r = COMPUTE_HLINE_EDGE_R_MASK(addr1_bit);
                // Write edges first.
                yy = 0;
                addr0_old = addr0;
                addr1_old = addr1;
                while(yy < height)
                {
                        WRITE_WORD_MODE(buff, addr0, mask_l, mode);
                        WRITE_WORD_MODE(buff, addr1, mask_r, mode);
                        addr0 += DISP_WIDTH / 16;
                        addr1 += DISP_WIDTH / 16;
                        yy++;
                }
                // Now write 0xffff words from start+1 to end-1 for each row.
                yy = 0;
                addr0 = addr0_old;
                addr1 = addr1_old;
                while(yy < height)
                {
                        for(i = addr0 + 1; i <= addr1 - 1; i++)
                        {
                                WRITE_WORD_MODE(buff, i, 0xffff, mode);
                        }
                        addr0 += DISP_WIDTH / 16;
                        addr1 += DISP_WIDTH / 16;
                        yy++;
                }
        }
}

/**
 * write_filled_rectangle_lm: draw a filled rectangle on both draw buffers.
 *
 * @param       x               x coordinate (left)
 * @param       y               y coordinate (top)
 * @param       width   rectangle width
 * @param       height  rectangle height
 * @param       lmode   0 = clear, 1 = set, 2 = toggle
 * @param       mmode   0 = clear, 1 = set, 2 = toggle
 */
void write_filled_rectangle_lm(unsigned int x, unsigned int y, unsigned int width, unsigned int height, int lmode, int mmode)
{
        write_filled_rectangle(draw_buffer_mask, x, y, width, height, mmode);
        write_filled_rectangle(draw_buffer_level, x, y, width, height, lmode);
}

/**
 * write_rectangle_outlined: draw an outline of a rectangle. Essentially
 * a convenience wrapper for draw_hline_outlined and draw_vline_outlined.
 *
 * @param       x               x coordinate (left)
 * @param       y               y coordinate (top)
 * @param       width   rectangle width
 * @param       height  rectangle height
 * @param       mode    0 = black outline, white body, 1 = white outline, black body
 * @param       mmode   0 = clear, 1 = set, 2 = toggle
 */
void write_rectangle_outlined(unsigned int x, unsigned int y, int width, int height, int mode, int mmode)
{
        //CHECK_COORDS(x, y);
        //CHECK_COORDS(x + width, y + height);
        //if((x + width) > DISP_WIDTH) width = DISP_WIDTH - x;
        //if((y + height) > DISP_HEIGHT) height = DISP_HEIGHT - y;
        write_hline_outlined(x, x + width, y, ENDCAP_ROUND, ENDCAP_ROUND, mode, mmode);
        write_hline_outlined(x, x + width, y + height, ENDCAP_ROUND, ENDCAP_ROUND, mode, mmode);
        write_vline_outlined(x, y, y + height, ENDCAP_ROUND, ENDCAP_ROUND, mode, mmode);
        write_vline_outlined(x + width, y, y + height, ENDCAP_ROUND, ENDCAP_ROUND, mode, mmode);
}

/**
 * write_circle: draw the outline of a circle on a given buffer,
 * with an optional dash pattern for the line instead of a normal line.
 *
 * @param       buff    pointer to buffer to write in
 * @param       cx              origin x coordinate
 * @param       cy              origin y coordinate
 * @param       r               radius
 * @param       dashp   dash period (pixels) - zero for no dash
 * @param       mode    0 = clear, 1 = set, 2 = toggle
 */
void write_circle(uint16_t *buff, unsigned int cx, unsigned int cy, unsigned int r, unsigned int dashp, int mode)
{
        CHECK_COORDS(cx, cy);
        int error = -r, x = r, y = 0;
        while(x >= y)
        {
                if(dashp == 0 || (y % dashp) < (dashp / 2))
                {
                        CIRCLE_PLOT_8(buff, cx, cy, x, y, mode);
                }
                error += (y * 2) + 1;
                y++;
                if(error >= 0)
                {
                        --x;
                        error -= x * 2;
                }
        }
}

/**
 * write_circle_outlined: draw an outlined circle on the draw buffer.
 *
 * @param       cx              origin x coordinate
 * @param       cy              origin y coordinate
 * @param       r               radius
 * @param       dashp   dash period (pixels) - zero for no dash
 * @param       bmode   0 = 4-neighbour border, 1 = 8-neighbour border
 * @param       mode    0 = black outline, white body, 1 = white outline, black body
 * @param       mmode   0 = clear, 1 = set, 2 = toggle
 */
void write_circle_outlined(unsigned int cx, unsigned int cy, unsigned int r, unsigned int dashp, int bmode, int mode, int mmode)
{
        int stroke, fill;
        CHECK_COORDS(cx, cy);
        SETUP_STROKE_FILL(stroke, fill, mode);
        // This is a two step procedure. First, we draw the outline of the
        // circle, then we draw the inner part.
        int error = -r, x = r, y = 0;
        while(x >= y)
        {
                if(dashp == 0 || (y % dashp) < (dashp / 2))
                {
                        CIRCLE_PLOT_8(draw_buffer_mask, cx, cy, x + 1, y, mmode);
                        CIRCLE_PLOT_8(draw_buffer_level, cx, cy, x + 1, y, stroke);
                        CIRCLE_PLOT_8(draw_buffer_mask, cx, cy, x, y + 1, mmode);
                        CIRCLE_PLOT_8(draw_buffer_level, cx, cy, x, y + 1, stroke);
                        CIRCLE_PLOT_8(draw_buffer_mask, cx, cy, x - 1, y, mmode);
                        CIRCLE_PLOT_8(draw_buffer_level, cx, cy, x - 1, y, stroke);
                        CIRCLE_PLOT_8(draw_buffer_mask, cx, cy, x, y - 1, mmode);
                        CIRCLE_PLOT_8(draw_buffer_level, cx, cy, x, y - 1, stroke);
                        if(bmode == 1)
                        {
                                CIRCLE_PLOT_8(draw_buffer_mask, cx, cy, x + 1, y + 1, mmode);
                                CIRCLE_PLOT_8(draw_buffer_level, cx, cy, x + 1, y + 1, stroke);
                                CIRCLE_PLOT_8(draw_buffer_mask, cx, cy, x - 1, y - 1, mmode);
                                CIRCLE_PLOT_8(draw_buffer_level, cx, cy, x - 1, y - 1, stroke);
                        }
                }
                error += (y * 2) + 1;
                y++;
                if(error >= 0)
                {
                        --x;
                        error -= x * 2;
                }
        }
        error = -r;
        x = r;
        y = 0;
        while(x >= y)
        {
                if(dashp == 0 || (y % dashp) < (dashp / 2))
                {
                        CIRCLE_PLOT_8(draw_buffer_mask, cx, cy, x, y, mmode);
                        CIRCLE_PLOT_8(draw_buffer_level, cx, cy, x, y, fill);
                }
                error += (y * 2) + 1;
                y++;
                if(error >= 0)
                {
                        --x;
                        error -= x * 2;
                }
        }
}

/**
 * write_circle_filled: fill a circle on a given buffer.
 *
 * @param       buff    pointer to buffer to write in
 * @param       cx              origin x coordinate
 * @param       cy              origin y coordinate
 * @param       r               radius
 * @param       mode    0 = clear, 1 = set, 2 = toggle
 */
void write_circle_filled(uint16_t *buff, unsigned int cx, unsigned int cy, unsigned int r, int mode)
{
        CHECK_COORDS(cx, cy);
        int error = -r, x = r, y = 0, xch = 0;
        // It turns out that filled circles can take advantage of the midpoint
        // circle algorithm. We simply draw very fast horizontal lines across each
        // pair of X,Y coordinates. In some cases, this can even be faster than
        // drawing an outlined circle!
        //
        // Due to multiple writes to each set of pixels, we have a special exception
        // for when using the toggling draw mode.
        while(x >= y)
        {
                if(y != 0)
                {
                        write_hline(buff, cx - x, cx + x, cy + y, mode);
                        write_hline(buff, cx - x, cx + x, cy - y, mode);
                        if(mode != 2 || (mode == 2 && xch && (cx - x) != (cx - y)))
                        {
                                write_hline(buff, cx - y, cx + y, cy + x, mode);
                                write_hline(buff, cx - y, cx + y, cy - x, mode);
                                xch = 0;
                        }
                }
                error += (y * 2) + 1;
                y++;
                if(error >= 0)
                {
                        --x;
                        xch = 1;
                        error -= x * 2;
                }
        }
        // Handle toggle mode.
        if(mode == 2)
        {
                write_hline(buff, cx - r, cx + r, cy, mode);
        }
}

/**
 * write_line: Draw a line of arbitrary angle.
 *
 * @param       buff    pointer to buffer to write in
 * @param       x0              first x coordinate
 * @param       y0              first y coordinate
 * @param       x1              second x coordinate
 * @param       y1              second y coordinate
 * @param       mode    0 = clear, 1 = set, 2 = toggle
 */
void write_line(uint16_t *buff, unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1, int mode)
{
        // Based on http://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm
        int steep = abs(y1 - y0) > abs(x1 - x0);
        if(steep)
        {
                SWAP(x0, y0);
                SWAP(x1, y1);
        }
        if(x0 > x1)
        {
                SWAP(x0, x1);
                SWAP(y0, y1);
        }
        int deltax = x1 - x0;
        int deltay = abs(y1 - y0);
        int error = deltax / 2;
        int ystep;
        int y = y0;
        int x, lasty = y, stox = 0;
        if(y0 < y1)
                ystep = 1;
        else
                ystep = -1;
        for(x = x0; x < x1; x++)
        {
                if(steep)
                {
                        if(lasty != y)
                        {
                                if(x > lasty)
                                        write_vline(buff, stox, y, lasty, mode);
                                else
                                        write_vline(buff, stox, lasty, y, mode);
                                lasty = y;
                                stox = x;
                        }
                }
                else
                {
                        //write_pixel(buff, x, y, mode);
                        /*
                        if(lasty != y)
                        {
                                if(y > lasty)
                                        write_vline(buff, stox, y, lasty, mode);
                                else
                                        write_vline(buff, stox, lasty, y, mode);
                                lasty = y;
                                stox = x;
                        }
                        */
                }
                error -= deltay;
                if(error < 0)
                {
                        y += ystep;
                        error += deltax;
                }
        }
}

/**
 * write_line_lm: Draw a line of arbitrary angle.
 *
 * @param       x0              first x coordinate
 * @param       y0              first y coordinate
 * @param       x1              second x coordinate
 * @param       y1              second y coordinate
 * @param       mmode   0 = clear, 1 = set, 2 = toggle
 * @param       lmode   0 = clear, 1 = set, 2 = toggle
 */
void write_line_lm(unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1, int mmode, int lmode)
{
        write_line(draw_buffer_mask, x0, y0, x1, y1, mmode);
        write_line(draw_buffer_level, x0, y0, x1, y1, lmode);
}

/**
 * write_line_outlined: Draw a line of arbitrary angle, with an outline.
 *
 * @param       buff            pointer to buffer to write in
 * @param       x0                      first x coordinate
 * @param       y0                      first y coordinate
 * @param       x1                      second x coordinate
 * @param       y1                      second y coordinate
 * @param       endcap0         0 = none, 1 = single pixel, 2 = full cap
 * @param       endcap1         0 = none, 1 = single pixel, 2 = full cap
 * @param       mode            0 = black outline, white body, 1 = white outline, black body
 * @param       mmode           0 = clear, 1 = set, 2 = toggle
 */
void write_line_outlined(unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1, int endcap0, int endcap1, int mode, int mmode)
{
        // Based on http://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm
        // This could be improved for speed.
        int omode, imode;
        if(mode == 0)
        {
                omode = 0;
                imode = 1;
        }
        else
        {
                omode = 1;
                imode = 0;
        }
        int steep = abs(y1 - y0) > abs(x1 - x0);
        if(steep)
        {
                SWAP(x0, y0);
                SWAP(x1, y1);
        }
        if(x0 > x1)
        {
                SWAP(x0, x1);
                SWAP(y0, y1);
        }
        int deltax = x1 - x0;
        int deltay = abs(y1 - y0);
        int error = deltax / 2;
        int ystep;
        int y = y0;
        int x;
        if(y0 < y1)
                ystep = 1;
        else
                ystep = -1;
        // Draw the outline.
        for(x = x0; x < x1; x++)
        {
                if(steep)
                {
                        write_pixel_lm(y - 1, x, mmode, omode);
                        write_pixel_lm(y + 1, x, mmode, omode);
                        write_pixel_lm(y, x - 1, mmode, omode);
                        write_pixel_lm(y, x + 1, mmode, omode);
                }
                else
                {
                        write_pixel_lm(x - 1, y, mmode, omode);
                        write_pixel_lm(x + 1, y, mmode, omode);
                        write_pixel_lm(x, y - 1, mmode, omode);
                        write_pixel_lm(x, y + 1, mmode, omode);
                }
                error -= deltay;
                if(error < 0)
                {
                        y += ystep;
                        error += deltax;
                }
        }
        // Now draw the innards.
        error = deltax / 2;
        y = y0;
        for(x = x0; x < x1; x++)
        {
                if(steep)
                {
                        write_pixel_lm(y, x, mmode, imode);
                }
                else
                {
                        write_pixel_lm(x, y, mmode, imode);
                }
                error -= deltay;
                if(error < 0)
                {
                        y += ystep;
                        error += deltax;
                }
        }
}

/**
 * write_word_misaligned: Write a misaligned word across two addresses
 * with an x offset.
 *
 * This allows for many pixels to be set in one write.
 *
 * @param       buff    buffer to write in
 * @param       word    word to write (16 bits)
 * @param       addr    address of first word
 * @param       xoff    x offset (0-15)
 * @param       mode    0 = clear, 1 = set, 2 = toggle
 */
void write_word_misaligned(uint16_t *buff, uint16_t word, unsigned int addr, unsigned int xoff, int mode)
{
        uint16_t firstmask = word >> xoff;
        uint16_t lastmask = word << (16 - xoff);
        WRITE_WORD_MODE(buff, addr, firstmask, mode);
        if(xoff > 0)
        {
                WRITE_WORD_MODE(buff, addr + 1, lastmask, mode);
        }
}

/**
 * write_word_misaligned_NAND: Write a misaligned word across two addresses
 * with an x offset, using a NAND mask.
 *
 * This allows for many pixels to be set in one write.
 *
 * @param       buff    buffer to write in
 * @param       word    word to write (16 bits)
 * @param       addr    address of first word
 * @param       xoff    x offset (0-15)
 *
 * This is identical to calling write_word_misaligned with a mode of 0 but
 * it doesn't go through a lot of switch logic which slows down text writing
 * a lot.
 */
void write_word_misaligned_NAND(uint16_t *buff, uint16_t word, unsigned int addr, unsigned int xoff)
{
        uint16_t firstmask = word >> xoff;
        uint16_t lastmask = word << (16 - xoff);
        WRITE_WORD_NAND(buff, addr, firstmask);
        if(xoff > 0)
        {
                WRITE_WORD_NAND(buff, addr + 1, lastmask);
        }
}

/**
 * write_word_misaligned_OR: Write a misaligned word across two addresses
 * with an x offset, using an OR mask.
 *
 * This allows for many pixels to be set in one write.
 *
 * @param       buff    buffer to write in
 * @param       word    word to write (16 bits)
 * @param       addr    address of first word
 * @param       xoff    x offset (0-15)
 *
 * This is identical to calling write_word_misaligned with a mode of 1 but
 * it doesn't go through a lot of switch logic which slows down text writing
 * a lot.
 */
void write_word_misaligned_OR(uint16_t *buff, uint16_t word, unsigned int addr, unsigned int xoff)
{
        uint16_t firstmask = word >> xoff;
        uint16_t lastmask = word << (16 - xoff);
        WRITE_WORD_OR(buff, addr, firstmask);
        if(xoff > 0)
        {
                WRITE_WORD_OR(buff, addr + 1, lastmask);
        }
}

/**
 * write_word_misaligned_lm: Write a misaligned word across two
 * words, in both level and mask buffers. This is core to the text
 * writing routines.
 *
 * @param       buff    buffer to write in
 * @param       word    word to write (16 bits)
 * @param       addr    address of first word
 * @param       xoff    x offset (0-15)
 * @param       lmode   0 = clear, 1 = set, 2 = toggle
 * @param       mmode   0 = clear, 1 = set, 2 = toggle
 */
void write_word_misaligned_lm(uint16_t wordl, uint16_t wordm, unsigned int addr, unsigned int xoff, int lmode, int mmode)
{
        write_word_misaligned(draw_buffer_level, wordl, addr, xoff, lmode);
        write_word_misaligned(draw_buffer_mask, wordm, addr, xoff, mmode);
}

/**
 * fetch_font_info: Fetch font info structs.
 *
 * @param       ch              character
 * @param       font    font id
 */
int fetch_font_info(char ch, int font, struct FontEntry *font_info, char *lookup)
{
        // First locate the font struct.
        if(font > SIZEOF_ARRAY(fonts))
                return 0; // font does not exist, exit.*/
        // Load the font info; IDs are always sequential.
        *font_info = fonts[font];
        // Locate character in font lookup table. (If required.)
        if(lookup != NULL)
        {
                *lookup = font_info->lookup[ch];
                if(*lookup == 0xff)
                        return 0; // character doesn't exist, don't bother writing it.
        }
        return 1;
}

/**
 * write_char: Draw a character on the current draw buffer.
 * Currently supports outlined characters and characters with
 * a width of up to 8 pixels.
 *
 * @param       ch              character to write
 * @param       x               x coordinate (left)
 * @param       y               y coordinate (top)
 * @param       flags   flags to write with (see gfx.h)
 * @param       font    font to use
 */
void write_char(char ch, unsigned int x, unsigned int y, int flags, int font)
{
    int yy, addr_temp, row, row_temp, xshift;
    uint16_t and_mask, or_mask, level_bits;
    struct FontEntry font_info;
    char lookup = 0;
    fetch_font_info(ch, font, &font_info, &lookup);
    // Compute starting address (for x,y) of character.
    int addr = CALC_BUFF_ADDR(x, y);
    int wbit = CALC_BIT_IN_WORD(x);
    // If font only supports lowercase or uppercase, make the letter
    // lowercase or uppercase.
    /*if(font_info.flags & FONT_LOWERCASE_ONLY)
            ch = tolower(ch);
    if(font_info.flags & FONT_UPPERCASE_ONLY)
            ch = toupper(ch);*/
    fetch_font_info(ch, font, &font_info, &lookup);
    // How big is the character? We handle characters up to 8 pixels
    // wide for now. Support for large characters may be added in future.
    if(font_info.width <= 8)
    {
            // Ensure we don't overflow.
            if(x + wbit > DISP_WIDTH)
                    return;
            // Load data pointer.
            row = lookup * font_info.height * 2;
            row_temp = row;
            addr_temp = addr;
            xshift = 16 - font_info.width;
            // We can write mask words easily.
            for(yy = y; yy < y + font_info.height; yy++)
            {
                    write_word_misaligned_OR(draw_buffer_mask, font_info.data[row] << xshift, addr, wbit);
                    addr += DISP_WIDTH / 16;
                    row++;
            }
            // Level bits are more complicated. We need to set or clear
            // level bits, but only where the mask bit is set; otherwise,
            // we need to leave them alone. To do this, for each word, we
            // construct an AND mask and an OR mask, and apply each individually.
            row = row_temp;
            addr = addr_temp;
            for(yy = y; yy < y + font_info.height; yy++)
            {
                    level_bits = font_info.data[row + font_info.height];
                    if(!(flags & FONT_INVERT)) // data is normally inverted
                            level_bits = ~level_bits;
                    or_mask = font_info.data[row] << xshift;
                    and_mask = (font_info.data[row] & level_bits) << xshift;
                    write_word_misaligned_OR(draw_buffer_level, or_mask, addr, wbit);
                    // If we're not bold write the AND mask.
                    //if(!(flags & FONT_BOLD))
                            write_word_misaligned_NAND(draw_buffer_level, and_mask, addr, wbit);
                    addr += DISP_WIDTH / 16;
                    row++;
            }
    }
}

/**
* calc_text_dimensions: Calculate the dimensions of a
* string in a given font. Supports new lines and
* carriage returns in text.
*
* @param       str                     string to calculate dimensions of
* @param       font_info       font info structure
* @param       xs                      horizontal spacing
* @param       ys                      vertical spacing
* @param       dim                     return result: struct FontDimensions
*/
void calc_text_dimensions(char *str, struct FontEntry font, int xs, int ys, struct FontDimensions *dim)
{
    int max_length = 0, line_length = 0, lines = 1;
    while(*str != 0)
    {
            line_length++;
            if(*str == '\n' || *str == '\r')
            {
                    if(line_length > max_length)
                            max_length = line_length;
                    line_length = 0;
                    lines++;
            }
            str++;
    }
    if(line_length > max_length)
            max_length = line_length;
    dim->width = max_length * (font.width + xs);
    dim->height = lines * (font.height + ys);
}

/**
* write_string: Draw a string on the screen with certain
* alignment parameters.
*
* @param       str             string to write
* @param       x               x coordinate
* @param       y               y coordinate
* @param       xs              horizontal spacing
* @param       ys              horizontal spacing
* @param       va              vertical align
* @param       ha              horizontal align
* @param       flags   flags (passed to write_char)
* @param       font    font
*/
void write_string(char *str, unsigned int x, unsigned int y, unsigned int xs, unsigned int ys, int va, int ha, int flags, int font)
{
    int xx = 0, yy = 0, xx_original = 0;
    struct FontEntry font_info;
    struct FontDimensions dim;
    // Determine font info and dimensions/position of the string.
    fetch_font_info(0, font, &font_info, NULL);
    calc_text_dimensions(str, font_info, xs, ys, &dim);
    switch(va)
    {
            case TEXT_VA_TOP:               yy = y; break;
            case TEXT_VA_MIDDLE:    yy = y - (dim.height / 2); break;
            case TEXT_VA_BOTTOM:    yy = y - dim.height; break;
    }
    switch(ha)
    {
            case TEXT_HA_LEFT:              xx = x; break;
            case TEXT_HA_CENTER:    xx = x - (dim.width / 2); break;
            case TEXT_HA_RIGHT:             xx = x - dim.width; break;
    }
    // Then write each character.
    xx_original = xx;
    while(*str != 0)
    {
            if(*str == '\n' || *str == '\r')
            {
                    yy += ys + font_info.height;
                    xx = xx_original;
            }
            else
            {
                    if(xx >= 0 && xx < DISP_WIDTH)
                            write_char(*str, xx, yy, flags, font);
                    xx += font_info.width + xs;
            }
            str++;
    }
}

/**
* write_string_formatted: Draw a string with format escape
* sequences in it. Allows for complex text effects.
*
* @param       str             string to write (with format data)
* @param       x               x coordinate
* @param       y               y coordinate
* @param       xs              default horizontal spacing
* @param       ys              default horizontal spacing
* @param       va              vertical align
* @param       ha              horizontal align
* @param       flags   flags (passed to write_char)
*/
void write_string_formatted(char *str, unsigned int x, unsigned int y, unsigned int xs, unsigned int ys, int va, int ha, int flags)
{
    int fcode = 0, fptr = 0, font = 0, fwidth = 0, fheight = 0, xx = x, yy = y, max_xx = 0, max_height = 0;
    struct FontEntry font_info;
    // Retrieve sizes of the fonts: bigfont and smallfont.
    fetch_font_info(0, 0, &font_info, NULL);
    int smallfontwidth = font_info.width, smallfontheight = font_info.height;
    fetch_font_info(0, 1, &font_info, NULL);
    int bigfontwidth = font_info.width, bigfontheight = font_info.height;
    // 11 byte stack with last byte as NUL.
    char fstack[11];
    fstack[10] = '\0';
    // First, we need to parse the string for format characters and
    // work out a bounding box. We'll parse again for the final output.
    // This is a simple state machine parser.
    char *ostr = str;
    while(*str)
    {
            if(*str == '<' && fcode == 1) // escape code: skip
                    fcode = 0;
            if(*str == '<' && fcode == 0) // begin format code?
            {
                    fcode = 1;
                    fptr = 0;
            }
            if(*str == '>' && fcode == 1)
            {
                    fcode = 0;
                    if(strcmp(fstack, "B")) // switch to "big" font (font #1)
                    {
                            fwidth = bigfontwidth;
                            fheight = bigfontheight;
                    }
                    else if(strcmp(fstack, "S")) // switch to "small" font (font #0)
                    {
                            fwidth = smallfontwidth;
                            fheight = smallfontheight;
                    }
                    if(fheight > max_height)
                            max_height = fheight;
                    // Skip over this byte. Go to next byte.
                    str++;
                    continue;
            }
            if(*str != '<' && *str != '>' && fcode == 1)
            {
                    // Add to the format stack (up to 10 bytes.)
                    if(fptr > 10) // stop adding bytes
                    {
                            str++; // go to next byte
                            continue;
                    }
                    fstack[fptr++] = *str;
                    fstack[fptr] = '\0'; // clear next byte (ready for next char or to terminate string.)
            }
            if(fcode == 0)
            {
                    // Not a format code, raw text.
                    xx += fwidth + xs;
                    if(*str == '\n')
                    {
                            if(xx > max_xx)
                                    max_xx = xx;
                            xx = x;
                            yy += fheight + ys;
                    }
            }
            str++;
    }
    // Reset string pointer.
    str = ostr;
    // Now we've parsed it and got a bbox, we need to work out the dimensions of it
    // and how to align it.
    int width = max_xx - x;
    int height = yy - y;
    int ay, ax;
    switch(va)
    {
            case TEXT_VA_TOP:               ay = yy; break;
            case TEXT_VA_MIDDLE:    ay = yy - (height / 2); break;
            case TEXT_VA_BOTTOM:    ay = yy - height; break;
    }
    switch(ha)
    {
            case TEXT_HA_LEFT:              ax = x; break;
            case TEXT_HA_CENTER:    ax = x - (width / 2); break;
            case TEXT_HA_RIGHT:             ax = x - width; break;
    }
    // So ax,ay is our new text origin. Parse the text format again and paint
    // the text on the display.
    fcode = 0;
    fptr = 0;
    font = 0;
    xx = 0;
    yy = 0;
    while(*str)
    {
            if(*str == '<' && fcode == 1) // escape code: skip
                    fcode = 0;
            if(*str == '<' && fcode == 0) // begin format code?
            {
                    fcode = 1;
                    fptr = 0;
            }
            if(*str == '>' && fcode == 1)
            {
                    fcode = 0;
                    if(strcmp(fstack, "B")) // switch to "big" font (font #1)
                    {
                            fwidth = bigfontwidth;
                            fheight = bigfontheight;
                            font = 1;
                    }
                    else if(strcmp(fstack, "S")) // switch to "small" font (font #0)
                    {
                            fwidth = smallfontwidth;
                            fheight = smallfontheight;
                            font = 0;
                    }
                    // Skip over this byte. Go to next byte.
                    str++;
                    continue;
            }
            if(*str != '<' && *str != '>' && fcode == 1)
            {
                    // Add to the format stack (up to 10 bytes.)
                    if(fptr > 10) // stop adding bytes
                    {
                            str++; // go to next byte
                            continue;
                    }
                    fstack[fptr++] = *str;
                    fstack[fptr] = '\0'; // clear next byte (ready for next char or to terminate string.)
            }
            if(fcode == 0)
            {
                    // Not a format code, raw text. So we draw it.
                    // TODO - different font sizes.
                    write_char(*str, xx, yy + (max_height - fheight), flags, font);
                    xx += fwidth + xs;
                    if(*str == '\n')
                    {
                            if(xx > max_xx)
                                    max_xx = xx;
                            xx = x;
                            yy += fheight + ys;
                    }
            }
            str++;
    }
}


//SUPEROSD-


// graphics

void drawAttitude(uint16_t x, uint16_t y, int16_t pitch, int16_t roll, uint16_t size)
{
	int16_t a = mySin(roll+360);
	int16_t b = myCos(roll+360);
	int16_t c = mySin(roll+90+360)*5/100;
	int16_t d = myCos(roll+90+360)*5/100;

	int16_t k;
	int16_t l;

	int16_t indi30x1=myCos(30)*(size/2+1) / 100;
	int16_t indi30y1=mySin(30)*(size/2+1) / 100;

	int16_t indi30x2=myCos(30)*(size/2+4) / 100;
	int16_t indi30y2=mySin(30)*(size/2+4) / 100;

	int16_t indi60x1=myCos(60)*(size/2+1) / 100;
	int16_t indi60y1=mySin(60)*(size/2+1) / 100;

	int16_t indi60x2=myCos(60)*(size/2+4) / 100;
	int16_t indi60y2=mySin(60)*(size/2+4) / 100;

	pitch=pitch%90;
	if(pitch>90)
	{
		pitch=pitch-90;
	}
	if(pitch<-90)
	{
		pitch=pitch+90;
	}
	a = (a * (size/2)) / 100;
	b = (b * (size/2)) / 100;

	if(roll<-90 || roll>90)
		pitch=pitch*-1;
	k = a*pitch/90;
	l = b*pitch/90;

	// scale
	//0
	//drawLine((x)-1-(size/2+4), (y)-1, (x)-1 - (size/2+1), (y)-1);
	//drawLine((x)-1+(size/2+4), (y)-1, (x)-1 + (size/2+1), (y)-1);
	write_line_outlined((x)-1-(size/2+4), (y)-1, (x)-1 - (size/2+1), (y)-1,0,0,0,1);
	write_line_outlined((x)-1+(size/2+4), (y)-1, (x)-1 + (size/2+1), (y)-1,0,0,0,1);

	//30
	//drawLine((x)-1+indi30x1, (y)-1-indi30y1, (x)-1 + indi30x2, (y)-1 - indi30y2);
	//drawLine((x)-1-indi30x1, (y)-1-indi30y1, (x)-1 - indi30x2, (y)-1 - indi30y2);
	write_line_outlined((x)-1+indi30x1, (y)-1-indi30y1, (x)-1 + indi30x2, (y)-1 - indi30y2,0,0,0,1);
	write_line_outlined((x)-1-indi30x1, (y)-1-indi30y1, (x)-1 - indi30x2, (y)-1 - indi30y2,0,0,0,1);
	//60
	//drawLine((x)-1+indi60x1, (y)-1-indi60y1, (x)-1 + indi60x2, (y)-1 - indi60y2);
	//drawLine((x)-1-indi60x1, (y)-1-indi60y1, (x)-1 - indi60x2, (y)-1 - indi60y2);
	write_line_outlined((x)-1+indi60x1, (y)-1-indi60y1, (x)-1 + indi60x2, (y)-1 - indi60y2,0,0,0,1);
	write_line_outlined((x)-1-indi60x1, (y)-1-indi60y1, (x)-1 - indi60x2, (y)-1 - indi60y2,0,0,0,1);
	//90
	//drawLine((x)-1, (y)-1-(size/2+4), (x)-1, (y)-1 - (size/2+1));
	write_line_outlined((x)-1, (y)-1-(size/2+4), (x)-1, (y)-1 - (size/2+1),0,0,0,1);


	//roll
	//drawLine((x)-1 - b, (y)-1 + a, (x)-1 + b, (y)-1 - a); //Direction line
	write_line_outlined((x)-1 - b, (y)-1 + a, (x)-1 + b, (y)-1 - a,0,0,0,1); //Direction line
	//"wingtips"
	//drawLine((x)-1 - b, (y)-1 + a, (x)-1 - b + d, (y)-1 + a - c);
	//drawLine((x)-1 + b + d, (y)-1 - a - c, (x)-1 + b, (y)-1 - a);
	write_line_outlined((x)-1 - b, (y)-1 + a, (x)-1 - b + d, (y)-1 + a - c,0,0,0,1);
	write_line_outlined((x)-1 + b + d, (y)-1 - a - c, (x)-1 + b, (y)-1 - a,0,0,0,1);

	//pitch
	//drawLine((x)-1, (y)-1, (x)-1 - k, (y)-1 - l);
	write_line_outlined((x)-1, (y)-1, (x)-1 - k, (y)-1 - l,0,0,0,1);


	//drawCircle(x-1, y-1, 5);
	//write_circle_outlined(x-1, y-1, 5,0,0,0,1);
	//drawCircle(x-1, y-1, size/2+4);
	//write_circle_outlined(x-1, y-1, size/2+4,0,0,0,1);
}

void drawBattery(uint16_t x, uint16_t y, uint8_t battery, uint16_t size)
{
	int i=0;
	int batteryLines;
	//top
	/*drawLine((x)-1+(size/2-size/4), (y)-1, (x)-1 + (size/2+size/4), (y)-1);
	drawLine((x)-1+(size/2-size/4), (y)-1+1, (x)-1 + (size/2+size/4), (y)-1+1);

	drawLine((x)-1, (y)-1+2, (x)-1 + size, (y)-1+2);
	//bottom
	drawLine((x)-1, (y)-1+size*3, (x)-1 + size, (y)-1+size*3);
	//left
	drawLine((x)-1, (y)-1+2, (x)-1, (y)-1+size*3);

	//right
	drawLine((x)-1+size, (y)-1+2, (x)-1+size, (y)-1+size*3);*/

	write_rectangle_outlined((x)-1, (y)-1+2,size,size*3,0,1);
	write_vline_lm((x)-1+(size/2+size/4)+1,(y)-2,(y)-1+1,0,1);
	write_vline_lm((x)-1+(size/2-size/4)-1,(y)-2,(y)-1+1,0,1);
	write_hline_lm((x)-1+(size/2-size/4),(x)-1 + (size/2+size/4),(y)-2,0,1);
	write_hline_lm((x)-1+(size/2-size/4),(x)-1 + (size/2+size/4),(y)-1,1,1);
	write_hline_lm((x)-1+(size/2-size/4),(x)-1 + (size/2+size/4),(y)-1+1,1,1);

	batteryLines = battery*(size*3-2)/100;
	for(i=0;i<batteryLines;i++)
	{
		write_hline_lm((x)-1,(x)-1 + size,(y)-1+size*3-i,1,1);
	}
}

void setAttitudeOsd(int16_t pitch, int16_t roll, int16_t yaw)
{
	m_pitch=pitch;
	m_roll=roll;
	m_yaw=yaw;
}

void setGpsOsd(uint8_t status, int32_t lat, int32_t lon, float alt, float spd)
{
	m_gpsStatus=status;
	m_gpsLat=lat;
	m_gpsLon=lon;
	m_gpsAlt=alt;
	m_gpsSpd=spd;
}

void introText(){
	//printTextFB((GRAPHICS_WIDTH_REAL/2 - 40)/16,GRAPHICS_HEIGHT_REAL-10,"ver 0.1");
	write_string("ver 0.2", (GRAPHICS_WIDTH_REAL/2),GRAPHICS_HEIGHT_REAL-18, 1, 0, TEXT_VA_TOP, TEXT_HA_CENTER, 0, 0);
}

void introGraphics() {
	/* logo */
	copyimage(GRAPHICS_WIDTH_REAL/2-128/2, GRAPHICS_HEIGHT_REAL/2-128/2);

	/* frame */
	drawBox(0,0,GRAPHICS_WIDTH_REAL-2,GRAPHICS_HEIGHT_REAL-1);
}

/*
void drawAltitude(uint16_t x, uint16_t y, int16_t alt, uint8_t dir) {

	char temp[9]={0};
	char updown=' ';
	uint16_t charx=x/16;
	if(dir==0)
		updown=24;
	if(dir==1)
		updown=25;
	sprintf(temp,"%c%6dm",updown,alt);
	printTextFB(charx,y+2,temp);
	/* frame *
	drawBox(charx*16-3,y,charx*16+strlen(temp)*8+3,y+11);
}*/

/**
 * hud_draw_vertical_scale: Draw a vertical scale.
 *
 * @param       v                               value to display as an integer
 * @param       range                   range about value to display (+/- range/2 each direction)
 * @param       halign                  horizontal alignment: -1 = left, +1 = right.
 * @param       height                  height of scale
 * @param       x                               x displacement (typ. 0)
 * @param       y                               y displacement (typ. half display height)
 * @param       mintick_step    how often a minor tick is shown
 * @param       majtick_step    how often a major tick is shown
 * @param       mintick_len             minor tick length
 * @param       majtick_len             major tick length
 * @param       max_val                 maximum expected value (used to compute size of arrow ticker)
 * @param       flags                   special flags (see hud.h.)
 */
void hud_draw_vertical_scale(int v, int range, int halign, int x, int y, int height, int mintick_step, int majtick_step, int mintick_len, int majtick_len, int boundtick_len, int max_val, int flags)
{
        char temp[15];//, temp2[15];
        struct FontEntry font_info;
        struct FontDimensions dim;
        // Halign should be in a small span.
        //MY_ASSERT(halign >= -1 && halign <= 1);
        // Compute the position of the elements.
        int majtick_start = 0, majtick_end = 0, mintick_start = 0, mintick_end = 0, boundtick_start = 0, boundtick_end = 0;
        if(halign == -1)
        {
                majtick_start = x;
                majtick_end = x + majtick_len;
                mintick_start = x;
                mintick_end = x + mintick_len;
                boundtick_start = x;
                boundtick_end = x + boundtick_len;
        }
        else if(halign == +1)
        {
                majtick_start = DISP_WIDTH - x - 1;
                majtick_end = DISP_WIDTH - x - majtick_len - 1;
                mintick_start = DISP_WIDTH - x - 1;
                mintick_end = DISP_WIDTH - x - mintick_len - 1;
                boundtick_start = DISP_WIDTH - x - 1;
                boundtick_end = DISP_WIDTH - x - boundtick_len - 1;
        }
        // Retrieve width of large font (font #0); from this calculate the x spacing.
        fetch_font_info(0, 0, &font_info, NULL);
        int arrow_len = (font_info.height / 2) + 1;             // FIXME, font info being loaded correctly??
        int text_x_spacing = arrow_len;
        int max_text_y = 0, text_length = 0;
        int small_font_char_width = font_info.width + 1; // +1 for horizontal spacing = 1
        // For -(range / 2) to +(range / 2), draw the scale.
        int range_2 = range / 2, height_2 = height / 2;
        int calc_ys = 0, r = 0, rr = 0, rv = 0, ys = 0, style = 0;
        // Iterate through each step.
        for(r = -range_2; r <= +range_2; r++)
        {
                style = 0;
                rr = r + range_2 - v; // normalise range for modulo, subtract value to move ticker tape
                rv = -rr + range_2; // for number display
                if(flags & HUD_VSCALE_FLAG_NO_NEGATIVE)
                        rr += majtick_step / 2;
                if(rr % majtick_step == 0)
                        style = 1; // major tick
                else if(rr % mintick_step == 0)
                        style = 2; // minor tick
                else
                        style = 0;
                if(flags & HUD_VSCALE_FLAG_NO_NEGATIVE && rv < 0)
                        continue;
                if(style)
                {
                        // Calculate y position.
                        ys = ((long int)(r * height) / (long int)range) + y;
                        //sprintf(temp, "ys=%d", ys);
                        //con_puts(temp, 0);
                        // Depending on style, draw a minor or a major tick.
                        if(style == 1)
                        {
                                write_hline_outlined(majtick_start, majtick_end, ys, 2, 2, 0, 1);
                                memset(temp, ' ', 10);
                                //my_itoa(rv, temp);
                                sprintf(temp,"%d",rv);
                                text_length = (strlen(temp) + 1) * small_font_char_width; // add 1 for margin
                                if(text_length > max_text_y)
                                        max_text_y = text_length;
                                if(halign == -1)
                                        write_string(temp, majtick_end + text_x_spacing, ys, 1, 0, TEXT_VA_MIDDLE, TEXT_HA_LEFT, 0, 1);
                                else
                                        write_string(temp, majtick_end - text_x_spacing + 1, ys, 1, 0, TEXT_VA_MIDDLE, TEXT_HA_RIGHT, 0, 1);
                        }
                        else if(style == 2)
                                write_hline_outlined(mintick_start, mintick_end, ys, 2, 2, 0, 1);
                }
        }
        // Generate the string for the value, as well as calculating its dimensions.
        memset(temp, ' ', 10);
        //my_itoa(v, temp);
        sprintf(temp,"%d",v);
        // TODO: add auto-sizing.
        calc_text_dimensions(temp, font_info, 1, 0, &dim);
        int xx = 0, i = 0;
        if(halign == -1)
                xx = majtick_end + text_x_spacing;
        else
                xx = majtick_end - text_x_spacing;
        // Draw an arrow from the number to the point.
        for(i = 0; i < arrow_len; i++)
        {
                if(halign == -1)
                {
                        write_pixel_lm(xx - arrow_len + i, y - i - 1, 1, 1);
                        write_pixel_lm(xx - arrow_len + i, y + i - 1, 1, 1);
                        write_hline_lm(xx + dim.width - 1, xx - arrow_len + i + 1, y - i - 1, 0, 1);
                        write_hline_lm(xx + dim.width - 1, xx - arrow_len + i + 1, y + i - 1, 0, 1);
                }
                else
                {
                        write_pixel_lm(xx + arrow_len - i, y - i - 1, 1, 1);
                        write_pixel_lm(xx + arrow_len - i, y + i - 1, 1, 1);
                        write_hline_lm(xx - dim.width - 1, xx + arrow_len - i - 1, y - i - 1, 0, 1);
                        write_hline_lm(xx - dim.width - 1, xx + arrow_len - i - 1, y + i - 1, 0, 1);
                }
                // FIXME
                // write_hline_lm(xx - dim.width - 1, xx + (arrow_len - i), y - i - 1, 1, 1);
                // write_hline_lm(xx - dim.width - 1, xx + (arrow_len - i), y + i - 1, 1, 1);
        }
        if(halign == -1)
        {
                write_hline_lm(xx, xx + dim.width - 1, y - arrow_len, 1, 1);
                write_hline_lm(xx, xx + dim.width - 1, y + arrow_len - 2, 1, 1);
                write_vline_lm(xx + dim.width - 1, y - arrow_len, y + arrow_len - 2, 1, 1);
        }
        else
        {
                write_hline_lm(xx, xx - dim.width - 1, y - arrow_len, 1, 1);
                write_hline_lm(xx, xx - dim.width - 1, y + arrow_len - 2, 1, 1);
                write_vline_lm(xx - dim.width - 1, y - arrow_len, y + arrow_len - 2, 1, 1);
        }
        // Draw the text.
        if(halign == -1)
                write_string(temp, xx, y, 1, 0, TEXT_VA_MIDDLE, TEXT_HA_LEFT, 0, 0);
        else
                write_string(temp, xx, y, 1, 0, TEXT_VA_MIDDLE, TEXT_HA_RIGHT, 0, 0);
        // Then, add a slow cut off on the edges, so the text doesn't sharply
        // disappear. We simply clear the areas above and below the ticker, and we
        // use little markers on the edges.
        if(halign == -1)
        {
                write_filled_rectangle_lm(majtick_end + text_x_spacing, y + (height / 2) - (font_info.height / 2), max_text_y - boundtick_start, font_info.height, 0, 0);
                write_filled_rectangle_lm(majtick_end + text_x_spacing, y - (height / 2) - (font_info.height / 2), max_text_y - boundtick_start, font_info.height, 0, 0);
        }
        else
        {
                write_filled_rectangle_lm(majtick_end - text_x_spacing - max_text_y, y + (height / 2) - (font_info.height / 2), max_text_y, font_info.height, 0, 0);
                write_filled_rectangle_lm(majtick_end - text_x_spacing - max_text_y, y - (height / 2) - (font_info.height / 2), max_text_y, font_info.height, 0, 0);
        }
        write_hline_outlined(boundtick_start, boundtick_end, y + (height / 2), 2, 2, 0, 1);
        write_hline_outlined(boundtick_start, boundtick_end, y - (height / 2), 2, 2, 0, 1);
}

/**
 * hud_draw_compass: Draw a compass.
 *
 * @param       v                               value for the compass
 * @param       range                   range about value to display (+/- range/2 each direction)
 * @param       width                   length in pixels
 * @param       x                               x displacement (typ. half display width)
 * @param       y                               y displacement (typ. bottom of display)
 * @param       mintick_step    how often a minor tick is shown
 * @param       majtick_step    how often a major tick (heading "xx") is shown
 * @param       mintick_len             minor tick length
 * @param       majtick_len             major tick length
 * @param       flags                   special flags (see hud.h.)
 */
void hud_draw_linear_compass(int v, int range, int width, int x, int y, int mintick_step, int majtick_step, int mintick_len, int majtick_len, int flags)
{
        v %= 360; // wrap, just in case.
        struct FontEntry font_info;
        int majtick_start = 0, majtick_end = 0, mintick_start = 0, mintick_end = 0, textoffset = 0;
        char headingstr[4];
        majtick_start = y;
        majtick_end = y - majtick_len;
        mintick_start = y;
        mintick_end = y - mintick_len;
        textoffset = 8;
        int r, style, rr, rv, xs;
        int range_2 = range / 2;
        for(r = -range_2; r <= +range_2; r++)
        {
                style = 0;
                rr = (v + r + 360) % 360; // normalise range for modulo, add to move compass track
                rv = -rr + range_2; // for number display
                if(rr % majtick_step == 0)
                        style = 1; // major tick
                else if(rr % mintick_step == 0)
                        style = 2; // minor tick
                if(style)
                {
                        // Calculate x position.
                        xs = ((long int)(r * width) / (long int)range) + x;
                        // Draw it.
                        if(style == 1)
                        {
                                write_vline_outlined(xs, majtick_start, majtick_end, 2, 2, 0, 1);
                                // Draw heading above this tick.
                                // If it's not one of north, south, east, west, draw the heading.
                                // Otherwise, draw one of the identifiers.
                                if(rr % 90 != 0)
                                {
                                        // We abbreviate heading to two digits. This has the side effect of being easy to compute.
                                        headingstr[0] = '0' + (rr / 100);
                                        headingstr[1] = '0' + ((rr / 10) % 10);
                                        headingstr[2] = 0;
                                        headingstr[3] = 0; // nul to terminate
                                }
                                else
                                {
                                        switch(rr)
                                        {
                                                case 0:   headingstr[0] = 'N'; break;
                                                case 90:  headingstr[0] = 'E'; break;
                                                case 180: headingstr[0] = 'S'; break;
                                                case 270: headingstr[0] = 'W'; break;
                                        }
                                        headingstr[1] = 0;
                                        headingstr[2] = 0;
                                        headingstr[3] = 0;
                                }
                                // +1 fudge...!
                                write_string(headingstr, xs + 1, majtick_start + textoffset, 1, 0, TEXT_VA_MIDDLE, TEXT_HA_CENTER, 0, 1);
                        }
                        else if(style == 2)
                                write_vline_outlined(xs, mintick_start, mintick_end, 2, 2, 0, 1);
                }
        }
        // Then, draw a rectangle with the present heading in it.
        // We want to cover up any other markers on the bottom.
        // First compute font size.
        fetch_font_info(0, 0, &font_info, NULL);
        int text_width = (font_info.width + 1) * 3;
        int rect_width = text_width + 2;
        write_filled_rectangle_lm(x - (rect_width / 2), majtick_start + 2, rect_width, font_info.height + 2, 0, 1);
        write_rectangle_outlined(x - (rect_width / 2), majtick_start + 2, rect_width, font_info.height + 2, 0, 1);
        headingstr[0] = '0' + (v / 100);
        headingstr[1] = '0' + ((v / 10) % 10);
        headingstr[2] = '0' + (v % 10);
        headingstr[3] = 0;
        write_string(headingstr, x + 1, majtick_start + textoffset+2, 1, 0, TEXT_VA_MIDDLE, TEXT_HA_CENTER, 1, 0);
}


void calcHomeArrow(void)
{
	HomeLocationData home;
	HomeLocationGet (&home);
	GPSPositionData gpsData;
	GPSPositionGet (&gpsData);

	/** http://www.movable-type.co.uk/scripts/latlong.html **/
    float lat1, lat2, lon1, lon2, a, c, d, x, y, brng, u2g;
    float elevation;
    float gcsAlt=home.Altitude; // Home MSL altitude
    float uavAlt=gpsData.Altitude; // UAV MSL altitude
    float dAlt=uavAlt-gcsAlt; // Altitude difference

    // Convert to radians
    lat1 = DEG2RAD(home.Latitude)/10000000.0f; // Home lat
    lon1 = DEG2RAD(home.Longitude)/10000000.0f; // Home lon
    lat2 = DEG2RAD(gpsData.Latitude)/10000000.0f; // UAV lat
    lon2 = DEG2RAD(gpsData.Longitude)/10000000.0f; // UAV lon

    // Bearing
    /**
    var y = Math.sin(dLon) * Math.cos(lat2);
    var x = Math.cos(lat1)*Math.sin(lat2) -
            Math.sin(lat1)*Math.cos(lat2)*Math.cos(dLon);
    var brng = Math.atan2(y, x).toDeg();
    **/
    y = sinf(lon2-lon1) * cosf(lat2);
    x = cosf(lat1) * sinf(lat2) - sinf(lat1) * cosf(lat2) * cosf(lon2-lon1);
    brng = RAD2DEG(atan2f((sinf(lon2-lon1)*cosf(lat2)),(cosf(lat1)*sinf(lat2)-sinf(lat1)*cosf(lat2)*cosf(lon2-lon1))));
    if(brng<0)
        brng+=360;

    // yaw corrected bearing, needs compass
    u2g=brng-180-m_yaw;
    if(u2g<0)
    	u2g+=360;

    // Haversine formula for distance
    /**
    var R = 6371; // km
    var dLat = (lat2-lat1).toRad();
    var dLon = (lon2-lon1).toRad();
    var a = Math.sin(dLat/2) * Math.sin(dLat/2) +
            Math.cos(lat1.toRad()) * Math.cos(lat2.toRad()) *
            Math.sin(dLon/2) * Math.sin(dLon/2);
    var c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a));
    var d = R * c;
    **/
    a = sinf((lat2-lat1)/2) * sinf((lat2-lat1)/2) +
            cosf(lat1) * cosf(lat2) *
            sinf((lon2-lon1)/2) * sinf((lon2-lon1)/2);
    c = 2 * atan2f(sqrtf(a), sqrtf(1-a));
    d = 6371 * 1000 * c;

    // Elevation  v depends servo direction
    if(d!=0)
        elevation = 90-RAD2DEG(atanf(dAlt/d));
    else
        elevation = 0;
    //! TODO: sanity check

	char temp[50]={0};
	memset(temp, ' ', 40);
	sprintf(temp,"hea:%d",(int)brng);
	write_string(temp, 130, 10, 1, 0, TEXT_VA_TOP, TEXT_HA_LEFT, 0, 0);
	sprintf(temp,"ele:%d",(int)elevation);
	write_string(temp, 130, 10+14, 1, 0, TEXT_VA_TOP, TEXT_HA_LEFT, 0, 0);
	sprintf(temp,"dis:%d",(int)d);
	write_string(temp, 130, 10+14+14, 1, 0, TEXT_VA_TOP, TEXT_HA_LEFT, 0, 0);
	sprintf(temp,"u2g:%d",(int)u2g);
	write_string(temp, 130, 10+14+14+14, 1, 0, TEXT_VA_TOP, TEXT_HA_LEFT, 0, 0);

	sprintf(temp,"%c%c",(int)(u2g/22.5f)*2+0x90,(int)(u2g/22.5f)*2+0x91);
	printText16(200, 10+14+14,temp);

}

//main draw function
void updateGraphics() {

	OsdSettingsData OsdSettings;
	OsdSettingsGet (&OsdSettings);
	/*drawBox(2,2,GRAPHICS_WIDTH_REAL-4,GRAPHICS_HEIGHT_REAL-4);
	write_filled_rectangle(draw_buffer_mask,0,0,GRAPHICS_WIDTH_REAL-2,GRAPHICS_HEIGHT_REAL-2,0);
	write_filled_rectangle(draw_buffer_mask,2,2,GRAPHICS_WIDTH_REAL-4-2,GRAPHICS_HEIGHT_REAL-4-2,2);
	write_filled_rectangle(draw_buffer_mask,3,3,GRAPHICS_WIDTH_REAL-4-1,GRAPHICS_HEIGHT_REAL-4-1,0);*/
	//write_filled_rectangle(draw_buffer_mask,5,5,GRAPHICS_WIDTH_REAL-4-5,GRAPHICS_HEIGHT_REAL-4-5,0);
	//write_rectangle_outlined(10,10,GRAPHICS_WIDTH_REAL-20,GRAPHICS_HEIGHT_REAL-20,0,0);
	//drawLine(GRAPHICS_WIDTH_REAL-1, GRAPHICS_HEIGHT_REAL-1,(GRAPHICS_WIDTH_REAL/2)-1, GRAPHICS_HEIGHT_REAL-1 );
	//drawCircle((GRAPHICS_WIDTH_REAL/2)-1, (GRAPHICS_HEIGHT_REAL/2)-1, (GRAPHICS_HEIGHT_REAL/2)-1);
	//drawCircle((GRAPHICS_SIZE/2)-1, (GRAPHICS_SIZE/2)-1, (GRAPHICS_SIZE/2)-2);
	//drawLine(0, (GRAPHICS_SIZE/2)-1, GRAPHICS_SIZE-1, (GRAPHICS_SIZE/2)-1);
	//drawLine((GRAPHICS_SIZE/2)-1, 0, (GRAPHICS_SIZE/2)-1, GRAPHICS_SIZE-1);
	angleA++;
	if(angleB<=-90)
	{
	sum=2;
	}
	if(angleB>=90)
	{
	sum=-2;
	}
	angleB+=sum;
	angleC+=2;

	calcHomeArrow();

	/* Draw Attitude Indicator */
	if(OsdSettings.Attitude == OSDSETTINGS_ATTITUDE_ENABLED)
	{
		drawAttitude(OsdSettings.AttitudeSetup[OSDSETTINGS_ATTITUDESETUP_X],OsdSettings.AttitudeSetup[OSDSETTINGS_ATTITUDESETUP_Y],m_pitch,m_roll,96);
	}
	//write_string("Hello OP-OSD", 60, 12, 1, 0, TEXT_VA_TOP, TEXT_HA_LEFT, 0, 0);
	//printText16( 60, 12,"Hello OP-OSD");

	char temp[50]={0};
	memset(temp, ' ', 40);
	sprintf(temp,"Lat:%d",(int)m_gpsLat);
	write_string(temp, 5, 10, 1, 0, TEXT_VA_TOP, TEXT_HA_LEFT, 0, 0);
	sprintf(temp,"Lon:%d",(int)m_gpsLon);
	write_string(temp, 5, 10+14, 1, 0, TEXT_VA_TOP, TEXT_HA_LEFT, 0, 0);
	sprintf(temp,"Fix:%d",(int)m_gpsStatus);
	write_string(temp, 5, 10+14+14, 1, 0, TEXT_VA_TOP, TEXT_HA_LEFT, 0, 0);


	/* Print RTC time */
	if(OsdSettings.Time == OSDSETTINGS_TIME_ENABLED)
	{
		printTime(OsdSettings.TimeSetup[OSDSETTINGS_TIMESETUP_X],OsdSettings.TimeSetup[OSDSETTINGS_TIMESETUP_Y]);
	}

	/* Print Number of detected video Lines */
	sprintf(temp,"Lines:%4d",PIOS_Video_GetOSDLines());
	write_string(temp, (GRAPHICS_WIDTH_REAL - 2),10, 1, 0, TEXT_VA_TOP, TEXT_HA_RIGHT, 0, 0);

	/* Print ADC voltage */
	sprintf(temp,"Rssi:%4dV",(int)(PIOS_ADC_PinGet(4)*3000/4096));
	write_string(temp, (GRAPHICS_WIDTH_REAL - 2),24, 1, 0, TEXT_VA_TOP, TEXT_HA_RIGHT, 0, 0);

	/* Print CPU temperature */
	sprintf(temp,"Temp:%4dC",(int)(PIOS_ADC_PinGet(5)*0.29296875f-279));
	write_string(temp, (GRAPHICS_WIDTH_REAL - 2),38, 1, 0, TEXT_VA_TOP, TEXT_HA_RIGHT, 0, 0);


	/* Draw Battery Gauge */
	m_batt++;
	uint8_t dir=3;
	if(m_batt==101)
		m_batt=0;
	if(m_pitch>0)
	{
		dir=0;
		m_alt+=m_pitch/2;
	}
	else if(m_pitch<0)
	{
		dir=1;
		m_alt+=m_pitch/2;
	}
	if(OsdSettings.Battery == OSDSETTINGS_BATTERY_ENABLED)
	{
		drawBattery(OsdSettings.BatterySetup[OSDSETTINGS_BATTERYSETUP_X],OsdSettings.BatterySetup[OSDSETTINGS_BATTERYSETUP_Y],m_batt,16);
	}

	//drawAltitude(200,50,m_alt,dir);


	//drawArrow(96,GRAPHICS_HEIGHT_REAL/2,angleB,32);

    // Draw airspeed (left side.)
	if(OsdSettings.Speed == OSDSETTINGS_SPEED_ENABLED)
	{
	    hud_draw_vertical_scale((int)m_gpsSpd, 100, -1, OsdSettings.SpeedSetup[OSDSETTINGS_SPEEDSETUP_X],
	    		OsdSettings.SpeedSetup[OSDSETTINGS_SPEEDSETUP_Y], 100, 10, 20, 7, 12, 15, 1000, HUD_VSCALE_FLAG_NO_NEGATIVE);
	}
    // Draw altimeter (right side.)
	if(OsdSettings.Altitude == OSDSETTINGS_ALTITUDE_ENABLED)
	{
		hud_draw_vertical_scale((int)m_gpsAlt, 200, +1, OsdSettings.AltitudeSetup[OSDSETTINGS_ALTITUDESETUP_X],
				OsdSettings.AltitudeSetup[OSDSETTINGS_ALTITUDESETUP_Y], 100, 20, 100, 7, 12, 15, 500, 0);
	}
    // Draw compass.
	if(OsdSettings.Heading == OSDSETTINGS_HEADING_ENABLED)
	{
		if(m_yaw<0) {
			hud_draw_linear_compass(360+m_yaw, 150, 120, OsdSettings.HeadingSetup[OSDSETTINGS_HEADINGSETUP_X],
					OsdSettings.HeadingSetup[OSDSETTINGS_HEADINGSETUP_Y], 15, 30, 7, 12, 0);
		} else {
			hud_draw_linear_compass(m_yaw, 150, 120, OsdSettings.HeadingSetup[OSDSETTINGS_HEADINGSETUP_X],
					OsdSettings.HeadingSetup[OSDSETTINGS_HEADINGSETUP_Y], 15, 30, 7, 12, 0);
		}
	}
	//write_filled_rectangle(draw_buffer_level,20,20,30,30,1);
	//write_filled_rectangle(draw_buffer_mask,30,30,30,30,1);

    /* Make sure every line last bit is 0 */
	write_vline( draw_buffer_level,GRAPHICS_WIDTH_REAL-1,0,GRAPHICS_HEIGHT_REAL-1,0);
	write_vline( draw_buffer_mask,GRAPHICS_WIDTH_REAL-1,0,GRAPHICS_HEIGHT_REAL-1,0);

}


void updateOnceEveryFrame() {
	clearGraphics();
	updateGraphics();
}


// ****************
/**
 * Initialise the gps module
 * \return -1 if initialisation failed
 * \return 0 on success
 */

int32_t osdgenStart(void)
{
	// Start gps task
	vSemaphoreCreateBinary( osdSemaphore);
	xTaskCreate(osdgenTask, (signed char *)"OSDGEN", STACK_SIZE_BYTES/4, NULL, TASK_PRIORITY, &osdgenTaskHandle);
	TaskMonitorAdd(TASKINFO_RUNNING_GPS, osdgenTaskHandle);

	return 0;
}

/**
 * Initialise the osd module
 * \return -1 if initialisation failed
 * \return 0 on success
 */
int32_t osdgenInitialize(void)
{
	AttitudeActualInitialize();
#ifdef PIOS_INCLUDE_GPS
	GPSPositionInitialize();
#if !defined(PIOS_GPS_MINIMAL)
	GPSTimeInitialize();
	GPSSatellitesInitialize();
#endif
#ifdef PIOS_GPS_SETS_HOMELOCATION
	HomeLocationInitialize();
#endif
#endif
	OsdSettingsInitialize();

	return 0;
}
MODULE_INITCALL(osdgenInitialize, osdgenStart)

// ****************
/**
 * Main osd task. It does not return.
 */

static void osdgenTask(void *parameters)
{
	portTickType lastSysTime;
	AttitudeActualData attitude;
	GPSPositionData gpsData;
	// Loop forever
	lastSysTime = xTaskGetTickCount();

	// intro
	for(int i=0; i<125; i++)
	{
        if( xSemaphoreTake( osdSemaphore, LONG_TIME ) == pdTRUE )
        {
			clearGraphics();
			introGraphics();
        }
	}
	for(int i=0; i<125; i++)
	{
        if( xSemaphoreTake( osdSemaphore, LONG_TIME ) == pdTRUE )
        {
			clearGraphics();
			introGraphics();
			introText();
        }
	}

	while (1)
	{
        if( xSemaphoreTake( osdSemaphore, LONG_TIME ) == pdTRUE )
        {
			GPSPositionGet(&gpsData);
			AttitudeActualGet(&attitude);
			setAttitudeOsd((int16_t)attitude.Pitch,(int16_t)attitude.Roll,(int16_t)attitude.Yaw);
			setGpsOsd(gpsData.Status,gpsData.Latitude,gpsData.Longitude,gpsData.Altitude,gpsData.Groundspeed);
			updateOnceEveryFrame();
        }
		//xSemaphoreTake(osdSemaphore, portMAX_DELAY);
		//vTaskDelayUntil(&lastSysTime, 10 / portTICK_RATE_MS);
	}
}


// ****************

/**
  * @}
  * @}
  */
