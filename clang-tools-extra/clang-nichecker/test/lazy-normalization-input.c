extern void __VERIFIER_assume(int);

int main(void) {
  int x = 0;
  while (x < 0) {
  }
  do {
    x++;
    x += 2;
  } while (x < 3);
  for (;;) {
    x--;
    break;
  }
  return x;
}
