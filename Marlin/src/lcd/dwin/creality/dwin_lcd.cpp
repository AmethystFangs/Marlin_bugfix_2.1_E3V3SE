/**
 *Marlin 3D Printer Firmware
 *Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 *Based on Sprinter and grbl.
 *Copyright (c) 2011 Camiel Gubbels /Erik van der Zalm
 *
 *This program is free software: you can redistribute it and/or modify
 *it under the terms of the GNU General Public License as published by
 *the Free Software Foundation, either version 3 of the License, or
 *(at your option) any later version.
 *
 *This program is distributed in the hope that it will be useful,
 *but WITHOUT ANY WARRANTY; without even the implied warranty of
 *MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *GNU General Public License for more details.
 *
 *You should have received a copy of the GNU General Public License
 *along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

/********************************************************************************
 * @file     dwin_lcd.cpp
 * @author   LEO /Creality3D
 * @date     2019/07/18
 * @version  2.0.1
 * @brief    DWIN screen control functions
 ********************************************************************************/

#include "../../../inc/MarlinConfigPre.h"
#include "../../../inc/MarlinConfig.h"
#if ENABLED(DWIN_CREALITY_LCD)


#include "dwin.h"
#include "ui_position.h"
#include "../../marlinui.h"

#include "dwin_lcd.h"
#include <string.h> // for memset

//#define DEBUG_OUT 1
#include "../../../core/debug_out.h"

// Make sure DWIN_SendBuf is large enough to hold the largest string plus draw command and tail.
// Assume the narrowest (6 pixel) font and 2-byte gb2312-encoded characters.
uint8_t DWIN_SendBuf[11 + DWIN_WIDTH / 6 * 2] = { 0xAA };
uint8_t DWIN_BufTail[4] = { 0xCC, 0x33, 0xC3, 0x3C };
uint8_t databuf[26] = { 0 };
uint8_t receivedType;
char my_short_fn[13] = { 0 };

int recnum = 0;

inline void DWIN_Byte(size_t &i, const uint16_t bval) {
  DWIN_SendBuf[++i] = bval;
}

inline void DWIN_Word(size_t &i, const uint16_t wval) {
  DWIN_SendBuf[++i] = wval >> 8;
  DWIN_SendBuf[++i] = wval & 0xFF;
}

inline void DWIN_Long(size_t &i, const uint32_t lval) {
  DWIN_SendBuf[++i] = (lval >> 24) & 0xFF;
  DWIN_SendBuf[++i] = (lval >> 16) & 0xFF;
  DWIN_SendBuf[++i] = (lval >>  8) & 0xFF;
  DWIN_SendBuf[++i] = lval & 0xFF;
}

inline void DWIN_String(size_t &i, char * const string) {
  const size_t len = _MIN(sizeof(DWIN_SendBuf) - i, strlen(string));
  memcpy(&DWIN_SendBuf[i+1], string, len);
  i += len;
}

inline void DWIN_String(size_t &i, const __FlashStringHelper * string) {
  if (!string) return;
  const size_t len = strlen_P((PGM_P)string); // cast it to PGM_P, which is basically const char *, and measure it using the _P version of strlen.
  if (len == 0) return;
  memcpy(&DWIN_SendBuf[i+1], string, len);
  i += len;
}

// Send the data in the buffer and the packet end
inline void DWIN_Send(size_t &i) {
  ++i;
  LOOP_L_N(n, i) { LCD_SERIAL.write(DWIN_SendBuf[n]); delayMicroseconds(1); }
  LOOP_L_N(n, 4) { LCD_SERIAL.write(DWIN_BufTail[n]); delayMicroseconds(1); }
}

/*--------------------------------------System variable function --------------------------------------*/

// Handshake (1: Success, 0: Fail)
bool DWIN_Handshake(void)
{
  #ifndef LCD_BAUDRATE
    #define LCD_BAUDRATE 115200
  #endif
  LCD_SERIAL.begin(LCD_BAUDRATE);
  const millis_t serial_connect_timeout = millis() + 1000UL;
  while (!LCD_SERIAL.connected() && PENDING(millis(), serial_connect_timeout)) { /*Nothing*/ }

  size_t i = 0;
  DWIN_Byte(i, 0x00);
  DWIN_Send(i);

  while (LCD_SERIAL.available() > 0 && recnum < (signed)sizeof(databuf))
  {
    databuf[recnum] = LCD_SERIAL.read();
    // ignore the invalid data
    if (databuf[0] != FHONE)
    {
      // prevent the program from running.
      if (recnum > 0)
      {
        recnum = 0;
        ZERO(databuf);
      }
      continue;
    }
    delay(10);
    recnum++;
  }

  return ( recnum >= 3
        && databuf[0] == FHONE
        && databuf[1] == '\0'
        && databuf[2] == 'O'
        && databuf[3] == 'K' );
}

// Set the backlight luminance
//  luminance: (0x00-0xFF)
void DWIN_Backlight_SetLuminance(const uint8_t luminance) 
{
  size_t i = 0;
  DWIN_Byte(i, 0x5f);
  DWIN_Byte(i, luminance); //_MAX(luminance, 0x1F));
  DWIN_Send(i);
}

// Set screen display direction
//  dir: 0=0°, 1=90°, 2=180°, 3=270°
void DWIN_Frame_SetDir(uint8_t dir)
{
  /*
    size_t i = 0;
    DWIN_Byte(i, 0x34);
    DWIN_Byte(i, 0x5A);
    DWIN_Byte(i, 0xA5);
    DWIN_Byte(i, dir);
    DWIN_Send(i);
  */
}

// Update display
void DWIN_UpdateLCD(void)
{
  size_t i = 0;
  DWIN_Byte(i, 0x3D);
  DWIN_Send(i);
}

/*----------------------------------------Drawing functions ----------------------------------------*/

