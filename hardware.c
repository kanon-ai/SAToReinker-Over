#include "hardware.h"
#include "palette.h"
#include "aura.h"


__sfr __at (0x60) hw_vram;
__sfr __at (0x61) hw_palette;
__sfr __at (0x63) hw_reg_data;
__sfr __at (0x64) hw_reg_select;
__sfr __at (0x65) hw_status;
__sfr __at (0x66) hw_irq_flags;
__sfr __at (0x67) hw_system;
__sfr __at (0x6F) hw_video_control;
__sfr __at (0xA0) hw_psg_select;
__sfr __at (0xA1) hw_psg_write;
__sfr __at (0xA2) hw_psg_read;
__sfr __at (0xA9) hw_key_data;
__sfr __at (0xAA) hw_key_select;
static u8 palette_pending;
static u8 cursor_pending[16];

static void vram_write_address(u16 low, u8 high)
{
    hw_reg_select = 0;
    hw_reg_data = (u8)low;
    hw_reg_data = (u8)(low >> 8);
    hw_reg_data = high;
}

static void reg_write(u8 reg, u8 value)
{
    hw_reg_select = reg;
    hw_reg_data = value;
}

/* Arguments below are side-effect-free variables; expand in place to avoid
 * four to six extra C calls for every small command or font character. */
#define word_write(value) do { \
    hw_reg_data = (u8)(value); \
    hw_reg_data = (u8)((value) >> 8); \
} while (0)

void gfx_wait(void)
{
    while (hw_status & 0x01) { }
}

void gfx_vblank(void)
{
    /* A previous blank must end first: never consume a stale blank level. */
    while (hw_status & 0x40) { }
    while (!(hw_status & 0x40)) { }
}

void gfx_init(void)
{
    u8 i, bank, r, g, b;
    __asm di __endasm;
    /* Video9000 manual pp. 12-13: disable genlock/external video, then
     * wait at least one full frame before any V9990 register writes.
     * Two fresh blank edges cover a full frame regardless of entry phase. */
    hw_video_control = 0;
    gfx_vblank();
    gfx_vblank();
    hw_system = 0x02;
    hw_system = 0x00;
    /* R0..R28 are the defined writeable control registers. */
    hw_reg_select = 0;
    for (i = 0; i != 29; ++i) hw_reg_data = 0;
    hw_irq_flags = 0x07;
    reg_write(6, 0x81);
    reg_write(7, 0x00);
    reg_write(8, 0x42);       /* 512 KiB VRAM, no display or hardware cursors. */
    reg_write(13, 0x00);      /* 4bpp palette mode, palette entries 0..15. */
    reg_write(14, 0);
    /* Four complete RGB5 banks. R13 switches the bitmap colors while R28
     * keeps both native cursor planes on their own fixed entries 60..63. */
    for (bank = 0; bank != 4; ++bank) {
        for (i = 0; i != 16; ++i) {
            r = v9990_palette[i*3];
            g = v9990_palette[i*3+1];
            b = v9990_palette[i*3+2];
            if (bank == 1 && i && i != 7 && i < 13) {
                r = r*3/4;
                b = b + (31-b)/4;
            } else if (bank == 2 && i && i != 5 && i != 6 && i != 7) {
                r = r + (31-r)/5;
                g = g*3/4;
                b = b + (31-b)/8;
            } else if (bank == 3 && i) {
                r = r + (31-r)/3;
                g = g + (31-g)/3;
                b = b + (31-b)/4;
            }
            if (bank == 3 && i == 13) { r=7; g=31; b=31; }
            if (bank == 3 && i == 14) { r=31; g=31; b=31; }
            if (bank == 3 && i == 15) { r=31; g=22; b=8; }
            hw_palette = r; hw_palette = g; hw_palette = b;
        }
    }
    palette_pending = 0;
    reg_write(28, 15);
    hw_reg_select = 44;
    hw_reg_data = 0x00;      /* ARG */
    hw_reg_data = 0x0C;      /* COPY logical operation */
    hw_reg_data = 0xFF;
    hw_reg_data = 0xFF;      /* BOTH even and odd VRAM write masks. */
    /* Changes to the screen mode are not immediate. This also works with
     * display disabled because status VR reports the scan timing. */
    gfx_vblank();
    gfx_rect(0, 0, 256, 512, 0);
    gfx_wait();
}

