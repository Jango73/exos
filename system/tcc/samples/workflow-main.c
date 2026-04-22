int WorkflowCalculateChecksum(int BaseValue);
int WorkflowScaleValue(int Value);

int main(void) {
    int Checksum;
    int Scaled;

    Checksum = WorkflowCalculateChecksum(0x15);
    Scaled = WorkflowScaleValue(Checksum);

    return Scaled == 0x87 ? 0 : 1;
}