// Clear screen
//  color: Clear screen color
void DWIN_Frame_Clear(const uint16_t color) {
  size_t i = 0;
  DWIN_Byte(i, 0x01);
  DWIN_Word(i, color);
  DWIN_Send(i);
}

// Draw a point
//  width: point width   0x01-0x0F
//  height: point height 0x01-0x0F
//  x,y: upper left point
void DWIN_Draw_Point(uint8_t width, uint8_t height, uint16_t x, uint16_t y) {
  size_t i = 0;
  DWIN_Byte(i, 0x02);
  DWIN_Byte(i, width);
  DWIN_Byte(i, height);
  DWIN_Word(i, x);
  DWIN_Word(i, y);
  DWIN_Send(i);
}
void DWIN_Set_Color(uint16_t FC,uint16_t BC)
{
  size_t i = 0;
  DWIN_Byte(i, 0x40);
  DWIN_Word(i, FC);  //foreground color
  DWIN_Word(i, BC);  //background color
  DWIN_Send(i);
}

void DWIN_Set_24_Color(uint32_t BC)
{
  size_t i = 0;
  DWIN_Byte(i, 0x40); 
  DWIN_Byte(i, BC>>16); //background color
  DWIN_Byte(i, BC>>8);
  DWIN_Byte(i, BC);
  DWIN_Byte(i, 255);  //foreground color
  DWIN_Byte(i, 255);
  DWIN_Byte(i, 255);
  DWIN_Send(i);
}

// Draw a line
//  color: Line segment color
//  xStart/yStart: Start point
//  xEnd/yEnd: End point
void DWIN_Draw_Line(uint16_t color, uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd) 
{
  size_t i = 0;
  DWIN_Set_Color(color, 0xffff);
  DWIN_Byte(i, 0x56);
  DWIN_Word(i, xStart);
  DWIN_Word(i, yStart);
  DWIN_Word(i, xEnd);
  DWIN_Word(i, yEnd);
  DWIN_Send(i);
}

// Fill a rectangle (opcode 0x5B) with whatever color/background was last set
// via DWIN_Set_Color. Split out of DWIN_Draw_Rectangle's mode==1 case so a
// caller drawing many same-colored rects (e.g. a bucketed image blit) can
// set the palette once and send many of these, instead of one 0x40+0x5B
// pair per rect.
void DWIN_Fill_Rect_Raw(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd) {
  if (xEnd >= DWIN_WIDTH)
    xEnd = DWIN_WIDTH - 1;
  size_t i = 0;
  DWIN_Byte(i, 0x5B);
  DWIN_Word(i, xStart);
  DWIN_Word(i, yStart);
  DWIN_Word(i, xEnd);
  DWIN_Word(i, yEnd);
  DWIN_Send(i);
}

// Draw a rectangle
//  mode: 0=frame, 1=fill, 2=XOR fill
//  color: Rectangle color
//  xStart/yStart: upper left point
//  xEnd/yEnd: lower right point
void DWIN_Draw_Rectangle(uint8_t mode, uint16_t color,
                         uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd)
{
  if (mode == 1) {
    DWIN_Set_Color(color, 0xffff);
    DWIN_Fill_Rect_Raw(xStart, yStart, xEnd, yEnd);
    return;
  }

  size_t i = 0;
  uint8_t temp_mode = 0;
  switch (mode)
  {
    case 0:
      temp_mode = 0x59;
      break;
     case 2:
      temp_mode = 0x69; //Background color displays rectangular area
      break;
     case 3:
      temp_mode = 0x5A; //Background color fills rectangular area
      break;
    default:
      break;
  }
  if(xEnd >= DWIN_WIDTH)
    xEnd = DWIN_WIDTH - 1;
  DWIN_Set_Color(color,0xffff);
  i = 0;
  DWIN_Byte(i, temp_mode);
  DWIN_Word(i, xStart);
  DWIN_Word(i, yStart);
  DWIN_Word(i, xEnd);
  DWIN_Word(i, yEnd);
  DWIN_Send(i);
}

// Move a screen area
//  mode: 0, circle shift; 1, translation
//  dir: 0=left, 1=right, 2=up, 3=down
//  dis: Distance
//  color: Fill color
//  xStart/yStart: upper left point
//  xEnd/yEnd: bottom right point
void DWIN_Frame_AreaMove(uint8_t mode, uint8_t dir, uint16_t dis,
                         uint16_t color, uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd) {
  size_t i = 0;
  if(xEnd==DWIN_WIDTH) xEnd-=1;
  DWIN_Byte(i, 0x09);
  DWIN_Byte(i, (mode << 7) | dir);
  DWIN_Word(i, dis);
  DWIN_Word(i, color);
  DWIN_Word(i, xStart);
  DWIN_Word(i, yStart);
  DWIN_Word(i, xEnd);
  DWIN_Word(i, yEnd);
  DWIN_Send(i);
}

// uint16_t color,uint8_t width,uint8_t x_step,uint16_t y_ratio channel fixed XsYs XeYe color thickness X step Y=0 position Y data ratio
// AA 84 00 FF 0010 0020 0074 00C8 00F800 01 05 00C8 0100 CC33C33C 
void Draw_Curve_Set(uint8_t line_wide,uint8_t step_x,uint16_t step_y,uint32_t colour)
{
  size_t i = 0;
  DWIN_Byte(i, 0x84);
  DWIN_Byte(i, 0x00);
  DWIN_Byte(i, 0xFF);
  DWIN_Word(i, Curve_Psition_Start_X); //Left up x
  DWIN_Word(i, Curve_Psition_Start_Y); //Left up y
  DWIN_Word(i, Curve_Psition_End_X);  //Right down x
  DWIN_Word(i, Curve_Psition_End_Y); //Right down y

  DWIN_Byte(i, colour>>16);
  DWIN_Word(i, colour);    //color

  DWIN_Byte(i,line_wide);  //Thickness
  DWIN_Byte(i, step_x);   //X step length
  DWIN_Word(i, Curve_Zero_Y);
  DWIN_Word(i, step_y);
  DWIN_Send(i);
}

