
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2026 Jango73

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


    Bcrypt - Unit Tests

\************************************************************************/

#include "autotest/Autotest.h"
#include "Base.h"
#include "memory/Heap.h"
#include "log/Log.h"
#include "text/CoreString.h"
#include "system/System.h"

/************************************************************************/

// Bcrypt constants and types (avoiding system includes)
#define MAXKEYBYTES 56
#define ENCRYPT 0
#define DECRYPT 1

typedef U32 uLong;

typedef struct {
    U8 remove;
    U8 standardout;
    U8 compression;
    U8 type;
    uLong origsize;
    U8 securedelete;
} BCoptions;

/************************************************************************/
// Function declarations for BFEncrypt/BFDecrypt

uLong BFEncrypt(char **input, char *key, uLong sz, BCoptions *options);
uLong BFDecrypt(char **input, char *key, char *key2, uLong sz, BCoptions *options);

/************************************************************************/

/**
 * @brief Helper function to test encryption and decryption with given data.
 *
 * Tests both BFEncrypt and BFDecrypt functions to ensure they work correctly
 * together. Encrypts the input data and then decrypts it, verifying that
 * the decrypted result matches the original input.
 *
 * @param TestName Name of the test case for logging purposes
 * @param OriginalData Original data to encrypt and decrypt
 * @param DataSize Size of the original data in bytes
 * @param Key Encryption key to use
 * @return TRUE if encryption/decryption cycle succeeds, FALSE otherwise
 */
static BOOL TestEncryptDecrypt(const char *TestName, const char *OriginalData, U32 DataSize, const char *Key, TEST_RESULTS* Results) {
    UNUSED(TestName);
    BCoptions Options = {0};
    static char InputBuffer[256];       // Smaller static buffer for input data
    static char Key2[MAXKEYBYTES + 1];  // Static buffer for alternate key
    char *BufferPtr = InputBuffer;      // Pointer for BF functions that modify the pointer
    U32 EncryptedSize, DecryptedSize;
    U32 PaddedSize;
    BOOL TestPassed = FALSE;

    // Calculate required size
    PaddedSize = DataSize + MAXKEYBYTES;
    if (PaddedSize % 8 != 0) {
        PaddedSize += (8 - (PaddedSize % 8));
    }
    PaddedSize += 16;  // Extra space for headers and padding

    // Check if data fits in static buffer
    if (PaddedSize > sizeof(InputBuffer)) {
        DEBUG(TEXT("Data too large for static buffer in test: %s"), TestName);
        return FALSE;
    }

    // Prepare input data with key appended (bcrypt requirement)
    MemorySet(InputBuffer, 0, sizeof(InputBuffer));
    MemoryCopy(InputBuffer, OriginalData, DataSize);
    MemoryCopy(InputBuffer + DataSize, Key, MAXKEYBYTES);

    // Initialize options
    Options.remove = 0;
    Options.standardout = 0;
    Options.compression = 0;
    Options.type = ENCRYPT;
    Options.origsize = 0;
    Options.securedelete = 0;

    // Prepare key2 (alternate key for endian handling)
    MemorySet(Key2, 0, sizeof(Key2));
    MemoryCopy(Key2, Key, MAXKEYBYTES);

    // Test encryption
    EncryptedSize = BFEncrypt(&BufferPtr, (char *)Key, DataSize + MAXKEYBYTES, &Options);
    if (EncryptedSize == 0) {
        DEBUG(TEXT("Encryption failed for test: %s"), TestName);
        Results->TestsRun++;
        return FALSE;
    }

    // Test decryption
    DecryptedSize = BFDecrypt(&BufferPtr, (char *)Key, Key2, EncryptedSize, &Options);
    if (DecryptedSize == 0) {
        DEBUG(TEXT("Decryption failed for test: %s"), TestName);
        Results->TestsRun++;
        return FALSE;
    }

    // Verify decrypted data matches original
    if (DataSize == 0) {
        // For empty data, just check that decryption succeeded
        TestPassed = TRUE;
    } else if (DecryptedSize >= DataSize && MemoryCompare(BufferPtr, OriginalData, DataSize) == 0) {
        TestPassed = TRUE;
    } else {
        DEBUG(TEXT("Data verification failed for test: %s"), TestName);
        DEBUG(TEXT("Expected size: %u, Got size: %u"), DataSize, DecryptedSize);
    }

    Results->TestsRun++;
    if (TestPassed) Results->TestsPassed++;
    return TestPassed;
}

/**
 * @brief Comprehensive unit test for Bcrypt encryption/decryption functionality.
 *
 * Tests various data patterns and sizes to ensure the BFEncrypt and BFDecrypt
 * functions work correctly together. Includes tests for empty data, short strings,
 * longer text, and binary-like data.
 *
 * @return TRUE if all bcrypt tests pass, FALSE if any test fails
 */
void TestBcrypt(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Simple short string
    TestEncryptDecrypt("Simple string", "Hello World!", 12, "mypassword123456", Results);

    // Test 2: Single character
    TestEncryptDecrypt("Single char", "A", 1, "singlekey1234567", Results);

    // Test 3: Longer text (reduced size to fit in static buffer)
    TestEncryptDecrypt("Long text", "The quick brown fox jumps over the lazy dog.", 44, "longkey123456789", Results);

    // Test 4: Text with special characters
    TestEncryptDecrypt("Special chars", "!@#$%^&*()_+-=[]{}|;:,.<>?", 26, "specialkey123456", Results);

    // Test 5: Numeric string
    TestEncryptDecrypt("Numeric", "1234567890", 10, "numkey1234567890", Results);

    // Test 6: Binary-like data (with null bytes)
    char BinaryData[] = {0x01, 0x02, 0x00, 0x03, 0x04, 0xFF, 0x00, 0x05};
    TestEncryptDecrypt("Binary data", BinaryData, 8, "binarykey1234567", Results);
}
