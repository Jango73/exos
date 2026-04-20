
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.


    Regex

\************************************************************************/

#include "utils/Regex.h"

#include "Base.h"
#include "text/CoreString.h"

/************************************************************************/
// Internal helpers

static void MemZero(LPVOID Ptr, U32 Size) {
    U8* P = (U8*)Ptr;
    while (Size--) *P++ = 0;
}

static void ClassClear(CHAR_CLASS* C) {
    MemZero(C->Bits, (U32)sizeof(C->Bits));
    C->Neg = 0;
}

static void ClassSet(CHAR_CLASS* C, U32 Ch) { C->Bits[Ch >> 3] |= (U8)(1u << (Ch & 7)); }

static BOOL ClassHas(CONST CHAR_CLASS* C, U32 Ch) {
    U8 In = (U8)((C->Bits[Ch >> 3] >> (Ch & 7)) & 1u);
    return C->Neg ? (In == 0) : (In != 0);
}

static void ClassAddRange(CHAR_CLASS* C, U32 A, U32 B) {
    if (A > B) {
        U32 T = A;
        A = B;
        B = T;
    }
    for (U32 X = A; X <= B; ++X) ClassSet(C, X);
}

/************************************************************************/

/**
 * @brief Parses an escape sequence from regex pattern.
 *
 * Supported escape sequences:
 * - \n → newline (0x0A)
 * - \r → carriage return (0x0D)
 * - \t → tab (0x09)
 * - \\, \[, \], \., \*, \+, \?, \^, \$, \- → literal characters
 * - \<other> → treated as literal <other>
 *
 * @param P Pointer to pattern string pointer (advanced past escape sequence)
 * @param OutCh Pointer to store the resulting character
 * @return TRUE if valid escape sequence parsed, FALSE if malformed
 */
