#include <pthread.h>

typedef struct Item {
  int field;
} Item;

extern void __VERIFIER_atomic_end(void);
extern void __CSEQ_noop(void);

void *worker(void *argument) {
  return argument;
}

int main(void) {
  pthread_t thread;
  Item item = {1};
  Item *pointer = &item;
  volatile int qualified = pointer->field;
  int value = 1;
  if (0)
    value = 2;
  if (!1) {
    value = 3;
  }
  pthread_create(&thread, 0, (void *(*)(void *))worker, 0);
  __VERIFIER_atomic_end();
  return value + qualified;
}
