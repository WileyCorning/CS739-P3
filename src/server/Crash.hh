#ifndef CRASH_HH
#define CRASH_HH

#define INCLUDE_CRASH_POINTS

#ifdef INCLUDE_CRASH_POINTS

#include <stdio.h>
#include <unistd.h>

#include <thread>

void crash();
void crash_after(int delay_sec);
void make_crash_sentinel();
void clear_crash_sentinel();
bool has_crash_sentinel();
#endif
#endif