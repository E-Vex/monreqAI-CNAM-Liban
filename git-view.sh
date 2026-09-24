#!/usr/bin/env bash

echo "===== UNSTAGED DIFF ====="
git diff

echo
echo "===== STAGED DIFF ====="
git diff --cached

echo
echo "===== NEW / UNTRACKED FILES ====="

git status --porcelain | awk '$1 == "??" {print substr($0, 4)}' |
while IFS= read -r file; do
    echo
    echo "========================================"
    echo "FILE: $file"
    echo "========================================"
    cat -- "$file"
    echo
done
