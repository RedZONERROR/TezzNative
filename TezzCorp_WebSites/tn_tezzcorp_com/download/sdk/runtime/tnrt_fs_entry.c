// runtime/tnrt_fs_entry.c
// Bare-metal entry stub for freestanding targets.

extern int main(void);

void _start(void){
  (void)main();
  for(;;){
    // halt/spin
  }
}
