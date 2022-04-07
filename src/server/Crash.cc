#include "Crash.hh"

void crash() {
    *((char*)0) = 0;
}
void crash_after(int delay_sec) {
    std::thread([delay_sec] {
        sleep(delay_sec);
        crash();
    }).detach();
}
void make_crash_sentinel() {
    auto f = fopen("crash_sentinel", "w");
    fclose(f);
}
void clear_crash_sentinel() {
    remove("crash_sentinel");
}
bool has_crash_sentinel() {
    return access("crash_sentinel", F_OK) == 0;
}