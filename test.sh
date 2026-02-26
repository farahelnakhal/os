#!/bin/bash

SHELL_CMD="./myshell"
PASS=0; FAIL=0; TOTAL=0
GREEN='\033[0;32m'; RED='\033[0;31m'; NC='\033[0m'

pass() { echo -e "${GREEN}[PASS]${NC} $1"; PASS=$((PASS+1)); TOTAL=$((TOTAL+1)); }
fail() { echo -e "${RED}[FAIL]${NC} $1\n       Expected: '$2'\n       Got: '$3'"; FAIL=$((FAIL+1)); TOTAL=$((TOTAL+1)); }

#send cmd to shell, check stdout+stderr contains string
check() {
    local name="$1" input="$2" expected="$3"
    actual=$(printf '%s\nexit\n' "$input" | $SHELL_CMD 2>&1)
    echo "$actual" | grep -qF "$expected" && pass "$name" || fail "$name" "$expected" "$actual"
}

#send cmd to shell, check a file contains string afterwards
check_file() {
    local name="$1" input="$2" file="$3" expected="$4"
    rm -f "$file"
    printf '%s\nexit\n' "$input" | $SHELL_CMD 2>&1 >/dev/null; sleep 0.1
    [ -f "$file" ] && grep -qF "$expected" "$file" && pass "$name" || fail "$name" "$expected in $file" "$(cat $file 2>/dev/null)"
}

#check file exists and is non-empty
check_file_nonempty() {
    local name="$1" input="$2" file="$3"
    rm -f "$file"
    printf '%s\nexit\n' "$input" | $SHELL_CMD 2>&1 >/dev/null; sleep 0.1
    [ -f "$file" ] && [ -s "$file" ] && pass "$name" || fail "$name" "non-empty $file" "missing or empty"
}

#check string is NOT in output
check_absent() {
    local name="$1" input="$2" absent="$3"
    actual=$(printf '%s\nexit\n' "$input" | $SHELL_CMD 2>&1)
    echo "$actual" | grep -qF "$absent" && fail "$name" "no '$absent'" "$actual" || pass "$name"
}

# =============================================================================
mkdir -p test_tmp && cd test_tmp
SHELL_CMD="../myshell"
printf 'hello world\nline one\nline two\nfoo\nbar\nbaz\n' > input.txt
printf '#!/bin/bash\necho "hello from script"\n' > hello.sh && chmod +x hello.sh
printf '#!/bin/bash\necho "err" >&2\n' > errscript.sh && chmod +x errscript.sh

echo -e "\n========================================\n P1 Testing \n========================================"

# --- Basic Commands ----------------------------------------------------------
check         "pwd"                      "pwd"                     "/"
check         "uname"                    "uname"                   "Linux"
check         "echo with args"           "echo hello world"        "hello world"
check         "ls -l"                    "ls -l"                   "total"
check         "cat file"                 "cat input.txt"           "hello world"
check         "grep in file"             "grep hello input.txt"    "hello world"
check         "wc -l"                    "wc -l input.txt"         "6"
check         "sort"                     "sort input.txt"          "bar"

# --- Output Redirection ------------------------------------------------------
check_file    "echo > file"              "echo hello > out.txt"            "out.txt"  "hello"
check_file    "cat > file"               "cat input.txt > cat_out.txt"     "cat_out.txt"  "hello world"
printf 'echo first > ow.txt\nexit\n' | $SHELL_CMD >/dev/null 2>&1
printf 'echo second > ow.txt\nexit\n' | $SHELL_CMD >/dev/null 2>&1
TOTAL=$((TOTAL+1)); grep -q "second" ow.txt && ! grep -q "first" ow.txt && pass "> truncates (no append)" || fail "> truncates (no append)" "only 'second'" "$(cat ow.txt)"

# --- Input Redirection -------------------------------------------------------
check         "cat < file"               "cat < input.txt"          "hello world"
check         "grep < file"              "grep line < input.txt"    "line one"
check         "wc -l < file"             "wc -l < input.txt"        "6"

# --- Error Redirection -------------------------------------------------------
check_file_nonempty "invalid cmd 2> file"     "invalidcmd_xyz 2> err1.txt"       "err1.txt"
check_file_nonempty "ls bad path 2> file"     "ls /nonexistent_xyz 2> err2.txt"  "err2.txt"
check_absent        "stderr not on terminal"  "ls /nonexistent_xyz 2> err3.txt"  "No such file"

# --- Input + Output Together -------------------------------------------------
check_file    "cat < in > out"           "cat < input.txt > both.txt"            "both.txt"   "hello world"
check_file    "grep < in > out"          "grep line < input.txt > grep_out.txt"  "grep_out.txt"  "line one"

# --- Pipes -------------------------------------------------------------------
check         "single pipe"              "echo hello | cat"                         "hello"
check         "pipe to grep"             "cat input.txt | grep foo"                 "foo"
check         "pipe to wc"              "cat input.txt | wc -l"                    "6"
check         "two pipes"               "cat input.txt | grep line | wc -l"        "2"
check         "three pipes"             "cat input.txt | grep line | sort | cat"   "line one"
check         "four pipes"              "cat input.txt | cat | cat | cat | wc -l"  "6"

