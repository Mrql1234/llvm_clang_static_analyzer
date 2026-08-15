#include <pthread.h>

void *worker(void *argument);

void *worker(void *argument) {
  return argument;
}

int main(void) {
  pthread_t first;
  pthread_t second;
  worker(0);
  pthread_create(&first, 0, worker, 0);
  pthread_create(&second, 0, worker, 0);
  pthread_join(first, 0);
  pthread_join(second, 0);
  return 0;
}
