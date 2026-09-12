#ifndef SATORI_EXPERT_DISK_H
#define SATORI_EXPERT_DISK_H

/* This edition has independent simulation rules and one page-1 replay buffer. */
#define REPLAY_FILENAME "SATORIX.RPL"
#define REPLAY_FCB_NAME "SATORIX RPL"
#define REPLAY_RULE_ID 3
#define REPLAY_CAPACITY 16384

void disk_init(void);
unsigned char disk_save(void);
unsigned char disk_load(void);

#endif