void Draw_Curve_Data(uint8_t index,int16_t* temp_data)
{
  size_t i = 0;
  DWIN_Byte(i, 0x84);
  DWIN_Byte(i, 0x00);//aisle
  DWIN_Byte(i, 0x00);//Fixed
  //data
  for(uint8_t num=0;num<index;num++)
  {
    DWIN_Word(i, *(temp_data+num));
  }
  DWIN_Send(i);
}
/*----------------------------------------Text related functions ----------------------------------------*/

// Draw a string
//  widthAdjust: true=self-adjust character width; false=no adjustment
//  bShow: true=display background color; false=don't display background color
//  size: Font size
//  color: Character color
//  bColor: Background color
//  x/y: Upper-left coordinate of the string
//  *string: The string
void DWIN_Draw_String(bool widthAdjust, bool bShow, uint8_t size,
                      uint16_t color, uint16_t bColor, uint16_t x, uint16_t y, char *string)
{
  size_t i = 0;
  char mode;
  if(size == font10x20)
  {
    size = font8x16;
  }
  DWIN_Byte(i, 0x98);
  DWIN_Word(i, x);
  DWIN_Word(i, y);
  DWIN_Byte(i, 0);  //font

  if(bShow)
  {
    // show background;
    mode = 0x40;
  }
  else
  {
    mode = 0;
  }

  // Gbk encoding
  mode |= 0x02;

  DWIN_Byte(i, mode);
  DWIN_Byte(i, size);
  DWIN_Word(i, color);
  DWIN_Word(i, bColor);
  DWIN_String(i, string);
  DWIN_Send(i);
}

void DWIN_SHOW_MAIN_PIC()
{
  size_t i = 0;
  DWIN_Byte(i, 0x70);
  DWIN_Word(i, 0);
  DWIN_Send(i);
}
// Draw a positive integer
//  bShow: true=display background color; false=don't display background color
//  zeroFill: true=zero fill; false=no zero fill
//  zeroMode: 1=leading 0 displayed as 0; 0=leading 0 displayed as a space
//  size: Font size
//  color: Character color
//  bColor: Background color
//  iNum: Number of digits
//  x/y: Upper-left coordinate
//  value: Integer value
void DWIN_Draw_IntValue(uint8_t bShow, bool zeroFill, uint8_t zeroMode, uint8_t size, uint16_t color,
                          uint16_t bColor, uint8_t iNum, uint16_t x, uint16_t y, uint16_t value)
{
  size_t i = 0;
  if(size == font10x20)
  {
    size = font8x16;
  }
  DWIN_Byte(i, 0x14);
  // Bit 7: bshow
  // Bit 6: 1 = signed; 0 = unsigned number;
  // Bit 5: zeroFill
  // Bit 4: zeroMode
  // Bit 3-0: size
  DWIN_Byte(i, (bShow * 0x80) | (zeroFill * 0x20) | (zeroMode * 0x10) | size);
  DWIN_Word(i, color);
  DWIN_Word(i, bColor);
  DWIN_Byte(i, iNum);
  DWIN_Byte(i, 0); // F num
  DWIN_Word(i, x);
  DWIN_Word(i, y);
  #if 0
    for (char count = 0; count < 8; count++)
    {
      DWIN_Byte(i, value);
      value >>= 8;
      if (!(value & 0xFF)) break;
    }
  #else
    // Write a big-endian 64 bit integer
    const size_t p = i + 1;
    for (char count = 8; count--;) { // 7..0
      ++i;
      DWIN_SendBuf[p + count] = value;
      value >>= 8;
    }
  #endif
  // DWIN_Draw_String(true, true,size,color, bColor, x, y, cmd);
  DWIN_Send(i);
}

void DWIN_Draw_IntValue_N0SPACE(uint8_t bShow, bool zeroFill, uint8_t zeroMode, uint8_t size, uint16_t color,
                          uint16_t bColor, uint8_t iNum, uint16_t x, uint16_t y, uint16_t value)
{
  size_t i = 0;
  if(size == font10x20)
  {
    size = font8x16;
  }
  DWIN_Byte(i, 0x15);
  // Bit 7: bshow
  // Bit 6: 1 = signed; 0 = unsigned number;
  // Bit 5: zeroFill
  // Bit 4: zeroMode
  // Bit 3-0: size
  DWIN_Byte(i, (bShow * 0x80) | (zeroFill * 0x20) | (zeroMode * 0x10) | size);
  DWIN_Word(i, color);
  DWIN_Word(i, bColor);
  DWIN_Byte(i, iNum);
  DWIN_Byte(i, 0); // F num
  DWIN_Word(i, x);
  DWIN_Word(i, y);
  #if 0
    for (char count = 0; count < 8; count++)
    {
      DWIN_Byte(i, value);
      value >>= 8;
      if (!(value & 0xFF)) break;
    }
  #else
    // Write a big-endian 64 bit integer
    const size_t p = i + 1;
    for (char count = 8; count--;) { // 7..0
      ++i;
      DWIN_SendBuf[p + count] = value;
      value >>= 8;
    }
  #endif
  // DWIN_Draw_String(true, true,size,color, bColor, x, y, cmd);
  DWIN_Send(i);
}
// Draw a floating point number
//  bShow: true=display background color; false=don't display background color
//  zeroFill: true=zero fill; false=no zero fill
//  zeroMode: 1=leading 0 displayed as 0; 0=leading 0 displayed as a space
//  size: Font size
//  color: Character color
//  bColor: Background color
//  iNum: Number of whole digits
//  fNum: Number of decimal digits
//  x/y: Upper-left point
//  value: Float value
void DWIN_Draw_FloatValue(uint8_t bShow, bool zeroFill, uint8_t zeroMode, uint8_t size, uint16_t color,
                            uint16_t bColor, uint8_t iNum, uint8_t fNum, uint16_t x, uint16_t y, long value)
{
  //uint8_t *fvalue = (uint8_t*)&value;
  size_t i = 0;
  if(size == font10x20)
  {
    size = font8x16;
  }
  DWIN_Byte(i, 0x14);
  DWIN_Byte(i, (bShow * 0x80) | (zeroFill * 0x20) | (zeroMode * 0x10) | size);
  DWIN_Word(i, color);
  DWIN_Word(i, bColor);
  DWIN_Byte(i, iNum);
  DWIN_Byte(i, fNum);
  DWIN_Word(i, x);
  DWIN_Word(i, y);
  DWIN_Long(i, value);
  /*
  DWIN_Byte(i, fvalue[3]);
  DWIN_Byte(i, fvalue[2]);
  DWIN_Byte(i, fvalue[1]);
  DWIN_Byte(i, fvalue[0]);
  */
  DWIN_Send(i);
}

