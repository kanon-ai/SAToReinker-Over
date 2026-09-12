#ifndef SATOR_EXPERT_PATTERNS_H
#define SATOR_EXPERT_PATTERNS_H
#include "hardware.h"

/* Kept byte-for-byte compatible with the indexed Z80 collision/draw loop. */
typedef struct { int x,y; signed char vx,vy; u8 live,graze,color; } Bullet;
extern Bullet bullets[192];

#define PATTERN_COUNT 31
#define PATTERN_START_TICK 600
#define PATTERN_DURATION 240
#define PATTERN_WARNING 32
#define PATTERN_EMIT_END 150
#define PATTERN_HINT_TOP 0
#define PATTERN_HINT_BELOW 1
#define PATTERN_HINT_SIDES 2

extern u8 pattern_id, pattern_hint, pattern_busy, pattern_active;
extern u16 tick;
extern u16 pattern_age;
extern u8 pattern_emitter_count, pattern_emitter_x[2], pattern_emitter_y[2];

/* Returns the allocated bullet slot, or 255 when all 192 slots are occupied.
 * The game applies its one-time 1.3 speed scale to these base velocities. */
u8 expert_spawn(int x,int y,signed char dx,signed char dy,u8 color);
void patterns_reset(void);
void patterns_step(u8 level);
void patterns_slot_reset(u8 slot);
const char *patterns_name(void);
u8 patterns_allow_laser(void);
#endif
