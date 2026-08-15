int helper(int value) {
  if (value < 0)
    return 3;
  return value + 1;
}

int main(void) {
  int result;
  result = helper(4);
  return helper(result);
}
