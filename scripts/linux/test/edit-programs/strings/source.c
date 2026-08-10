void debug(const char* Format, ...);

int CountVowels(const char* Text) {
    int Index;
    int Count = 0;

    for (Index = 0; Text[Index] != 0; Index++) {
        char Character = Text[Index];
        if (Character == 'a' || Character == 'e' || Character == 'i' || Character == 'o' || Character == 'u') {
            Count++;
        }
    }
    return Count;
}

int main(void) {
    int Vowels;

    Vowels = CountVowels("the quick brown fox");
    debug("EDIT_SMOKE_STRINGS:Vowels=%d", Vowels);
    return 0;
}