/*----------------------------------------Picture related functions ----------------------------------------*/

// Draw JPG and cached in #0 virtual display area
// id: Picture ID
void DWIN_JPG_ShowAndCache(const uint8_t id)
{
  size_t i = 0;
  DWIN_Word(i, 0x2200);
  DWIN_Byte(i, id);
  DWIN_Send(i);     // AA 23 00 00 00 00 08 00 01 02 03 CC 33 C3 3C
}

// Draw an Icon
//  libID: Icon library ID
//  picID: Icon ID
//  x/y: Upper-left point
void DWIN_ICON_Not_Filter_Show(uint8_t libID, uint8_t picID, uint16_t x, uint16_t y) 
{
  NOMORE(x, DWIN_WIDTH - 1);
  NOMORE(y, DWIN_HEIGHT - 1); // --ozy --srl
  size_t i = 0;

  DWIN_Byte(i, 0x97);
  DWIN_Word(i, x);
  DWIN_Word(i, y);
  DWIN_Byte(i, libID);
  DWIN_Byte(i, 0x01);
  DWIN_Word(i, picID);
  DWIN_Send(i);
}

// Draw an Icon
// libID: Icon library ID
// picID: Icon ID
// x/y: Upper-left point
void DWIN_ICON_Show(uint8_t libID, uint8_t picID, uint16_t x, uint16_t y)
{
  NOMORE(x, DWIN_WIDTH - 1);
  // --ozy --srl
  NOMORE(y, DWIN_HEIGHT - 1);
  size_t i = 0;
  DWIN_Byte(i, 0x97);
  DWIN_Word(i, x);
  DWIN_Word(i, y);
  DWIN_Byte(i, libID);
  DWIN_Byte(i, 0x00);
  DWIN_Word(i, picID);
  DWIN_Send(i);
}

// Unzip the JPG picture to a virtual display area
//  n: Cache index
//  id: Picture ID
void DWIN_JPG_CacheToN(uint8_t n, uint8_t id)
{
  size_t i = 0;
  DWIN_Byte(i, 0x25);
  DWIN_Byte(i, n);
  DWIN_Byte(i, id);
  DWIN_Send(i);
}

// Copy area from virtual display area to current screen
//  cacheID: virtual area number
//  xStart/yStart: Upper-left of virtual area
//  xEnd/yEnd: Lower-right of virtual area
//  x/y: Screen paste point
void DWIN_Frame_AreaCopy(uint8_t cacheID, uint16_t xStart, uint16_t yStart,
                         uint16_t xEnd, uint16_t yEnd, uint16_t x, uint16_t y) {
  size_t i = 0;
  /*
  DWIN_Byte(i, 0x27);
  DWIN_Byte(i, 0x80 | cacheID);
  DWIN_Word(i, xStart);
  DWIN_Word(i, yStart);
  DWIN_Word(i, xEnd);
  DWIN_Word(i, yEnd);
  DWIN_Word(i, x);
  DWIN_Word(i, y);
  DWIN_Send(i);
  */
  DWIN_Byte(i, 0x71);
  DWIN_Byte(i, cacheID);
  DWIN_Word(i, xStart);
  DWIN_Word(i, yStart);
  DWIN_Word(i, xEnd);
  DWIN_Word(i, yEnd);
  DWIN_Word(i, x);
  DWIN_Word(i, y);
  DWIN_Send(i);
}

// Animate a series of icons
//  animID: Animation ID; 0x00-0x0F
//  animate: true on; false off;
//  libID: Icon library ID
//  picIDs: Icon starting ID
//  picIDe: Icon ending ID
//  x/y: Upper-left point
//  interval: Display time interval, unit 10mS
void DWIN_ICON_Animation(uint8_t animID, bool animate, uint8_t libID, uint8_t picIDs, uint8_t picIDe, uint16_t x, uint16_t y, uint16_t interval) {
  NOMORE(x, DWIN_WIDTH - 1);
  NOMORE(y, DWIN_HEIGHT - 1); // --ozy --srl
  size_t i = 0;
  DWIN_Byte(i, 0x28);
  DWIN_Word(i, x);
  DWIN_Word(i, y);
  // Bit 7: animation on or off
  // Bit 6: start from begin or end
  // Bit 5-4: unused (0)
  // Bit 3-0: animID
  DWIN_Byte(i, (animate * 0x80) | 0x40 | animID);
  DWIN_Byte(i, libID);
  DWIN_Byte(i, picIDs);
  DWIN_Byte(i, picIDe);
  DWIN_Byte(i, interval);
  DWIN_Send(i);
}

// Animation Control
//  state: 16 bits, each bit is the state of an animation id
void DWIN_ICON_AnimationControl(uint16_t state) {
  size_t i = 0;
  DWIN_Byte(i, 0x28);
  DWIN_Word(i, state);
  DWIN_Send(i);
}