TOTAL=$((TOTAL+1))
actual=$(printf 'seq 1 100 | wc -l\nexit\n' | $SHELL_CMD 2>&1)
echo "$actual" | grep -q "100" && pass "large pipe: seq 100 | wc -l" || fail "large pipe" "100" "$actual"

# --- Composed Compounds -------------------------------------------
check         "cmd1 < in | cmd2"                   "cat < input.txt | grep hello"                    "hello world"
check_file    "cmd1 | cmd2 > out"                  "echo pipeout | cat > comp1.txt"                  "comp1.txt"       "pipeout"
check_file    "cmd < in > out"                     "cat < input.txt > comp2.txt"                     "comp2.txt"       "hello world"
check_file    "cmd1 < in | cmd2 > out"             "cat < input.txt | grep line > comp3.txt"         "comp3.txt"       "line one"
check_file    "cmd1 < in | cmd2 | cmd3 > out"      "cat < input.txt | grep line | sort > comp4.txt"  "comp4.txt"       "line"
check_file_nonempty "cmd 2> err"                   "invalidcmd_xyz 2> comp_err1.txt"                 "comp_err1.txt"
check_file_nonempty "cmd1 | cmd2 2> err"           "echo hi | invalidcmd_xyz 2> comp_err2.txt"       "comp_err2.txt"
check_file_nonempty "last cmd 2> err in pipe"      "echo hi | cat | ls /nonexistent_xyz 2> comp_err3.txt"  "comp_err3.txt"

# --- Error Handling -----------------------------------------------
check  "missing input file after <"       "cat <"                           "Input file not specified"
check  "missing output file after >"      "echo hello >"                    "Output file not specified"
check  "missing error file after 2>"      "ls 2>"                           "Error output file not specified"
check  "missing command after pipe"       "echo hello |"                    "Command missing after pipe"
check  "empty command between pipes"      "echo hello | | cat"              "Empty command between pipes"
check  "invalid command"                  "invalidcmd_xyz123"               "Command not found"
check  "invalid cmd in pipe sequence"     "echo hi | invalidcmd_xyz | cat"  "Command not found in pipe sequence"
check  "file not found"                   "cat < nonexistent_xyz.txt"       "File not found"

# --- Error Handling --------------------------------------------------
check  "pipe at start"                    "| cat"                           "Empty command between pipes"
check  "double pipe ||"                   "echo hi || cat"                  "Empty command between pipes"
check  "missing output after > in pipe"   "cat input.txt | echo hello >"   "Output file not specified"

TOTAL=$((TOTAL+1))
actual=$(printf '> nocrash.txt\nexit\n' | $SHELL_CMD 2>&1)
echo "$actual" | grep -qi "segfault\|killed" && fail "bare > no crash" "no crash" "$actual" || pass "bare > no crash"

# --- Shell Lifecycle ---------------------------------------------------------
TOTAL=$((TOTAL+1)); printf 'exit\n' | $SHELL_CMD >/dev/null 2>&1; [ $? -eq 0 ] && pass "exit returns 0" || fail "exit returns 0" "0" "$?"
TOTAL=$((TOTAL+1)); printf '' | $SHELL_CMD >/dev/null 2>&1;       [ $? -eq 0 ] && pass "EOF exits cleanly" || fail "EOF exits cleanly" "0" "$?"
check         "$ prompt shown"            "echo hi"                         "\$"
check_absent  "empty line no crash"       ""                                "crash"
check_absent  "spaces-only no crash"      "   "                             "crash"

TOTAL=$((TOTAL+1))
actual=$(printf 'invalidcmd_xyz\necho still_alive\nexit\n' | $SHELL_CMD 2>&1)
echo "$actual" | grep -q "still_alive" && pass "shell continues after error" || fail "shell continues after error" "still_alive" "$actual"

TOTAL=$((TOTAL+1))
actual=$(printf 'echo a\necho b\necho c\nexit\n' | $SHELL_CMD 2>&1)
echo "$actual" | grep -q "a" && echo "$actual" | grep -q "c" && pass "sequential commands" || fail "sequential commands" "a and c" "$actual"

# --- Local Executable --------------------------------------------------------
check         "./hello.sh"               "./hello.sh"               "hello from script"
check         "./hello.sh | cat"         "./hello.sh | cat"         "hello from script"
check         "spaces around pipe"       "echo hello  |  cat"       "hello"
check         "spaces around <"          "cat  <  input.txt"        "hello world"

TOTAL=$((TOTAL+1))
rm -f ws_out.txt; printf 'echo hello  >  ws_out.txt\nexit\n' | $SHELL_CMD >/dev/null 2>&1; sleep 0.1
[ -f "ws_out.txt" ] && grep -q "hello" ws_out.txt && pass "spaces around >" || fail "spaces around >" "hello in ws_out.txt" "$(cat ws_out.txt 2>/dev/null)"

# =============================================================================
cd .. && rm -rf test_tmp
echo -e "\n========================================"
echo -e " Results: ${GREEN}$PASS passed${NC} / ${RED}$FAIL failed${NC}"
echo -e "========================================\n"
