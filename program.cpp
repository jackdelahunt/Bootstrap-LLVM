#include <iostream>

extern "C" {
    int get_number();
}

int main() {
    std::cout << get_number() << "\n";
}