void DWIN_ICON_WR_SRAM(uint16_t data)
{
  size_t i = 0;
  DWIN_Byte(i, 0x31);
  DWIN_Byte(i, 0x5A);
  DWIN_Word(i, data);
  DWIN_Send(i);
}

void DWIN_ICON_SHOW_SRAM(uint16_t x,uint16_t y,uint16_t addr)
{
  size_t i = 0;
  DWIN_Byte(i, 0xC1);
  DWIN_Byte(i, 0x12);
  DWIN_Word(i, x);
  DWIN_Word(i, y);
  DWIN_Byte(i, 0);
  DWIN_Word(i, addr);
  DWIN_Send(i);
}


// Thumbnail read/write routines
bool gcode_readline(char *buffer, const size_t bufsize) {
  size_t i = 0;
  while (!card.eof()) {
    int16_t c = card.get();
    if (c < 0) break;
    if (c == '\n' || c == '\r') {
      if (i == 0) continue;       // skip consecutive empty lines
      break;
    }
    if (i + 1 < bufsize)         // leave space for '\0'
      buffer[i++] = char(c);
  }
  buffer[i] = '\0';
  return i > 0;
}




static uint16_t parse_hex4(const char *p) {
  uint16_t v = 0;
  for (uint8_t i = 0; i < 4; i++) {
    const char c = p[i];
    uint8_t n;
    if      (c >= '0' && c <= '9') n = c - '0';
    else if (c >= 'A' && c <= 'F') n = c - 'A' + 10;
    else if (c >= 'a' && c <= 'f') n = c - 'a' + 10;
    else return 0;  // unexpected character -> black color
    v = (v << 4) | n;
  }
  return v;
}


bool find_thumb_raw16_header(uint16_t &w, uint16_t &h) {
  char line[96];

  // Always start from the beginning of the file
  card.setIndex(0);

  uint16_t line_count = 0;

  while (line_count < 50 && gcode_readline(line, sizeof(line))) {
    line_count++;

    if (line[0] != ';') continue;

    char *p = line + 1;
    while (*p == ' ') p++;

    // Buscamos: "; E3V3SE_THUMB_RAW16_BEGIN 96x96"
    if (strncmp(p, "E3V3SE_THUMB_RAW16_BEGIN", 24) == 0) {
      uint16_t tw = 0, th = 0;

      // Try to parse the "96x96" that comes right after
      if (sscanf(p + 24, "%hu%*c%hu", &tw, &th) == 2) {
        w = tw;
        h = th;
      }
      else {
        w = 96;
        h = 96;
      }

      return true;
    }
  }

  return false;
}


static constexpr uint16_t THUMB_X_START = 12;
static constexpr uint16_t THUMB_Y_START = 25;

// Bucketed, vertically-merged blit for DWIN_RenderThumb.
//
// DWIN_Draw_Rectangle re-sends the palette (0x40) before every fill (0x5B),
// so a naive per-pixel draw costs 2 wire frames per pixel. Instead: RLE
// each row, merge a run vertically into the matching run above it when the
// (x0,x1,color) span matches (one 0x5B fill then covers many rows), then
// group all runs by color so the palette is sent once per distinct color
// rather than once per run. See DWIN_OptimizationReference/
// BENCHMARK_RESULTS.md for the measurements this is based on.

struct ThumbRun {
  uint8_t x0, x1, y0, y1; // coordinates local to the thumbnail (0..95)
  uint16_t color;
};

// Sized from real quantized 96x96 thumbnails (worst measured: ~1420 runs).
// An image that overflows this just flushes (renders) early at the next
// row boundary and continues -- never drops pixels, only palette reuse.
static constexpr uint16_t MAX_THUMB_RUNS = 1536;
static ThumbRun thumb_runs[MAX_THUMB_RUNS];
static uint8_t thumb_run_emitted[(MAX_THUMB_RUNS + 7) / 8];

// A row can produce at most ceil(96/2)=48 runs (each run must be separated
// from the next by at least one skipped background pixel).
static constexpr uint8_t MAX_RUNS_PER_ROW = 48;
static uint16_t thumb_open_prev[MAX_RUNS_PER_ROW]; // indices into thumb_runs
static uint16_t thumb_open_cur[MAX_RUNS_PER_ROW];

// Delays every THUMB_BATCH_SIZE frames (SET+FILL counted together) instead
// of every single frame. Empirically bisected on real hardware -- a
// host-side test rig's numbers did NOT transfer to real firmware, so these
// values were found by flashing and watching an actual printer (see
// DWIN_OptimizationReference/dwin_stress.cpp for the tooling). Confirmed
// reliable on C13 (GD32F303) hardware; still needs its own validation pass
// on F401 boards before assuming it holds there too.
//
// An unthrottled flood of commands has separately been observed to overrun
// the panel's input buffer badly enough to need a PHYSICAL POWER CYCLE to
// recover -- never remove this throttle entirely.
#define THUMB_BATCH_SIZE 10
#define THUMB_BATCH_DELAY_MS 16

static void thumb_throttle() {
  // IMPORTANT: always plain delay(), never safe_delay(). safe_delay() calls
  // idle(), which can let other DWIN UI code send its own DWIN_Set_Color
  // mid-blit, corrupting the palette state a still-pending
  // DWIN_Fill_Rect_Raw call is relying on.
  static uint16_t s_batch_frame_count = 0; // not reset per render; harmless
  if (++s_batch_frame_count >= THUMB_BATCH_SIZE) {
    delay(THUMB_BATCH_DELAY_MS);
    s_batch_frame_count = 0;
  }

  // hal.watchdog_refresh() (STM32F1) is only fed from
  // Temperature::updateTemperaturesFromRawValues(), which runs cooperatively
  // from the main loop, not from any ISR. This function blocks well past
  // the 4s watchdog timeout while rendering, so it must feed the watchdog
  // itself (same pattern as Sd2Card.cpp's own long blocking SD init).
  hal.watchdog_refresh();
}

