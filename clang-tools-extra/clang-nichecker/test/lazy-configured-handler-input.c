int state;

void deferred_handler(void) {
  state = 2;
}

int main(void) {
  state = 0;
  return 0;
}
