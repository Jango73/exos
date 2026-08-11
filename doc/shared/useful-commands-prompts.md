# Useful Commands

## Perl

Remove DEBUG(...) calls, including multi-line invocations:

```bash
perl -0pi -e 's/^[ \t]*DEBUG\([^;]*?\);\n//smg' some-file.c
```

# Useful Prompts

## Summary for branch

Write a commit-ready branch summary in English of up to 8 lines. Each line must be one change theme for the whole branch (group related commits), not commit-by-commit. No hashes, no file lists, no bullets, no numbering, no headers. Each line must be a concise action sentence starting with a strong past-tense verb (Implemented/Added/Refactored/Fixed/Improved/Introduced/Renamed/Hardened). Keep each line under 20 words. Cover all major areas touched in the branch. Write the result in temp/commit.txt : replace the file if existing.