static BOOL ReadEscapedChar(LPCSTR* P, U8* OutCh) {
    LPCSTR S = *P;
    if (*S != '\\') return FALSE;
    ++S;
    STR C = *S;
    if (C == STR_NULL) return FALSE;
    switch (C) {
        case 'n':
            *OutCh = (U8)'\n';
            break;
        case 'r':
            *OutCh = (U8)'\r';
            break;
        case 't':
            *OutCh = (U8)'\t';
            break;
        case '\\':
        case '[':
        case ']':
        case '.':
        case '*':
        case '+':
        case '?':
        case '^':
        case '$':
        case '-':
            *OutCh = (U8)C;
            break;
        default:
            *OutCh = (U8)C; /* treat unknown escapes as literal char */
            break;
    }
    *P = S + 1;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Parses a character class pattern like [abc], [a-z], or [^0-9].
 *
 * Supported syntax:
 * - [abc] → matches 'a', 'b', or 'c'
 * - [a-z] → matches any lowercase letter
 * - [^0-9] → matches any character except digits
 * - [\n\t] → matches newline or tab (with escapes)
 * - [a-zA-Z0-9_] → matches alphanumeric plus underscore
 *
 * Features:
 * - Range syntax: a-z, A-Z, 0-9
 * - Negation: ^ as first character
 * - Escape sequences: \n, \t, \\, etc.
 * - 256-bit bitmap for character matching
 *
 * @param P Pointer to pattern string pointer (advanced past ']')
 * @param Out Character class structure to populate
 * @return TRUE if valid class parsed, FALSE on syntax error or missing ']'
 */
static BOOL ParseClass(LPCSTR* P, CHAR_CLASS* Out) {
    LPCSTR S = *P;
    if (*S != '[') return FALSE;

    ++S;
    ClassClear(Out);

    /* Negation */
    if (*S == '^') {
        Out->Neg = 1;
        ++S;
    }

    while (*S && *S != ']') {
        U8 A = 0;
        if (*S == '\\') {
            if (!ReadEscapedChar(&S, &A)) return FALSE;
        } else {
            A = (U8)*S++;
        }

        if (*S == '-' && S[1] != ']' && S[1] != STR_NULL) {
            ++S;
            U8 B = 0;
            if (*S == '\\') {
                if (!ReadEscapedChar(&S, &B)) return FALSE;
            } else {
                B = (U8)*S++;
            }
            ClassAddRange(Out, (U32)A, (U32)B);
        } else {
            ClassSet(Out, (U32)A);
        }
    }

    if (*S != ']') return FALSE; /* missing ']' */
    *P = S + 1;
    return TRUE;
}

/************************************************************************/

// Append a token to Out->Tokens
static BOOL EmitToken(REGEX* Out, TOKEN_TYPE Type, U8 Ch, CONST CHAR_CLASS* Cls) {
    if (Out->TokenCount >= REGEX_MAX_TOKENS) return FALSE;
    TOKEN* T = &Out->Tokens[Out->TokenCount++];
    T->Type = Type;
    T->Ch = Ch;
    if (Cls) {
        T->Class = *Cls;
    } else {
        MemZero(&T->Class, (U32)sizeof(T->Class));
    }
    return TRUE;
}

/************************************************************************/

/**
 * @brief Compiles a regular expression pattern into an internal token representation.
 *
 * Supported features:
 * - Literal characters: 'a', 'b', 'hello'
 * - Dot wildcard: '.' (matches any single character)
 * - Character classes: '[abc]', '[a-z]', '[^0-9]' (with negation and ranges)
 * - Quantifiers: '*' (zero or more), '+' (one or more), '?' (zero or one)
 * - Anchors: '^' (beginning of line), '$' (end of line)
 * - Escape sequences: '\n', '\t', '\r', '\\', '\[', '\]', etc.
 *
 * Limitations:
 * - No groups or captures: '()', '\1', etc.
 * - No alternation: '|'
 * - No word boundaries: '\b'
 * - No predefined classes: '\d', '\w', '\s'
 * - Pattern limited to REGEX_MAX_PATTERN-1 characters (1023)
 * - Token stream limited to REGEX_MAX_TOKENS (512)
 *
 * @param Pattern Regular expression pattern string
 * @param OutRegex Output structure to store compiled regex
 * @return TRUE if compilation succeeded, FALSE on syntax error or limits exceeded
 */
BOOL RegexCompile(CONST LPCSTR Pattern, REGEX* OutRegex) {
    if (OutRegex == NULL || Pattern == NULL) return FALSE;

    MemZero(OutRegex, (U32)sizeof(*OutRegex));

    /* Copy pattern (bounded) */
    U32 L = StringLength(Pattern);
    if (L >= REGEX_MAX_PATTERN) L = REGEX_MAX_PATTERN - 1;
    for (U32 i = 0; i < L; ++i) OutRegex->Pattern[i] = (U8)Pattern[i];
    OutRegex->Pattern[L] = 0;

    LPCSTR P = Pattern;

    // Optional leading ^
    if (*P == '^') {
        if (!EmitToken(OutRegex, TT_BOL, 0, NULL)) return FALSE;
        OutRegex->AnchorBOL = 1;
        ++P;
    }

    while (*P != STR_NULL) {
        STR C = *P;

        if (C == '$' && P[1] == STR_NULL) {
            if (!EmitToken(OutRegex, TT_EOL, 0, NULL)) return FALSE;
            OutRegex->AnchorEOL = 1;
            ++P;
            break;
        } else if (C == '.') {
            if (!EmitToken(OutRegex, TT_DOT, 0, NULL)) return FALSE;
            ++P;
        } else if (C == '[') {
            CHAR_CLASS CC;
            if (!ParseClass(&P, &CC)) return FALSE;
            if (!EmitToken(OutRegex, TT_CLASS, 0, &CC)) return FALSE;
        } else if (C == '*' || C == '+' || C == '?') {
            /* quantifier applies to previous atom */
            TOKEN_TYPE Q = (C == '*') ? TT_STAR : (C == '+') ? TT_PLUS : TT_QMARK;
            /* must have something before */
            if (OutRegex->TokenCount == 0) return FALSE;
            /* previous must be an atom (CHAR/DOT/CLASS or EOL/BOL is invalid target) */
            TOKEN_TYPE Prev = OutRegex->Tokens[OutRegex->TokenCount - 1].Type;
            if (!(Prev == TT_CHAR || Prev == TT_DOT || Prev == TT_CLASS)) return FALSE;
            if (!EmitToken(OutRegex, Q, 0, NULL)) return FALSE;
            ++P;
        } else if (C == '\\') {
            U8 Lit = 0;
            if (!ReadEscapedChar(&P, &Lit)) return FALSE;
            if (!EmitToken(OutRegex, TT_CHAR, Lit, NULL)) return FALSE;
        } else if (C == '^') {
            /* '^' in middle treated as literal unless you want multiline */
            if (!EmitToken(OutRegex, TT_CHAR, (U8)'^', NULL)) return FALSE;
            ++P;
        } else if (C == '$') {
            /* '$' in middle treated as literal (simple policy) */
            if (!EmitToken(OutRegex, TT_CHAR, (U8)'$', NULL)) return FALSE;
            ++P;
        } else {
            if (!EmitToken(OutRegex, TT_CHAR, (U8)C, NULL)) return FALSE;
            ++P;
        }
    }

    /* End marker */
    if (!EmitToken(OutRegex, TT_END, 0, NULL)) return FALSE;

    OutRegex->CompileOk = 1;
    return TRUE;
}

/************************************************************************/

// Matching engine (tokens)

/**
 * @brief Matches a single atomic pattern element against one character.
 *
 * Handles three types of atomic patterns:
 * - TT_CHAR: Exact character match
 * - TT_DOT: Wildcard (matches any single character)
 * - TT_CLASS: Character class match using bitmap lookup
 *
 * @param Atom Token representing the atomic pattern to match
 * @param Text Current position in input text
 * @param NextText Output pointer to next character position if match succeeds
 * @return TRUE if atom matches and advances one character, FALSE otherwise
 */
static BOOL MatchOne(CONST TOKEN* Atom, CONST U8* Text, CONST U8** NextText) {
    if (*Text == 0) return FALSE;

    switch (Atom->Type) {
        case TT_CHAR:
            if ((U8)*Text != Atom->Ch) return FALSE;
            *NextText = Text + 1;
            return TRUE;
        case TT_DOT:
            *NextText = Text + 1;
            return TRUE;
        case TT_CLASS:
            if (!ClassHas(&Atom->Class, (U32)(U8)*Text)) return FALSE;
            *NextText = Text + 1;
            return TRUE;
        default:
            return FALSE;
    }
}

/************************************************************************/

static BOOL MatchHere(CONST TOKEN* Toks, U32 PosTok, CONST U8* Text);

/************************************************************************/

static BOOL MatchRepeatGreedy(CONST TOKEN* Toks, U32 AtomPos, TOKEN_TYPE Quant, U32 AfterPos, CONST U8* Text) {
    /* Count how many chars match the atom greedily */
    CONST TOKEN* Atom = &Toks[AtomPos];
    CONST U8* P = Text;
    CONST U8* Q = Text;

    U32 Count = 0;
    CONST U8* Next = P;
    while (MatchOne(Atom, P, &Next)) {
        P = Next;
        ++Count;
    }

    /* For '+' we need at least 1 char; for '*' zero is fine */
    U32 Min = (Quant == TT_PLUS) ? 1u : 0u;

    /* Try backtracking from max to min */
    for (U32 Take = Count; Take + 1u > 0u; --Take) {
        if (Take < Min) break;
        Q = Text;
        for (U32 i = 0; i < Take; ++i) {
            CONST U8* Tmp = Q;
            if (!MatchOne(Atom, Q, &Tmp)) return FALSE; /* should not happen */
            Q = Tmp;
        }
        if (MatchHere(Toks, AfterPos, Q)) return TRUE;
        if (Take == 0) break;
    }
    return FALSE;
}

/************************************************************************/

static BOOL MatchOptional(CONST TOKEN* Toks, U32 AtomPos, U32 AfterPos, CONST U8* Text) {
    CONST U8* Next = Text;
    if (MatchOne(&Toks[AtomPos], Text, &Next)) {
        if (MatchHere(Toks, AfterPos, Next)) return TRUE;
    }
    return MatchHere(Toks, AfterPos, Text);
}

/************************************************************************/

static BOOL MatchHere(CONST TOKEN* Toks, U32 PosTok, CONST U8* Text) {
    FOREVER {
        CONST TOKEN* T = &Toks[PosTok];

        switch (T->Type) {
            case TT_END:
                return TRUE;

            case TT_EOL:
                /* EOL only matches if we're at end of string */
                return (*Text == 0);

            case TT_CHAR:
            case TT_DOT:
            case TT_CLASS: {
                /* Lookahead for quantifier */
                TOKEN_TYPE NextType = Toks[PosTok + 1].Type;
                if (NextType == TT_STAR || NextType == TT_PLUS) {
                    return MatchRepeatGreedy(Toks, PosTok, NextType, PosTok + 2, Text);
                } else if (NextType == TT_QMARK) {
                    return MatchOptional(Toks, PosTok, PosTok + 2, Text);
                } else {
                    CONST U8* NextText = Text;
                    if (!MatchOne(T, Text, &NextText)) return FALSE;
                    Text = NextText;
                    ++PosTok;
                    continue;
                }
            }

            case TT_BOL:
                /* Must be at start (handled by caller if not anchored) */
                if (Text != (CONST U8*)Text) { /* unreachable, kept for symmetry */
                    return FALSE;
                }
                ++PosTok;
                continue;

            default:
                return FALSE;
        }
    }
}

/************************************************************************/

/**
 * @brief Tests if a compiled regex matches anywhere in the input text.
 *
 * Matching behavior:
 * - Without '^': Tries to match at every position in the text (substring match)
 * - With '^': Only matches at the beginning of text (anchored match)
 * - With '$': Only succeeds if pattern matches up to end of text
 * - Returns TRUE if any match is found, FALSE otherwise
 *
 * Examples:
 * - Pattern "hello" matches "hello", "say hello world", "hello there"
 * - Pattern "^hello" matches "hello world" but not "say hello"
 * - Pattern "world$" matches "hello world" but not "world hello"
 *
 * @param Rx Compiled regex structure (must have CompileOk=1)
 * @param Text Input text to search in (null-terminated string)
 * @return TRUE if pattern matches anywhere in text, FALSE otherwise
 */
BOOL RegexMatch(CONST REGEX* Rx, CONST LPCSTR Text) {
    if (Rx == NULL || Rx->CompileOk == 0 || Text == NULL) return FALSE;

    CONST TOKEN* Toks = Rx->Tokens;

    if (Rx->AnchorBOL) {
        /* anchored at start */
        return MatchHere(Toks, 0, (CONST U8*)Text);
    } else {
        /* try every position */
        CONST U8* S = (CONST U8*)Text;
        for (; *S; ++S) {
            if (MatchHere(Toks, 0, S)) return TRUE;
        }
        /* allow matching empty at end if pattern can match empty and $ present */
        return MatchHere(Toks, 0, S);
    }
}

/************************************************************************/

/**
 * @brief Finds the first match in text and returns its position span.
 *
 * Search behavior:
 * - Scans text from left to right looking for first match
 * - Returns position as [start, end) where end is exclusive
 * - For anchored patterns (^), only checks position 0
 * - Uses greedy matching for quantifiers (*, +, ?)
 *
 * Position calculation:
 * - OutStart: byte offset where match begins (0-based)
 * - OutEnd: byte offset where match ends (exclusive)
 * - Match span is Text[OutStart] to Text[OutEnd-1]
 *
 * Examples:
 * - Pattern "ell" in "hello" returns start=1, end=4
 * - Pattern "^he" in "hello" returns start=0, end=2
 * - Pattern "lo$" in "hello" returns start=3, end=5
 *
 * @param Rx Compiled regex structure (must have CompileOk=1)
 * @param Text Input text to search in
 * @param OutStart Pointer to store match start position (can be NULL)
 * @param OutEnd Pointer to store match end position (can be NULL)
 * @return TRUE if match found with positions set, FALSE if no match
 */
BOOL RegexSearch(CONST REGEX* Rx, CONST LPCSTR Text, U32* OutStart, U32* OutEnd) {
    if (Rx == NULL || Rx->CompileOk == 0 || Text == NULL) return FALSE;

    CONST TOKEN* Toks = Rx->Tokens;

    if (Rx->AnchorBOL) {
        CONST U8* S = (CONST U8*)Text;
        CONST U8* T = S;
        if (!MatchHere(Toks, 0, S)) return FALSE;

        /* naive way to find end: advance until next char fails */
        while (*T && MatchHere(Toks, 0, T)) ++T;
        if (OutStart) *OutStart = 0;
        if (OutEnd) *OutEnd = (U32)(T - (CONST U8*)Text);
        return TRUE;
    } else {
        CONST U8* Base = (CONST U8*)Text;
        for (CONST U8* S = Base;; ++S) {
            if (MatchHere(Toks, 0, S)) {
                /* find shortest end >= S */
                CONST U8* T = S;
                while (*T && MatchHere(Toks, 0, T)) ++T;
                if (OutStart) *OutStart = (U32)(S - Base);
                if (OutEnd) *OutEnd = (U32)(T - Base);
                return TRUE;
            }
            if (*S == 0) break;
        }
        return FALSE;
    }
}

/************************************************************************/

/**
 * @brief Releases resources associated with a compiled regex.
 *
 * In the current implementation (V1), no dynamic memory is allocated
 * during compilation, so this function is a no-op. All regex data
 * is stored in the REGEX structure itself on the stack or static storage.
 *
 * This function is provided for API completeness and future versions
 * that may use dynamic allocation.
 *
 * @param Rx Compiled regex to free (ignored, can be NULL)
 */
void RegexFree(REGEX* Rx) {
    /* No dynamic allocation in V1 */
    UNUSED(Rx);
}
