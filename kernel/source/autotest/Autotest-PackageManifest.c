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


    Package manifest parser autotests

\************************************************************************/

#include "autotest/Autotest.h"

#include "text/CoreString.h"
#include "log/Log.h"
#include "User.h"
#include "package/PackageManifest.h"

/***************************************************************************/

/**
 * @brief Assert one boolean condition in package manifest tests.
 * @param Condition Assertion condition.
 * @param Results Test aggregate result.
 * @param Message Failure message.
 */
static void PackageManifestAssert(BOOL Condition, TEST_RESULTS* Results, LPCSTR Message) {
    UNUSED(Message);
    if (Results == NULL) return;

    Results->TestsRun++;
    if (Condition) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Assertion failed: %s"), Message);
    }
}

/***************************************************************************/

/**
 * @brief Return architecture token expected by this kernel build.
 * @return Arch value compatible with current build.
 */
static LPCSTR TestPackageManifestCurrentArch(void) {
#if defined(__EXOS_ARCH_X86_32__)
    return TEXT("x86-32");
#elif defined(__EXOS_ARCH_X86_64__)
    return TEXT("x86-64");
#else
    return TEXT("unknown");
#endif
}

/***************************************************************************/

/**
 * @brief Return one architecture token incompatible with current build.
 * @return Incompatible architecture token.
 */
static LPCSTR TestPackageManifestOtherArch(void) {
#if defined(__EXOS_ARCH_X86_32__)
    return TEXT("x86-64");
#elif defined(__EXOS_ARCH_X86_64__)
    return TEXT("x86-32");
#else
    return TEXT("x86-32");
#endif
}

/***************************************************************************/

/**
 * @brief Parse nominal top-level manifest and validate compatibility.
 * @param Results Test aggregate result.
 */
static void TestPackageManifestTopLevelAndCompatibility(TEST_RESULTS* Results) {
    STR ManifestText[768];
    PACKAGE_MANIFEST Manifest;
    U32 Status;

    StringPrintFormat(
        ManifestText,
        TEXT("name = \"shell\"\n"
             "version = \"1.2.3\"\n"
             "arch = \"%s\"\n"
             "kernel_api = \"%u.%u\"\n"
             "entry = \"/binary/shell.elf\"\n"
             "[commands]\n"
             "shell = \"/binary/shell.elf\"\n"
             "mesh-view = \"/binary/mesh-view.elf\"\n"),
        TestPackageManifestCurrentArch(),
        EXOS_VERSION_MAJOR,
        EXOS_VERSION_MINOR);

    Status = PackageManifestParseText(ManifestText, &Manifest);
    PackageManifestAssert(Status == PACKAGE_MANIFEST_STATUS_OK, Results, TEXT("top-level parse status"));

    if (Status == PACKAGE_MANIFEST_STATUS_OK) {
        PackageManifestAssert(StringCompare(Manifest.Name, TEXT("shell")) == 0, Results, TEXT("name"));
        PackageManifestAssert(StringCompare(Manifest.Version, TEXT("1.2.3")) == 0, Results, TEXT("version"));
        PackageManifestAssert(StringCompare(Manifest.Entry, TEXT("/binary/shell.elf")) == 0, Results, TEXT("entry"));
        PackageManifestAssert(Manifest.CommandCount == 2, Results, TEXT("command count"));
        PackageManifestAssert(StringCompare(Manifest.Commands[0].Name, TEXT("shell")) == 0, Results, TEXT("command[0] name"));
        PackageManifestAssert(
            StringCompare(Manifest.Commands[1].Target, TEXT("/binary/mesh-view.elf")) == 0,
            Results,
            TEXT("command[1] target"));

        Status = PackageManifestCheckCompatibility(&Manifest);
        PackageManifestAssert(Status == PACKAGE_MANIFEST_STATUS_OK, Results, TEXT("compatibility status"));
    }

    PackageManifestRelease(&Manifest);
}

/***************************************************************************/

/**
 * @brief Parse manifest with [package] section keys.
 * @param Results Test aggregate result.
 */
static void TestPackageManifestPackageSection(TEST_RESULTS* Results) {
    STR ManifestText[768];
    PACKAGE_MANIFEST Manifest;
    U32 Status;

    StringPrintFormat(
        ManifestText,
        TEXT("[package]\n"
             "name = \"netget\"\n"
             "version = \"0.9.0\"\n"
             "arch = \"%s\"\n"
             "kernel_api = \"%u.%u\"\n"
             "entry = \"/binary/netget.elf\"\n"
             "[package.commands]\n"
             "netget = \"/binary/netget.elf\"\n"),
        TestPackageManifestCurrentArch(),
        EXOS_VERSION_MAJOR,
        EXOS_VERSION_MINOR);

    Status = PackageManifestParseText(ManifestText, &Manifest);
    PackageManifestAssert(Status == PACKAGE_MANIFEST_STATUS_OK, Results, TEXT("section parse status"));
    if (Status == PACKAGE_MANIFEST_STATUS_OK) {
        PackageManifestAssert(StringCompare(Manifest.Name, TEXT("netget")) == 0, Results, TEXT("section name"));
        PackageManifestAssert(Manifest.CommandCount == 1, Results, TEXT("section command count"));
    }

    PackageManifestRelease(&Manifest);
}

