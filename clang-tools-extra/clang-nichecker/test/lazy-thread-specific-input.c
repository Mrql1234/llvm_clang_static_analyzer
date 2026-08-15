#include <pthread.h>

pthread_key_t key;

void cleanup(void *value) {
  (void)value;
}

void *worker(void *argument) {
  pthread_setspecific(key, argument);
  return pthread_getspecific(key);
}

int main(void) {
  pthread_t thread;
  pthread_key_create(&key, cleanup);
  pthread_create(&thread, 0, worker, 0);
  pthread_join(thread, 0);
  return 0;
}
