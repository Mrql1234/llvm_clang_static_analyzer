#include <pthread.h>

int released;
pthread_mutex_t lock;

void cleanup(void *value) {
  released = *(int *)value;
}

void *worker(void *arg) {
  if (*(int *)arg) {
    pthread_mutex_lock(&lock);
    pthread_mutex_unlock(&lock);
    return arg;
  }
  return 0;
}

int main(void) {
  pthread_t thread;
  int value = 1;
  pthread_create(&thread, 0, worker, &value);
  return 0;
}
