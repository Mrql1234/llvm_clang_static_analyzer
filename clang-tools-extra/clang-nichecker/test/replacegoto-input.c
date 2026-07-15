void IF(unsigned Thread, unsigned Pc, unsigned NextLabel);
static unsigned tworker_1;

void worker(void) {
tworker_0: IF(1, 0, tworker_1);
  goto __exit_loop_0;
__exit_loop_0:
  ;
tworker_1:
  ;
}

int main(void) {
  worker();
  return 0;
}