/***************************************************************************/

/**
 * @brief Validate missing mandatory fields.
 * @param Results Test aggregate result.
 */
static void TestPackageManifestMissingFields(TEST_RESULTS* Results) {
    static const STR MissingName[] =
        "version = \"1.0\"\n"
        "arch = \"x86-32\"\n"
        "kernel_api = \"0.5\"\n"
        "entry = \"/binary/app.elf\"\n";
    static const STR MissingVersion[] =
        "name = \"pkg\"\n"
        "arch = \"x86-32\"\n"
        "kernel_api = \"0.5\"\n"
        "entry = \"/binary/app.elf\"\n";
    static const STR MissingArch[] =
        "name = \"pkg\"\n"
        "version = \"1.0\"\n"
        "kernel_api = \"0.5\"\n"
        "entry = \"/binary/app.elf\"\n";
    static const STR MissingKernelApi[] =
        "name = \"pkg\"\n"
        "version = \"1.0\"\n"
        "arch = \"x86-32\"\n"
        "entry = \"/binary/app.elf\"\n";
    static const STR MissingEntry[] =
        "name = \"pkg\"\n"
        "version = \"1.0\"\n"
        "arch = \"x86-32\"\n"
        "kernel_api = \"0.5\"\n";
    PACKAGE_MANIFEST Manifest;
    U32 Status;

    Status = PackageManifestParseText((LPCSTR)MissingName, &Manifest);
    PackageManifestAssert(Status == PACKAGE_MANIFEST_STATUS_MISSING_NAME, Results, TEXT("missing name"));

    Status = PackageManifestParseText((LPCSTR)MissingVersion, &Manifest);
    PackageManifestAssert(Status == PACKAGE_MANIFEST_STATUS_MISSING_VERSION, Results, TEXT("missing version"));

    Status = PackageManifestParseText((LPCSTR)MissingArch, &Manifest);
    PackageManifestAssert(Status == PACKAGE_MANIFEST_STATUS_MISSING_ARCH, Results, TEXT("missing arch"));

    Status = PackageManifestParseText((LPCSTR)MissingKernelApi, &Manifest);
    PackageManifestAssert(Status == PACKAGE_MANIFEST_STATUS_MISSING_KERNEL_API, Results, TEXT("missing kernel api"));

    Status = PackageManifestParseText((LPCSTR)MissingEntry, &Manifest);
    PackageManifestAssert(Status == PACKAGE_MANIFEST_STATUS_MISSING_ENTRY, Results, TEXT("missing entry"));

    PackageManifestRelease(&Manifest);
}

/***************************************************************************/

/**
 * @brief Validate dependency graph keys are rejected.
 * @param Results Test aggregate result.
 */
static void TestPackageManifestForbiddenDependencyGraph(TEST_RESULTS* Results) {
    static const STR ManifestWithRequires[] =
        "name = \"pkg\"\n"
        "version = \"1.0\"\n"
        "arch = \"x86-32\"\n"
        "kernel_api = \"0.5\"\n"
        "entry = \"/binary/app.elf\"\n"
        "requires = [\"api.core\"]\n";
    PACKAGE_MANIFEST Manifest;
    U32 Status;

    Status = PackageManifestParseText((LPCSTR)ManifestWithRequires, &Manifest);
    PackageManifestAssert(
        Status == PACKAGE_MANIFEST_STATUS_FORBIDDEN_DEPENDENCY_GRAPH,
        Results,
        TEXT("forbidden dependency graph"));

    PackageManifestRelease(&Manifest);
}

/***************************************************************************/

/**
 * @brief Validate manifest entry and command map path policy.
 * @param Results Test aggregate result.
 */
