typedef unsigned long pthread_t;

int released;

void cleanup(void *value) {
  released = *(int *)value;
}

void *worker(void *arg) {
  if (*(int *)arg)
    return arg;
  return 0;
}

int main(void) {
  pthread_t thread;
  int value = 1;
  pthread_create(&thread, 0, worker, &value);
  return 0;
}