static void cursor_init(void)
{
    u8 plane, y, x, packed, bit;
    /* 7FE00 attributes and 7FF00 patterns are reserved in feature.bin. */
    gfx_cursor(128,100,0,0);
    vram_write_address(0xFE00,7);
    for (x=0;x!=16;++x) hw_vram=cursor_pending[x];
    vram_write_address(0xFF00,7);
    for (plane=0; plane!=2; ++plane) for (y=0; y!=32; ++y) {
        for (x=0; x!=32; x+=8) {
            packed=0;
            for(bit=0;bit!=8;++bit) {
                u8 px=x+bit, on;
                if (!plane) {
                    on=(((y==5||y==26)&&(px>=5&&px<=10||px>=21&&px<=26)) ||
                        ((px==5||px==26)&&(y>=5&&y<=10||y>=21&&y<=26)));
                } else {
                    on=((px==15&&y>=12&&y<=18)||(y==15&&px>=12&&px<=18));
                }
                if(on)packed|=(u8)(0x80>>bit);
            }
            hw_vram=packed;
        }
    }
}

void hardware_upload(void)
{


    cursor_init();
    reg_write(8, 0x82);      /* Display and native B1 cursors enabled. */
    gfx_vblank();
}

void gfx_palette_select(u8 bank) { palette_pending = (bank & 3) << 2; }

void gfx_cursor(u16 x, u16 y, u8 visible, u8 locked)
{
    u8 plane, at=0;
    /* x/y describe the screen-space centre; non-interlace Y adds one. */
    x-=15; y-=16;
    for(plane=0;plane!=2;++plane) {
        cursor_pending[at++]=(u8)y; cursor_pending[at++]=0;
        cursor_pending[at++]=(u8)(y>>8)&1; cursor_pending[at++]=0;
        cursor_pending[at++]=(u8)x; cursor_pending[at++]=0;
        cursor_pending[at++]=((u8)(x>>8)&3) | (visible?0:0x10) |
            (plane?0x80:(locked?0xC0:0x40));
        cursor_pending[at++]=0;
    }
}

void gfx_rect(u16 x, u16 y, u16 w, u16 h, u8 color)
{
    /* Zero means 2048/4096 to the V9990, so reject empty rectangles here. */
    if (!w || !h || x >= 256 || y >= 2048) return;
    if (w > 256 - x) w = 256 - x;
    if (h > 2048 - y) h = 2048 - y;
    gfx_fill(x, y, w, h, color);
}

void gfx_fill(u16 x, u16 y, u16 w, u16 h, u8 color)
{
    if (!w || !h) return;
    color &= 15;
    color |= color << 4;
    gfx_wait();
    hw_reg_select = 36;
    word_write(x);
    word_write(y);
    word_write(w);
    word_write(h);
    hw_reg_data = 0;
    hw_reg_data = 0x0C;
    hw_reg_data = 0xFF;
    hw_reg_data = 0xFF;
    hw_reg_data = color;
    hw_reg_data = color;     /* Four identical palette indices per word. */
    reg_write(52, 0x20);     /* LMMV */
}

void gfx_copy(u16 sx, u16 sy, u16 dx, u16 dy, u16 w, u16 h, u8 transparent)
{
    u8 arg = 0;
    if (!w || !h || sx >= 256 || dx >= 256 || sy >= 2048 || dy >= 2048) return;
    if (w > 256 - sx) w = 256 - sx;
    if (w > 256 - dx) w = 256 - dx;
    if (h > 2048 - sy) h = 2048 - sy;
    if (h > 2048 - dy) h = 2048 - dy;
    /* memmove-like direction for overlapping rectangles; harmless for assets. */
    if (dy > sy && dy < sy + h) {
        sy += h - 1;
        dy += h - 1;
        arg |= 8;
    }
    if (dx > sx && dx < sx + w) {
        sx += w - 1;
        dx += w - 1;
        arg |= 4;
    }
    gfx_wait();
    hw_reg_select = 32;
    word_write(sx);
    word_write(sy);
    word_write(dx);
    word_write(dy);
    word_write(w);
    word_write(h);
    hw_reg_data = arg;
    hw_reg_data = transparent ? 0x1C : 0x0C;
    hw_reg_data = 0xFF;
    hw_reg_data = 0xFF;
    reg_write(52, 0x40);     /* LMMM */
}

