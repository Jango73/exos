#!/bin/bash

set -euo pipefail

print_help() {
    cat <<'EOF'
Usage:
  bash scripts/linux/utils/amend-staged-into-commit.sh --commit <hash>
  bash scripts/linux/utils/amend-staged-into-commit.sh [-c <hash>]
  bash scripts/linux/utils/amend-staged-into-commit.sh [-h|--help]

Behavior:
  - Amends the staged changes into the target commit.
  - Preserves tracked unstaged changes by saving and restoring them.
  - Refuses to run unless the target commit is specified explicitly.
  - Uses a direct amend when the target is HEAD.
  - Uses fixup + autosquash rebase when the target is older than HEAD.

Examples:
  bash scripts/linux/utils/amend-staged-into-commit.sh --commit HEAD
  bash scripts/linux/utils/amend-staged-into-commit.sh --commit 3c51fd6
EOF
}

ScriptFolder="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RepositoryRoot="$(cd "${ScriptFolder}/../../.." && pwd)"

TargetRef=""
TemporaryPatch=""
ShouldRestorePatch=0
ShouldDeletePatch=0

cleanup() {
    local ExitCode="$1"

    if [ "${ExitCode}" -eq 0 ]; then
        if [ "${ShouldDeletePatch}" -eq 1 ] && [ -n "${TemporaryPatch}" ] && [ -f "${TemporaryPatch}" ]; then
            rm -f "${TemporaryPatch}"
        fi
        return
    fi

    if [ "${ShouldRestorePatch}" -eq 1 ] && [ -n "${TemporaryPatch}" ] && [ -f "${TemporaryPatch}" ]; then
        echo "Tracked unstaged changes were saved in: ${TemporaryPatch}" >&2
        echo "Reapply them with: git apply --3way \"${TemporaryPatch}\"" >&2
    fi
}

trap 'cleanup $?' EXIT

while [ "$#" -gt 0 ]; do
    case "$1" in
        -c|--commit)
            if [ "$#" -lt 2 ]; then
                echo "Missing value for $1" >&2
                exit 2
            fi
            TargetRef="$2"
            shift 2
            ;;
        -h|--help)
            print_help
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            print_help >&2
            exit 2
            ;;
    esac
done

cd "${RepositoryRoot}"

if ! git rev-parse --git-dir >/dev/null 2>&1; then
    echo "This script must run inside a Git repository." >&2
    exit 1
fi

if [ -z "${TargetRef}" ]; then
    echo "No target commit specified." >&2
    print_help >&2
    exit 2
fi

if ! git diff --cached --quiet --ignore-submodules --; then
    :
else
    echo "No staged changes to amend." >&2
    exit 1
fi

if [ -d ".git/rebase-merge" ] || [ -d ".git/rebase-apply" ]; then
    echo "A rebase is already in progress." >&2
    exit 1
fi

if [ -f ".git/MERGE_HEAD" ]; then
    echo "A merge is already in progress." >&2
    exit 1
fi

if [ -f ".git/CHERRY_PICK_HEAD" ]; then
    echo "A cherry-pick is already in progress." >&2
    exit 1
fi

TargetCommit="$(git rev-parse --verify "${TargetRef}^{commit}")"
HeadCommit="$(git rev-parse --verify HEAD)"

if ! git merge-base --is-ancestor "${TargetCommit}" "${HeadCommit}"; then
    echo "Target commit is not an ancestor of HEAD: ${TargetCommit}" >&2
    exit 1
fi

if ! git diff --quiet --ignore-submodules --; then
    TemporaryPatch="$(mktemp /tmp/exos-amend-staged-into-commit.XXXXXX.patch)"
    git diff -- > "${TemporaryPatch}"
    git checkout -- .
    ShouldRestorePatch=1
fi

if [ "${TargetCommit}" = "${HeadCommit}" ]; then
    git commit --amend --no-edit
else
    git commit --fixup="${TargetCommit}"

    if ParentCommit="$(git rev-parse --verify "${TargetCommit}^" 2>/dev/null)"; then
        GIT_SEQUENCE_EDITOR=true git rebase -i --autosquash "${ParentCommit}"
    else
        GIT_SEQUENCE_EDITOR=true git rebase -i --root --autosquash
    fi
fi

if [ "${ShouldRestorePatch}" -eq 1 ]; then
    git apply --3way "${TemporaryPatch}"
    ShouldRestorePatch=0
    ShouldDeletePatch=1
fi

echo "Amend completed for target: ${TargetRef}"
echo "Current HEAD: $(git rev-parse --short HEAD)"
