static int WorkflowRotateNibble(int Value) {
    return ((Value << 1) | (Value >> 3)) & 0x0F;
}

int WorkflowCalculateChecksum(int BaseValue) {
    int Index;
    int Sum;

    Sum = 0;
    for (Index = 0; Index < 4; Index++) {
        Sum += WorkflowRotateNibble(BaseValue + Index);
    }

    return Sum;
}