void gfx_blit(u16 sx, u16 sy, u16 dx, u16 dy, u16 w, u16 h, u8 transparent)
{
    if (!w || !h) return;
    gfx_wait();
    hw_reg_select = 32;
    word_write(sx);
    word_write(sy);
    word_write(dx);
    word_write(dy);
    word_write(w);
    word_write(h);
    hw_reg_data = 0;
    hw_reg_data = transparent ? 0x1C : 0x0C;
    hw_reg_data = 0xFF;
    hw_reg_data = 0xFF;
    reg_write(52, 0x40);
}

/* Consecutive 7x7 transparent copies from atlas row 512. No other command
 * may be issued between begin and the final bullet. Keep invariant command
 * registers resident instead of rewriting all sixteen bytes per bullet. */
void gfx_bullets_begin(void)
{
    gfx_wait();
    hw_reg_select = 34;
    hw_reg_data = 0; hw_reg_data = 2;
    hw_reg_select = 40;
    hw_reg_data = 7; hw_reg_data = 0;
    hw_reg_data = 7; hw_reg_data = 0;
    hw_reg_data = 0; hw_reg_data = 0x1C;
    hw_reg_data = 0xFF; hw_reg_data = 0xFF;
}
void gfx_bullet(u8 sx, u8 dx, u16 dy)
{
    gfx_wait();
    hw_reg_select = 32;
    hw_reg_data = sx; hw_reg_data = 0;
    /* LMMM advances SY internally: restore it for EVERY copy. */
    hw_reg_data = 0; hw_reg_data = 2;
    hw_reg_data = dx; hw_reg_data = 0;
    hw_reg_data = (u8)dy; hw_reg_data = (u8)(dy >> 8);
    hw_reg_select = 52; hw_reg_data = 0x40;
}

void gfx_line(u16 x0, u16 y0, u16 x1, u16 y1, u8 color)
{
    u16 dx, dy, major, minor;
    u8 arg = 0;
    if (x0 >= 256 || x1 >= 256 || y0 >= 2048 || y1 >= 2048) return;
    if (x1 < x0) { dx = x0 - x1; arg |= 4; }
    else dx = x1 - x0;
    if (y1 < y0) { dy = y0 - y1; arg |= 8; }
    else dy = y1 - y0;
    if (!dx && !dy) { gfx_rect(x0, y0, 1, 1, color); return; }
    if (dy > dx) { major = dy; minor = dx; arg |= 1; }
    else { major = dx; minor = dy; }
    color &= 15;
    color |= color << 4;
    gfx_wait();
    hw_reg_select = 36;
    word_write(x0);
    word_write(y0);
    word_write(major);
    word_write(minor);
    hw_reg_data = arg;
    hw_reg_data = 0x0C;
    hw_reg_data = 0xFF;
    hw_reg_data = 0xFF;
    hw_reg_data = color;
    hw_reg_data = color;
    reg_write(52, 0xB0);     /* LINE */
}

static void background_palette(void) {
 extern u16 tick;u8 phase=(tick>>8)%3;
 /* Hold each picture, then fade to the next during the last half-cycle. */
 u8 f=(tick&255)<128?0:((tick&127)>>4)+1;
 u8 a=phase==0?8-f:phase==2?f:0;
 u8 b=phase==1?8-f:phase==0?f:0;
 u8 c=phase==2?8-f:phase==1?f:0;
 reg_write(14,8*4);hw_palette=a;hw_palette=a/2;hw_palette=a/4;
 reg_write(14,14*4);hw_palette=b/4;hw_palette=b;hw_palette=b;
 hw_palette=c;hw_palette=c/2;hw_palette=c;
}
void gfx_flip(u8 page)
{
    u8 i;
    gfx_wait();
    gfx_vblank();
    reg_write(13, palette_pending);
    background_palette();
    reg_write(18, page & 1);
    /* Publish cursor attributes with the framebuffer they point into. */
    vram_write_address(0xFE00,7);
    for (i=0;i!=16;++i) hw_vram=cursor_pending[i];
    /* R18 high Y is frame-latched. Do not recycle the old front buffer
     * until active display has begun with the new page selected. */
    while (hw_status & 0x40) { }
}

