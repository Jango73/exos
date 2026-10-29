scc --exclude-dir doc --exclude-dir node_modules --exclude-dir third --exclude-dir build --exclude-dir log --exclude-dir scripts --by-file --sort complexity --format html > log/quality.html
if command -v xdg-open >/dev/null 2>&1; then
    xdg-open log/quality.html
fi
