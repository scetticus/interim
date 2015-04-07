#include "minilisp.h"
#include "blit.h"
#include "machine.h"
#include "utf8.h"
#include <stdio.h>
#include <stdint.h>

#define FAST_BLIT

#define put_pixel machine_video_set_pixel

int blit_vector32(uint32_t* pixels, uint sx, uint sy, uint pitch, uint w, uint h, uint dx, uint dy)
{
  for (int y=0; y<h; y++) {
    for (int x=0; x<w; x++) {
      put_pixel(dx+x,dy+y, pixels[((sy+y)*pitch+sx+x)]);
    }
  }
  
  return 0;
}

static uint32_t* FB;
void init_blitter(uint32_t* fb) {
  FB = fb;
}

#define T_PITCH 1920

int blit_string1(uint32_t color, int h, int w, int y, int x, int cursor_pos, Cell* str_c, Cell* font) {
  uint8_t* pixels = font->addr;
  uint8_t* str = str_c->addr;

  int x2 = x+w;
  int y2 = y+h;
  int x1 = x;
  int i  = 0;
  int p  = 0;
  uint8_t c = 0;

  do {
    c = str[i];
    uint8_t len = utf8_rune_len(c);
    uint32_t rune = utf8_rune_at(&str[i],0);

    if (cursor_pos != p) {
      if (rune!=10 && rune!=32) {
        blit_vector1_invert(color, y, x, 16, 16, 4096, (rune/256)*16, (rune&0xff)*16, pixels);
      }
    } else {
      if (rune == 0) rune = 32;
      blit_vector1(color, y, x, 16, 16, 4096, (rune/256)*16, (rune&0xff)*16, pixels);
    }
    i+=len;
    x+=8;
    if (x>=x2 || c==10) {
      y+=16;
      x=x1;
    }
    if (y>=y2) {
      break;
    }
    p++;
  } while (c);

  return i;
}

// 32bit source blitting gives ~2fps
// reading uint32_t from source is slower than reading bytes (at least 2x?)
// solution: 256-color palette / intensity / mask!
// O3 is slower than O2
// very fast: convert to 0x1 and 0x0 and subtract 1 (using overflow)

#ifdef FAST_BLIT

int blit_vector1(uint32_t color, uint dy, uint dx, uint h, uint w, uint pitch, uint sy, uint sx, uint8_t* pixels)
{
  uint32_t s_offset = sy*pitch+sx;
  uint32_t t_offset = dy*T_PITCH+dx;
 
  for (unsigned int y=0; y<h; y++) {
    register uint32_t* tfb = FB+t_offset;
    /*for (unsigned int x=0; x<w; x++) {
      register uint32_t px = pixels[s_offset+x];
      px |= (px<<8);
      px |= (px<<16);
      
      *(tfb+x) = px;
      }*/

    // letz unroll it
    register uint8_t* ptr = pixels+s_offset;

    for (unsigned int x=0; x<16; x++) {
      register uint32_t px = *ptr - 1;
      //px |= (px<<8);
      //px |= (px<<16);
      
      *tfb = px&color;
      tfb++;
      ptr++;
    }
    
    s_offset += pitch;
    t_offset += T_PITCH;
  }
  
  return 0;
}

int blit_vector1_invert(uint32_t color, uint dy, uint dx, uint h, uint w, uint pitch, uint sy, uint sx, uint8_t* pixels)
{
  uint32_t s_offset = sy*pitch+sx;
  uint32_t t_offset = dy*T_PITCH+dx;
 
  for (unsigned int y=0; y<h; y++) {
    register uint32_t* tfb = FB+t_offset;
    register uint8_t* ptr = pixels+s_offset;

    for (unsigned int x=0; x<16; x++) {
      register uint32_t px = (1-*ptr) - 1;
      //px |= (px<<8);
      //px |= (px<<16);
      
      *tfb = px&color;
      tfb++;
      ptr++;
    }
    
    s_offset += pitch;
    t_offset += T_PITCH;
  }
  
  return 0;
}

#else

