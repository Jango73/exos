void debug(const char* Format, ...);

int Fibonacci(int Number) {
    if (Number < 2) {
        return Number;
    }
    return Fibonacci(Number - 1) + Fibonacci(Number - 2);
}

int main(void) {
    int Result;

    Result = Fibonacci(10);
    debug("EDIT_SMOKE_RECURSION:Fibonacci=%d", Result);
    return 0;
}
