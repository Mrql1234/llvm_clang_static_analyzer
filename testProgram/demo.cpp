#include <cstdlib>
#include <iostream>

int divide(int a, int b) {
    return a / b;
}

int main() {
    int *p = nullptr;

    if (std::getenv("USE_PTR")) {
        std::cout << *p << "\n";  // potential null dereference
    }

    int x = 10;
    int y = 0;
    std::cout << divide(x, y) << "\n";  // divide by zero

    return 0;
}