// Emit every accumulated run, grouped by color: one DWIN_Set_Color per
// distinct color, followed by all of that color's DWIN_Fill_Rect_Raw calls.
static void thumb_flush_runs(uint16_t run_count) {
  memset(thumb_run_emitted, 0, (run_count + 7) / 8);
  for (uint16_t i = 0; i < run_count; i++) {
    if (thumb_run_emitted[i >> 3] & (1 << (i & 7))) continue;
    const uint16_t color = thumb_runs[i].color;
    DWIN_Set_Color(color, 0xFFFF);
    thumb_throttle();
    for (uint16_t j = i; j < run_count; j++) {
      if (thumb_run_emitted[j >> 3] & (1 << (j & 7))) continue;
      if (thumb_runs[j].color != color) continue;
      thumb_run_emitted[j >> 3] |= (1 << (j & 7));
      const ThumbRun &r = thumb_runs[j];
      DWIN_Fill_Rect_Raw(THUMB_X_START + r.x0, THUMB_Y_START + r.y0,
                          THUMB_X_START + r.x1, THUMB_Y_START + r.y1);
      thumb_throttle();
    }
  }
}

 bool DWIN_RenderThumb(const char *filename) {
  hal.watchdog_refresh(); // this whole function blocks well past the 4s watchdog window; see thumb_throttle()

  card.openFileRead(filename);
  if (!card.isFileOpen())
    return false;

  uint16_t w = 0, h = 0;
  if (!find_thumb_raw16_header(w, h)) { // header not found, bail out
    card.closefile();
    return false;
  }

  // header found, limit to max size
  if (w > 96) w = 96;
  if (h > 96) h = 96;


  char line[4 * 96 + 8]; // 384 hex + '; ' + '\0'

  uint16_t y = 0;
  uint16_t run_count = 0;     // runs accumulated in thumb_runs so far
  uint8_t open_prev_count = 0, open_cur_count = 0; // vertical-merge state

  while (y < h && gcode_readline(line, sizeof(line))) {
    hal.watchdog_refresh(); // a run of all-background rows emits no frames, so thumb_throttle()'s refresh alone wouldn't cover it

    // Skip lines other than image data
    if (line[0] != ';') continue;

    const char *p = line + 1;
    while (*p == ' ') p++;

    // Have we reached the END?
    if (strncmp(p, "E3V3SE_THUMB_RAW16_END", 23) == 0)
      break;

    const size_t len = strlen(p);
    if (len < w * 4)
      break;

    // RLE this row, merging each run vertically into the matching
    // (x0,x1,color) run left open by the row above where possible.
    open_cur_count = 0;
    uint8_t prev_idx = 0; // two-pointer scan over thumb_open_prev
    uint16_t x = 0;
    while (x < w) {
      const uint16_t color = parse_hex4(p + x * 4);
      if (color == 0) { x++; continue; } // background: not drawn, matches previous behavior

      uint16_t x2 = x;
      while (x2 + 1 < w && parse_hex4(p + (x2 + 1) * 4) == color) x2++;

      // Runs within a row are x-sorted and disjoint, so this is a 1:1 match,
      // not a search: skip previous-row runs that already ended before x.
      while (prev_idx < open_prev_count && thumb_runs[thumb_open_prev[prev_idx]].x1 < x) prev_idx++;

      bool merged = false;
      if (prev_idx < open_prev_count) {
        ThumbRun &pr = thumb_runs[thumb_open_prev[prev_idx]];
        if (pr.x0 == x && pr.x1 == x2 && pr.color == color) {
          pr.y1 = uint8_t(y); // extend the existing run down; x0/x1/color never change
          if (open_cur_count < MAX_RUNS_PER_ROW)
            thumb_open_cur[open_cur_count++] = thumb_open_prev[prev_idx];
          prev_idx++;
          merged = true;
        }
      }
      if (!merged) {
        thumb_runs[run_count] = { uint8_t(x), uint8_t(x2), uint8_t(y), uint8_t(y), color };
        if (open_cur_count < MAX_RUNS_PER_ROW)
          thumb_open_cur[open_cur_count++] = run_count;
        run_count++;
      }
      x = x2 + 1;
    }

    memcpy(thumb_open_prev, thumb_open_cur, open_cur_count * sizeof(uint16_t));
    open_prev_count = open_cur_count;

    y++;

    // Flush at row boundaries only (never mid-row: that would leave a
    // half-built open_cur and buys nothing). A completed row needs at most
    // MAX_RUNS_PER_ROW new slots, so checking here guarantees the next row
    // always fits.
    if (MAX_THUMB_RUNS - run_count < MAX_RUNS_PER_ROW) {
      thumb_flush_runs(run_count);
      run_count = 0;
      open_prev_count = 0; // those runs are now drawn; nothing left to extend
    }
  }

  if (run_count > 0)
    thumb_flush_runs(run_count);

  card.closefile();
  return y > 0;
}

// ----
#define ORCA_FOOTER_WINDOW    32768UL     //Bytes from the end that we are going to scan

// Remove trailing spaces/newlines
static void trim_trailing_ws(char *s) {
  int len = strlen(s);
  while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
    s[--len] = '\0';
  }
}

// Convert strings like "13m 18s", "1h 2m 3s", "45s" to seconds
static uint32_t parse_orca_time_to_seconds(const char *time_str) {
  uint32_t seconds = 0;
  uint32_t value   = 0;

  for (const char *p = time_str; *p; ++p) {
    if (*p >= '0' && *p <= '9') {
      value = value * 10UL + (*p - '0');
      continue;
    }

    if (*p == 'h' || *p == 'H') {
      seconds += value * 3600UL;
      value = 0;
    }
    else if (*p == 'm' || *p == 'M') {
      seconds += value * 60UL;
      value = 0;
    }
    else if (*p == 's' || *p == 'S') {
      seconds += value;
      value = 0;
    }
  }

  seconds += value; // In case it ends without a suffix
  return seconds;
}