// blits b+w bitmaps 
//int blit_vector1(void* pixels, uint sx, uint sy, uint pitch, uint w, uint h, uint dx, uint dy, uint color)

int blit_vector1(uint color, uint dy, uint dx, uint h, uint w, uint pitch, uint sy, uint sx, uint8_t* pixels)
{
  for (unsigned int y=0; y<h; y++) {
    unsigned int ty = dy+y;
    for (unsigned int x=0; x<w; x++) {
      unsigned int px = pixels[((sy+y)*pitch+sx+x)];
      unsigned int tx = dx+x*8;
      
      // unpack byte into 8 pixels
      put_pixel(tx++,ty,((px>>7)&1)?color:0);
      put_pixel(tx++,ty,((px>>6)&1)?color:0);
      put_pixel(tx++,ty,((px>>5)&1)?color:0);
      put_pixel(tx++,ty,((px>>4)&1)?color:0);
      put_pixel(tx++,ty,((px>>3)&1)?color:0);
      put_pixel(tx++,ty,((px>>2)&1)?color:0);
      put_pixel(tx++,ty,((px>>1)&1)?color:0);
      put_pixel(tx,ty,(px&1)?color:0);
    }
  }
  
  return 0;
}

int blit_vector1_invert(uint color, uint dy, uint dx, uint h, uint w, uint pitch, uint sy, uint sx, uint8_t* pixels)
{
  for (unsigned int y=0; y<h; y++) {
    unsigned int ty = dy+y;
    for (unsigned int x=0; x<w; x++) {
      unsigned int px = pixels[((sy+y)*pitch+sx+x)];
      unsigned int tx = dx+x*8;
      
      // unpack byte into 8 pixels
      put_pixel(tx++,ty,((px>>7)&1)?0:color);
      put_pixel(tx++,ty,((px>>6)&1)?0:color);
      put_pixel(tx++,ty,((px>>5)&1)?0:color);
      put_pixel(tx++,ty,((px>>4)&1)?0:color);
      put_pixel(tx++,ty,((px>>3)&1)?0:color);
      put_pixel(tx++,ty,((px>>2)&1)?0:color);
      put_pixel(tx++,ty,((px>>1)&1)?0:color);
      put_pixel(tx,ty,(px&1)?0:color);
    }
  }
  
  return 0;
}

#endif


uint8_t* blitter_speedtest(uint8_t* font) {
  printf("speedtest unpacking font\r\n");
  uint8_t* font_unpacked = (uint8_t*)malloc(16*16*256*256);
  for (int y=0; y<256*16; y++) {
    if (y%10==0) {
      //printf("speedtest unpacking font row %d/%d\r\n",y,256*16);
    }
    int tx = 0;
    int to = y*256*16;
    for (int x=0; x<256*2; x++) {
      uint8_t px = font[((4*16+y))*516+(x+4)];
      
      font_unpacked[to+tx] = (((px>>7)&1)?1:0); tx++;
      font_unpacked[to+tx] = (((px>>6)&1)?1:0); tx++;
      font_unpacked[to+tx] = (((px>>5)&1)?1:0); tx++;
      font_unpacked[to+tx] = (((px>>4)&1)?1:0); tx++;

      font_unpacked[to+tx] = (((px>>3)&1)?1:0); tx++;
      font_unpacked[to+tx] = (((px>>2)&1)?1:0); tx++;
      font_unpacked[to+tx] = (((px>>1)&1)?1:0); tx++;
      font_unpacked[to+tx] = (((px>>0)&1)?1:0); tx++;
    }
  }
  //free(font);
  //font = font_unpacked;
  return font_unpacked;

  /*
  int rune = 0;
  while (1) {
    printf("speedtest rune: %d\r\n",rune);
    for (int y=0; y<60; y++) {
      for (int x=0; x<120; x++) {
        blit_vector1b(0xffff00,y*16,x*16,16,16,256*16,(rune/256)*16,(rune%256)*16,font_unpacked);
      }
    }
    rune++;
    if (rune>=64*256) rune = 0;
    }*/
}