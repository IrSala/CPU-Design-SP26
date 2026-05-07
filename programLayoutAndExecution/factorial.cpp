// factorial.cpp
// Recursive factorial with a main() driver.
// Compile: g++ -std=c++17 -o factorial factorial.cpp
// Run:     ./factorial

#include <iostream>

// Recursive factorial: n! = n * (n-1)!
// Base case: 0! = 1
unsigned long long factorial(unsigned int n) {
    if (n == 0) return 1;          // base case
    return n * factorial(n - 1);  // recursive call
}

int main() {
    for (unsigned int i = 0; i <= 10; ++i) {
        std::cout << i << "! = " << factorial(i) << "\n";
    }
    return 0;
}