model_information_t model_information;

// static const char *gcode_information_name[] = {
//   "TIME",
//   "Filament used",
//   "Layer height"
// };

uint8_t read_gcode_model_information(const char* fileName) {
  char string_buf[_GCODE_METADATA_STRING_LENGTH_MAX + 1];
  char byte;
  uint16_t line_idx = 0;
  // SERIAL_ECHOLNPGM("read_gcode_model using: ", fileName);
  // SERIAL_ECHOLNPGM("Card current filename: ", card.filename);

  // SERIAL_ECHOLNPAIR("Reading model information from G-code file: ", fileName);
  // SERIAL_ECHOLN("Resetting model information variables.");
  ui.reset_remaining_time();
  ui.total_time_reset();
  
  #if ENABLED(DWIN_RENDER_THUMBNAIL)
    ui.set_total_layers(0);
    ui.set_current_layer(0);
  #endif
  // memset(&model_information, 0, sizeof(model_information)); 
  memset(model_information.filament, 0, sizeof(model_information.filament));
  memset(model_information.height,   0, sizeof(model_information.height));
  
  // SERIAL_ECHOLNPAIR("Remaining time reset to: ", ui.get_remaining_time());
  // SERIAL_ECHOLNPAIR("Total time reset to: ", ui.get_total_time());
  // SERIAL_ECHOLNPAIR("Model filament info: ", model_information.filament);
  // SERIAL_ECHOLNPAIR("Model height info: ", model_information.height);


  card.openFileRead(fileName);
  if (!card.isFileOpen())
    return METADATA_PARSE_ERROR;

  bool is_orca            = false;
  bool have_cura_time     = false;
  bool have_cura_filament = false;
  bool have_cura_height   = false;

  // ---------------------------------------------------------------------------
  // PASS 1: first MAX_HEADER_LINES
  //   -Search for Cura-type header (TIME, Filament used, Layer height)
  //   -Detect if it is OrcaSlicer
  // ---------------------------------------------------------------------------
  while (!card.eof() && line_idx++ < _GCODE_METADATA_STRING_LENGTH_MAX) {

    // Read a full line
    uint16_t i = 0;
    while (!card.eof() && i < _GCODE_METADATA_STRING_LENGTH_MAX) {
      int16_t c = card.get();
      if (c < 0) break;
      byte = (char)c;
      if (byte == '\r' || byte == '\n') break;
      string_buf[i++] = byte;
    }
    string_buf[i] = '\0';

    if (i == 0)
      continue;

    #if ENABLED(USER_LOGIC_DEUBG)
      SERIAL_ECHOLNPAIR("Header line: ", string_buf);
    #endif

    // Detect Orca in the header
    if (strstr(string_buf, "OrcaSlicer") || strstr(string_buf, "Orca Slicer"))
      is_orca = true;

    // We are only interested in comments
    if (string_buf[0] != ';')
      continue;

    // Skip ';' and spaces
    char *char_pos = string_buf + 1;
    while (*char_pos == ' ')
      char_pos++;

    if (!*char_pos)
      continue;

    // ---Curate style ---
    // ;TIMES:441
    if (!have_cura_time && strncmp(char_pos, "TIME", 4) == 0) {
      const char *value_buf = char_pos + 4;
      while (*value_buf == ':' || *value_buf == ' ' || *value_buf == '=') value_buf++;
      ui.set_total_time(atoi(value_buf));    // Cura gives TIME in seconds
      have_cura_time = true;
    }
    // ;Filament used: 0.187823m
    else if (!have_cura_filament && strncmp(char_pos, "Filament used", 13) == 0) {
      const char *value_buf = char_pos + 13;
      while (*value_buf == ':' || *value_buf == ' ' || *value_buf == '=') value_buf++;

      memset(model_information.filament, 0, sizeof(model_information.filament));
      if (strlen(value_buf) > 6) {
        strncpy(model_information.filament, value_buf, 5);
        model_information.filament[5] = '\0';
        if ('m' == value_buf[strlen(value_buf) - 1])
          strncat(model_information.filament, &value_buf[strlen(value_buf) - 1], 1);
        else if ('m' == value_buf[strlen(value_buf) - 2])
          strncat(model_information.filament, &value_buf[strlen(value_buf) - 2], 1);
      }
      else {
        strcpy(model_information.filament, value_buf);
      }
      have_cura_filament = true;
    }
    // ;Layer height: 0.2
    else if (!have_cura_height && strncmp(char_pos, "Layer height", 12) == 0) {
      const char *value_buf = char_pos + 12;
      while (*value_buf == ':' || *value_buf == ' ' || *value_buf == '=') value_buf++;
      memset(model_information.height, 0, sizeof(model_information.height));
      strncpy(model_information.height, value_buf, sizeof(model_information.height) - 3);
      model_information.height[sizeof(model_information.height) - 3] = '\0';
      trim_trailing_ws(model_information.height);
      strcat(model_information.height, "mm");
      have_cura_height = true;
    }

    if (have_cura_time && have_cura_filament && have_cura_height) {
      // Card.closefile();
      ui.set_remaining_time(ui.get_total_time());
      // SERIAL_ECHOLN("Cura-type header detected and parsed successfully.");
      // SERIAL_ECHOLNPAIR("Total time: ", ui.get_total_time());
      // SERIAL_ECHOLNPAIR("Remaining time: ", ui.get_remaining_time());
      // SERIAL_ECHOLNPAIR("Filament used: ", model_information.filament);
      // SERIAL_ECHOLNPAIR("Layer height: ", model_information.height);
      return METADATA_PARSE_OK;
    }
  }

  // If it is not Orca and we did not find a Cura-type header, exit
  if (!is_orca) {
    // Card.closefile();
    return METADATA_PARSE_ERROR;
  }

  // ---------------------------------------------------------------------------
  // PASS 2: Orca footer (from the end of the file)
  //   We look for:
  //     ; filament used [mm] = 433.62
  //     ; total layers count = 82
  //     ; estimated printing time (normal mode) = 13m 18s
  //     ; layer_height = 0.16
  // ---------------------------------------------------------------------------
  const uint32_t filesize = card.getFileSize();
  const uint32_t window   = (filesize > ORCA_FOOTER_WINDOW) ? ORCA_FOOTER_WINDOW : filesize;

  // Position near the end
  card.setIndex(filesize - window);

  // Consume first partial line (we are in the middle of a line)
  while (!card.eof()) {
    int16_t c = card.get();
    if (c < 0) break;
    if (c == '\n' || c == '\r') break;
  }

  bool have_filament_mm   = false;
  bool have_layers        = false;
  bool have_time          = false;
  bool have_layer_height  = false;

  char     filament_mm_str[16]  = { 0 };  // "433.62"
  char     layer_height_str[16] = { 0 };  // "0.16"
  uint32_t orca_time_sec        = 0;
  uint16_t orca_layers          = 0;

  while (!card.eof()) {
    uint16_t i = 0;
    while (!card.eof() && i < _GCODE_METADATA_STRING_LENGTH_MAX) {
      int16_t c = card.get();
      if (c < 0) break;
      byte = (char)c;
      if (byte == '\r' || byte == '\n') break;
      string_buf[i++] = byte;
    }
    string_buf[i] = '\0';

    if (i == 0)
      continue;

    if (string_buf[0] != ';')
      continue;

    char *char_pos = string_buf + 1;
    while (*char_pos == ' ')
      char_pos++;

    if (!*char_pos)
      continue;

    // ; filament used [mm] = 433.62
    if (!have_filament_mm && strncmp(char_pos, "filament used [mm]", 18) == 0) {
      const char *p = strchr(char_pos, '=');
      if (p) {
        p++;
        while (*p == ' ' || *p == '\t') p++;
        strncpy(filament_mm_str, p, sizeof(filament_mm_str) - 1);
        filament_mm_str[sizeof(filament_mm_str) - 1] = '\0';
        trim_trailing_ws(filament_mm_str);
        have_filament_mm = true;
      }
    }

    // ; total layers count = 82
    else if (!have_layers && strncmp(char_pos, "total layers count", 18) == 0) {
      const char *p = strchr(char_pos, '=');
      if (p) {
        p++;
        while (*p == ' ' || *p == '\t') p++;
        orca_layers = (uint16_t)atoi(p);
        have_layers = (orca_layers > 0);
        #if ENABLED(DWIN_RENDER_THUMBNAIL)
          ui.set_total_layers(orca_layers);
        #endif
      }
    }

    // ; estimated printing time (normal mode) = 13m 18s
    else if (!have_time && strncmp(char_pos, "estimated printing time", 23) == 0) {
      const char *p = strchr(char_pos, '=');
      if (p) {
        p++;
        while (*p == ' ' || *p == '\t') p++;
        orca_time_sec = parse_orca_time_to_seconds(p);
        have_time     = (orca_time_sec > 0);
      }
    }

    // ; layer_height = 0.16   (en CONFIG_BLOCK)
    else if (!have_layer_height && strncmp(char_pos, "layer_height", 12) == 0) {
      const char *p = strchr(char_pos, '=');
      if (!p) p = strchr(char_pos, ':');
      if (p) {
        p++;
        while (*p == ' ' || *p == '\t') p++;
        strncpy(layer_height_str, p, sizeof(layer_height_str) - 1);
        layer_height_str[sizeof(layer_height_str) - 1] = '\0';
        trim_trailing_ws(layer_height_str);
        have_layer_height = (layer_height_str[0] != '\0');
      }
    }

    // If we already have everything we want, we can exit early
    if (have_filament_mm && have_layers && have_time && have_layer_height)
      break;
  }

  //card.closefile();
  // ----Apply data obtained from Orca ----

  // Total time (seconds)
  if (have_time)
    ui.set_total_time(orca_time_sec);

  // Filament: mm -> m (2 decimals, "Xm")
  if (have_filament_mm) {
    const float mm     = atof(filament_mm_str);
    const float meters = mm * 0.001f;
    char tmp[16];
    dtostrf(meters, 0, 2, tmp);       // e.g.: "0.cz"
    char *p = tmp;
    while (*p == ' ') p++;            // remove leading spaces

    memset(model_information.filament, 0, sizeof(model_information.filament));
    strncpy(model_information.filament, p, sizeof(model_information.filament) - 2);
    model_information.filament[sizeof(model_information.filament) - 2] = '\0';
    strcat(model_information.filament, "m");
  }

  // Layer height: "0.16mm"
  if (have_layer_height) {
    memset(model_information.height, 0, sizeof(model_information.height));
    strncpy(model_information.height, layer_height_str, sizeof(model_information.height) - 3);
    model_information.height[sizeof(model_information.height) - 3] = '\0';
    trim_trailing_ws(model_information.height);
    strcat(model_information.height, "mm");
  }

  // If your struct has a field for total layers:
  // if (have_layers)
  //   model_information.total_layers = orca_layers;

  // Consider it OK if we get at least some useful info
  if (have_time && have_filament_mm && have_layer_height){
      ui.set_remaining_time(ui.get_total_time());
      // SERIAL_ECHOLN("Orca-type header detected and parsed successfully.");
      // SERIAL_ECHOLNPAIR("Total time: ", ui.get_total_time());
      // SERIAL_ECHOLNPAIR("Remaining time: ", ui.get_remaining_time());
      // SERIAL_ECHOLNPAIR("Filament used: ", model_information.filament);
      // SERIAL_ECHOLNPAIR("Layer height: ", model_information.height);
      return METADATA_PARSE_OK;
    }
    

  return METADATA_PARSE_ERROR;
}


#endif // Dwin creality lcd
