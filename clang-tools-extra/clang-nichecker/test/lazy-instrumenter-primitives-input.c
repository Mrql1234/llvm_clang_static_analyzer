#include <pthread.h>

int state;

extern int __VERIFIER_nondet_int(void);
extern void __VERIFIER_assume(int);

void *worker(void *argument) {
  state = __VERIFIER_nondet_int();
  __VERIFIER_assume(state >= 0);
  return argument;
}

int main(void) {
  pthread_t thread;
  pthread_create(&thread, 0, worker, 0);
  pthread_join(thread, 0);
  return 0;
}
