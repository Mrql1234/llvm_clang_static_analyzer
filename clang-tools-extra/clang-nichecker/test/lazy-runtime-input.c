#include <pthread.h>

pthread_mutex_t mutex;
pthread_cond_t condition;

void *worker(void *argument) {
  pthread_mutex_lock(&mutex);
  pthread_cond_wait(&condition, &mutex);
  pthread_mutex_unlock(&mutex);
  return argument;
}

int main(void) {
  pthread_t thread;
  pthread_mutex_init(&mutex, 0);
  pthread_cond_init(&condition, 0);
  pthread_create(&thread, 0, worker, 0);
  pthread_cond_signal(&condition);
  pthread_join(thread, 0);
  return 0;
}
