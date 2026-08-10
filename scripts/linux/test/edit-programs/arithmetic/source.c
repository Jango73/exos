void debug(const char* Format, ...);

int ComputeSum(int MaxValue) {
    int Index;
    int Total = 0;

    for (Index = 1; Index <= MaxValue; Index++) {
        Total += Index;
    }
    return Total;
}

int main(void) {
    int Sum;

    Sum = ComputeSum(10);
    debug("EDIT_SMOKE_ARITHMETIC:Sum=%d", Sum);
    return 0;
}
