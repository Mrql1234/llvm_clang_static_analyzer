#include <pthread.h>

int state = 0;

void *writer(void *unused) {
  state = 1;
  state = 2;
  return 0;
}

void *reader(void *unused) {
  int first;
  int second;
  int third;
  first = state;
  second = state;
  state = 3;
  third = state;
  return 0;
}

int main(void) {
  pthread_t writer_thread;
  pthread_t reader_thread;
  pthread_create(&writer_thread, 0, writer, 0);
  pthread_create(&reader_thread, 0, reader, 0);
  pthread_join(writer_thread, 0);
  pthread_join(reader_thread, 0);
  return 0;
}