u8 input_read(void)
{
    u8 keys, result = 0;
    u8 ppi = hw_key_select;
    u8 joy_control;
    hw_key_select = (ppi & 0xF0) | 8;
    keys = (u8)~hw_key_data;
    if (keys & 0x10) result |= INPUT_LEFT;
    if (keys & 0x80) result |= INPUT_RIGHT;
    if (keys & 0x20) result |= INPUT_UP;
    if (keys & 0x40) result |= INPUT_DOWN;
    if (keys & 0x01) result |= INPUT_FIRE;
    hw_key_select = (ppi & 0xF0) | 5;
    if (!(hw_key_data & 0x20)) result |= INPUT_BOMB;  /* X */
    hw_key_select = (ppi & 0xF0) | 7;
    if (!(hw_key_data & 0x04)) result |= INPUT_PAUSE; /* ESC */
    if (!(hw_key_data & 0x08)) result |= INPUT_LAB;   /* TAB */
    hw_key_select = ppi;

    /* Preserve the current sound mixer while enforcing MSX I/O directions. */
    hw_psg_select = 7;
    hw_psg_write = (hw_psg_read & 0x3F) | 0x80;
    hw_psg_select = 15;
    joy_control = hw_psg_read;
    /* Joystick 1, pin 8 low, pins 6/7 released for trigger input. */
    hw_psg_write = (joy_control & 0xAF) | 0x03;
    hw_psg_select = 14;
    keys = (u8)~hw_psg_read;
    if (keys & 0x04) result |= INPUT_LEFT;
    if (keys & 0x08) result |= INPUT_RIGHT;
    if (keys & 0x01) result |= INPUT_UP;
    if (keys & 0x02) result |= INPUT_DOWN;
    if (keys & 0x10) result |= INPUT_FIRE;
    if (keys & 0x20) result |= INPUT_BOMB;
    hw_psg_select = 15;
    hw_psg_write = joy_control;
    return result;
}

void aura_upload(void) {
 u8 y,x;const u8 *p=aura_pixels;gfx_wait();
 for(y=0;y<32;++y){vram_write_address((u16)(560+y)*128,1);for(x=0;x<64;++x)hw_vram=*p++;}
}

