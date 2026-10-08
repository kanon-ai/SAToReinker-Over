#ifndef NEON_HARDWARE_H
#define NEON_HARDWARE_H

typedef unsigned char u8;
typedef unsigned int u16;

#define INPUT_LEFT  1
#define INPUT_RIGHT 2
#define INPUT_UP    4
#define INPUT_DOWN  8
#define INPUT_FIRE  16
#define INPUT_BOMB  32
#define INPUT_PAUSE 64
#define INPUT_LAB   128

/* SCREEN 5 + EPAL, two 32 KiB pages in 256 KiB VRAM, shared 16 colors. */
void gfx_init(void); void gfx_wait(void); void gfx_vblank(void);
void gfx_fill(u16 x,u16 y,u16 w,u16 h,u8 color);
void gfx_blit(u16 sx,u16 sy,u16 dx,u16 dy,u16 w,u16 h,u8 transparent);
void gfx_line(u16 x,u16 y,u16 x1,u16 y1,u8 color);
void gfx_flip(u8 page); void background_init(void);
void gfx_bullets_begin(void); void gfx_bullet(u8 sx,u8 dx,u16 dy);
void geo_flush(void); void geo_polygon(int x,int y,u8 color);
u8 input_read(void); u8 disk_keys(void);
u16 timer_read(void); void clock_poll(void); void clock_reset(void);
extern unsigned long clock_pending;
void background_draw(u8 page,u16 phase);
void gfx_actors(u16 x,u16 y,u16 phase,u8 visible);
extern u8 sprite_count,bitmap_count,force_bitmap,decor_enabled,sat[512],bands[32];
extern u16 rendered_bullets;
void gfx_bullet_fast(void);
void gfx_dirty(u8 x,u8 y,u8 w,u8 h);
extern u8 bullet_x,bullet_y,bullet_group,background_full;
#endif