static void TestPackageManifestPathPolicy(TEST_RESULTS* Results) {
    static const STR InvalidEntryPath[] =
        "name = \"pkg\"\n"
        "version = \"1.0\"\n"
        "arch = \"x86-32\"\n"
        "kernel_api = \"0.5\"\n"
        "entry = \"/package/binary/app.elf\"\n";
    static const STR InvalidCommandTarget[] =
        "name = \"pkg\"\n"
        "version = \"1.0\"\n"
        "arch = \"x86-32\"\n"
        "kernel_api = \"0.5\"\n"
        "entry = \"/binary/app.elf\"\n"
        "[commands]\n"
        "bad = \"/binary/../escape.elf\"\n";
    static const STR DuplicateCommands[] =
        "name = \"pkg\"\n"
        "version = \"1.0\"\n"
        "arch = \"x86-32\"\n"
        "kernel_api = \"0.5\"\n"
        "entry = \"/binary/app.elf\"\n"
        "[commands]\n"
        "tool = \"/binary/tool-a.elf\"\n"
        "[package.commands]\n"
        "tool = \"/binary/tool-b.elf\"\n";
    PACKAGE_MANIFEST Manifest;
    U32 Status;

    Status = PackageManifestParseText((LPCSTR)InvalidEntryPath, &Manifest);
    PackageManifestAssert(Status == PACKAGE_MANIFEST_STATUS_INVALID_ENTRY_PATH, Results, TEXT("invalid entry path"));
    PackageManifestRelease(&Manifest);

    Status = PackageManifestParseText((LPCSTR)InvalidCommandTarget, &Manifest);
    PackageManifestAssert(
        Status == PACKAGE_MANIFEST_STATUS_INVALID_COMMAND_MAP,
        Results,
        TEXT("invalid command target"));
    PackageManifestRelease(&Manifest);

    Status = PackageManifestParseText((LPCSTR)DuplicateCommands, &Manifest);
    PackageManifestAssert(
        Status == PACKAGE_MANIFEST_STATUS_DUPLICATE_COMMAND_NAME,
        Results,
        TEXT("duplicate command names"));
    PackageManifestRelease(&Manifest);
}

/***************************************************************************/

/**
 * @brief Validate deterministic compatibility failures.
 * @param Results Test aggregate result.
 */
static void TestPackageManifestCompatibilityFailures(TEST_RESULTS* Results) {
    PACKAGE_MANIFEST Manifest;
    U32 Status;

    MemorySet(&Manifest, 0, sizeof(Manifest));
    StringCopy(Manifest.Name, TEXT("pkg"));
    StringCopy(Manifest.Version, TEXT("1.0"));
    StringCopy(Manifest.Entry, TEXT("/binary/app.elf"));

    StringCopy(Manifest.Arch, TEXT("mips"));
    StringCopy(Manifest.KernelApi, TEXT("0.5"));
    Status = PackageManifestCheckCompatibility(&Manifest);
    PackageManifestAssert(Status == PACKAGE_MANIFEST_STATUS_INVALID_ARCH, Results, TEXT("invalid arch"));

    StringCopy(Manifest.Arch, TestPackageManifestOtherArch());
    Status = PackageManifestCheckCompatibility(&Manifest);
    PackageManifestAssert(Status == PACKAGE_MANIFEST_STATUS_INCOMPATIBLE_ARCH, Results, TEXT("incompatible arch"));

    StringCopy(Manifest.Arch, TestPackageManifestCurrentArch());
    StringCopy(Manifest.KernelApi, TEXT("broken"));
    Status = PackageManifestCheckCompatibility(&Manifest);
    PackageManifestAssert(Status == PACKAGE_MANIFEST_STATUS_INVALID_KERNEL_API, Results, TEXT("invalid kernel api"));

    StringPrintFormat(Manifest.KernelApi, TEXT("%u.%u"), EXOS_VERSION_MAJOR + 1, 0);
    Status = PackageManifestCheckCompatibility(&Manifest);
    PackageManifestAssert(
        Status == PACKAGE_MANIFEST_STATUS_INCOMPATIBLE_KERNEL_API,
        Results,
        TEXT("incompatible kernel api major"));

    StringPrintFormat(Manifest.KernelApi, TEXT("%u.%u"), EXOS_VERSION_MAJOR, EXOS_VERSION_MINOR + 1);
    Status = PackageManifestCheckCompatibility(&Manifest);
    PackageManifestAssert(
        Status == PACKAGE_MANIFEST_STATUS_INCOMPATIBLE_KERNEL_API,
        Results,
        TEXT("incompatible kernel api minor"));
}

/***************************************************************************/

/**
 * @brief Run package manifest parser test suite.
 * @param Results Test aggregate result.
 */
void TestPackageManifest(TEST_RESULTS* Results) {
    if (Results == NULL) return;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    TestPackageManifestTopLevelAndCompatibility(Results);
    TestPackageManifestPackageSection(Results);
    TestPackageManifestMissingFields(Results);
    TestPackageManifestForbiddenDependencyGraph(Results);
    TestPackageManifestPathPolicy(Results);
    TestPackageManifestCompatibilityFailures(Results);
}