static const u8 nazca_cat[][4]={{25,123,30,112},{30,112,42,105},{42,105,69,100},{69,100,87,96},{87,96,119,99},{119,99,146,105},{146,105,157,99},{157,99,167,91},{167,91,178,93},{178,93,182,79},{182,79,184,54},{184,54,188,48},{188,48,194,54},{194,54,204,75},{204,75,216,66},{216,66,225,60},{225,60,229,66},{229,66,229,84},{229,84,232,98},{232,98,230,113},{230,113,224,125},{224,125,212,131},{212,131,197,130},{197,130,186,123},{186,123,179,112},{179,112,175,101},{175,101,178,93},{25,123,48,122},{48,122,70,128},{70,128,81,128},{76,131,70,142},{70,142,70,156},{70,156,76,171},{76,171,86,184},{86,184,98,188},{98,188,109,183},{109,183,115,174},{115,174,114,164},{114,164,102,153},{102,153,93,147},{93,147,94,132},{94,132,101,119},{101,119,116,115},{116,115,132,119},{132,119,135,124},{135,124,130,131},{130,131,114,135},{114,135,98,134},{172,119,161,121},{161,121,148,120},{148,120,141,126},{141,126,143,135},{143,135,154,140},{154,140,168,138},{168,138,179,132},{179,132,184,123},{184,123,180,119},{180,119,172,119},{185,131,183,143},{183,143,172,151},{172,151,167,158},{167,158,177,161},{177,161,187,156},{187,156,194,146},{204,136,200,148},{200,148,192,157},{38,115,47,110},{47,110,63,108},{63,108,68,110},{68,110,66,115},{49,111,47,118},{55,110,54,116},{62,110,60,116},{189,93,190,86},{190,86,197,85},{197,85,200,91},{200,91,197,96},{197,96,191,96},{191,96,189,93},{210,93,211,86},{211,86,219,87},{219,87,221,94},{221,94,216,97},{216,97,210,96},{210,96,210,93},{197,111,202,114},{202,114,210,114},{210,114,216,110}};
static const u8 nazca_bird[][4]={{14,73,99,98},{99,98,109,93},{109,93,115,93},{115,93,121,98},{121,98,126,94},{126,94,135,26},{135,26,139,25},{139,25,141,28},{141,28,134,95},{134,95,138,99},{138,99,142,96},{142,96,150,39},{150,39,154,38},{154,38,157,41},{157,41,148,100},{148,100,152,102},{152,102,156,99},{156,99,165,59},{165,59,169,58},{169,58,171,61},{171,61,162,101},{162,101,165,104},{165,104,169,101},{169,101,177,76},{177,76,180,75},{180,75,182,78},{182,78,175,101},{175,101,178,101},{178,101,187,79},{187,79,190,80},{190,80,182,103},{182,103,185,104},{185,104,195,82},{195,82,198,84},{198,84,188,108},{188,108,180,110},{180,110,180,113},{180,113,216,119},{216,119,218,123},{218,123,215,125},{215,125,188,122},{188,122,187,126},{187,126,231,131},{231,131,232,135},{232,135,229,137},{229,137,187,133},{187,133,185,137},{185,137,245,144},{245,144,246,148},{246,148,242,150},{242,150,183,143},{183,143,181,147},{181,147,230,155},{230,155,231,159},{231,159,228,161},{228,161,179,153},{179,153,175,155},{175,155,211,164},{211,164,212,168},{212,168,208,170},{208,170,172,160},{172,160,168,162},{168,162,176,174},{176,174,175,180},{175,180,171,180},{171,180,165,168},{165,168,161,168},{161,168,167,186},{167,186,164,190},{164,190,160,189},{160,189,154,175},{154,175,151,175},{151,175,155,191},{155,191,151,194},{151,194,147,190},{147,190,142,176},{142,176,145,155},{145,155,140,151},{140,151,131,151},{131,151,120,183},{120,183,116,185},{116,185,114,181},{114,181,126,147},{126,147,124,144},{124,144,106,195},{106,195,102,195},{102,195,121,140},{121,140,118,138},{118,138,96,203},{96,203,92,203},{92,203,113,135},{113,135,111,131},{111,131,14,77}};
static const u8 nazca_figures[][4]={{17,89,15,63},{15,63,17,50},{17,50,25,42},{25,42,39,38},{39,38,56,40},{56,40,70,47},{70,47,78,60},{78,60,80,87},{80,87,72,89},{72,89,71,66},{71,66,68,57},{68,57,56,52},{56,52,38,51},{38,51,27,57},{27,57,26,85},{27,85,32,79},{32,79,45,77},{45,77,60,79},{60,79,68,85},{68,85,68,97},{68,97,63,107},{63,107,52,113},{52,113,40,112},{40,112,29,105},{29,105,26,95},{26,95,27,85},{34,65,36,61},{36,61,40,61},{40,61,43,65},{43,65,41,70},{41,70,36,70},{36,70,34,65},{57,65,59,61},{59,61,63,63},{63,63,64,68},{64,68,60,71},{60,71,57,68},{57,68,57,65},{37,91,43,88},{43,88,54,88},{54,88,58,93},{58,93,55,98},{55,98,44,100},{44,100,38,97},{38,97,37,91},{49,114,56,119},{56,119,58,133},{58,133,59,167},{59,167,62,169},{62,169,70,168},{70,168,71,172},{71,172,60,176},{60,176,55,173},{55,173,53,137},{53,137,46,135},{46,135,44,139},{44,139,46,171},{46,171,41,175},{41,175,31,178},{31,178,29,175},{29,175,39,170},{39,170,39,133},{39,133,37,121},{116,104,120,84},{120,84,127,65},{127,65,139,51},{139,51,151,44},{151,44,162,44},{162,44,169,50},{169,50,170,55},{170,55,167,57},{167,57,163,50},{163,50,156,49},{156,49,146,53},{146,53,136,66},{136,66,128,83},{128,83,121,106},{121,106,118,108},{118,108,116,104},{129,107,132,88},{132,88,140,72},{140,72,151,63},{151,63,161,62},{161,62,164,66},{164,66,160,68},{160,68,155,67},{155,67,145,76},{145,76,137,92},{137,92,134,109},{134,109,130,111},{130,111,129,107},{186,54,191,41},{191,41,199,35},{199,35,206,35},{206,35,216,41},{216,41,228,55},{228,55,240,76},{240,76,248,96},{248,96,246,100},{246,100,241,96},{241,96,231,76},{231,76,221,58},{221,58,211,46},{211,46,202,42},{202,42,196,46},{196,46,191,56},{191,56,187,58},{187,58,186,54},{195,66,199,60},{199,60,204,58},{204,58,211,62},{211,62,223,78},{223,78,234,101},{234,101,234,108},{234,108,230,107},{230,107,219,83},{219,83,208,68},{208,68,203,66},{203,66,199,70},{199,70,195,70},{195,70,195,66},{163,79,194,79},{194,79,199,82},{199,82,198,99},{198,99,194,114},{194,114,187,121},{187,121,175,123},{175,123,164,118},{164,118,159,109},{159,109,157,87},{157,87,157,82},{157,82,163,79},{165,89,168,85},{168,85,173,86},{173,86,175,90},{175,90,172,94},{172,94,167,94},{167,94,165,89},{185,88,188,85},{188,85,192,88},{192,88,192,92},{192,92,188,94},{188,94,185,91},{185,91,185,88},{172,108,175,104},{175,104,182,104},{182,104,186,109},{186,109,183,115},{183,115,176,117},{176,117,172,113},{172,113,172,108},{149,123,157,128},{157,128,170,134},{170,134,173,140},{173,140,169,160},{169,160,164,174},{164,174,158,179},{158,179,150,178},{150,178,140,171},{140,171,133,161},{133,161,129,159},{129,159,127,163},{127,163,131,172},{131,172,143,183},{143,183,154,188},{154,188,164,187},{164,187,174,179},{174,179,181,157},{181,157,186,179},{186,179,191,186},{191,186,203,188},{203,188,215,185},{215,185,224,180},{224,180,226,175},{226,175,223,173},{223,173,215,179},{215,179,204,181},{204,181,195,179},{195,179,191,171},{191,171,187,141},{187,141,192,135},{192,135,214,126},{214,126,216,122},{216,122,213,121},{213,121,188,131},{188,131,178,134},{178,134,168,130},{168,130,149,118},{149,118,145,119},{145,119,146,122},{146,122,149,123},{175,145,177,141},{177,141,181,142},{181,142,183,146},{183,146,180,150},{180,150,176,149},{176,149,175,145}};
void background_init(void) {
 u8 i;
 gfx_fill(0,768,256,212,0);
 for(i=0;i<sizeof(nazca_cat)/4;++i) gfx_line(nazca_cat[i][0],768+nazca_cat[i][1],nazca_cat[i][2],768+nazca_cat[i][3],8);
 for(i=0;i<sizeof(nazca_bird)/4;++i) gfx_line(nazca_bird[i][0],768+nazca_bird[i][1],nazca_bird[i][2],768+nazca_bird[i][3],14);
 for(i=0;i<sizeof(nazca_figures)/4;++i) gfx_line(nazca_figures[i][0],768+nazca_figures[i][1],nazca_figures[i][2],768+nazca_figures[i][3],15);
 gfx_wait();
}

u8 disk_keys(void){u8 p=hw_key_select,k=0;hw_key_select=(p&0xf0)|5;if(!(hw_key_data&1))k|=1;hw_key_select=(p&0xf0)|4;if(!(hw_key_data&2))k|=2;hw_key_select=p;return k;}

void title_upload(void){u16 n;const u8 *p=(const u8*)0x6000;*(volatile u8*)0x6800=4;vram_write_address(0xf400,1);for(n=0;n<6656;++n)hw_vram=*p++;}
