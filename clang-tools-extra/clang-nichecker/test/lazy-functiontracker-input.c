int consume(int left, int right) { return left + right; }

int main(void) {
  int first = 0;
  int second = 3;
  consume(first++, second--);
  return first + second;
}
