#include <pthread.h>

int state;

void interrupt_handler(void) {
  int local_value = 1;
  if (state)
    state = local_value;
}

int main(void) {
  int local_value = 0;
  if (state)
    local_value = 1;
  state = local_value;
  return 0;
}
