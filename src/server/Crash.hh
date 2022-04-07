#ifndef CRASH_HH
#define CRASH_HH

#define INCLUDE_CRASH_POINTS

#ifdef INCLUDE_CRASH_POINTS

#include <stdio.h>
#include <unistd.h>

#include <thread>

#define PREP_CRASH_ON_MESSAGE 0x10000u
#define CRASH_PRIMARY_BEFORE_BACKUP 0x11000u
#define CRASH_PRIMARY_AFTER_WRITE 0x12000u
#define CRASH_BACKUP_DURING_BACKUP 0x13000u
#define CRASH_BACKUP_AFTER_BACKUP 0x14000u
#define PREP_CRASH_ON_NEXT_RECOVER 0x15000u

void crash();
void crash_after(int delay_sec);
void make_crash_sentinel();
void clear_crash_sentinel();
bool has_crash_sentinel();
#endif
#endif