int state;

// priority 1
void interrupt_low(void) {
  state = 1;
}

// priority 2
void interrupt_high(void) {
  state = 2;
}

int main(void) {
  state = 0;
  return 0;
}
