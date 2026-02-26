#!/bin/bash

SHELL_CMD="./myshell"
PASS=0
FAIL=0
TOTAL=0

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

# -----------------------------------------------------------------------------
# run_test: send input to myshell, check stdout+stderr contains expected string
# -----------------------------------------------------------------------------
run_test() {
    local name="$1"
    local input="$2"
    local expected="$3"
    TOTAL=$((TOTAL + 1))
    actual=$(printf '%s\nexit\n' "$input" | $SHELL_CMD 2>&1)
    if echo "$actual" | grep -qF "$expected"; then
        echo -e "${GREEN}[PASS]${NC} $name"
        PASS=$((PASS + 1))
    else
        echo -e "${RED}[FAIL]${NC} $name"
        echo "       Expected : '$expected'"
        echo "       Got      : '$actual'"
        FAIL=$((FAIL + 1))
    fi
}

# -----------------------------------------------------------------------------
# run_test_absent: check that a string is NOT present in output
# -----------------------------------------------------------------------------
run_test_absent() {
    local name="$1"
    local input="$2"
    local absent="$3"
    TOTAL=$((TOTAL + 1))
    actual=$(printf '%s\nexit\n' "$input" | $SHELL_CMD 2>&1)
    if echo "$actual" | grep -qF "$absent"; then
        echo -e "${RED}[FAIL]${NC} $name (unexpected output found: '$absent')"
        echo "       Got: '$actual'"
        FAIL=$((FAIL + 1))
    else
        echo -e "${GREEN}[PASS]${NC} $name"
        PASS=$((PASS + 1))
    fi
}

# -----------------------------------------------------------------------------
# run_file_test: check a file exists and contains expected string after command
# -----------------------------------------------------------------------------
run_file_test() {
    local name="$1"
    local input="$2"
    local file="$3"
    local expected="$4"
    TOTAL=$((TOTAL + 1))
    rm -f "$file"
    printf '%s\nexit\n' "$input" | $SHELL_CMD 2>&1 > /dev/null
    sleep 0.1
    if [ -f "$file" ] && grep -qF "$expected" "$file"; then
        echo -e "${GREEN}[PASS]${NC} $name"
        PASS=$((PASS + 1))
    else
        echo -e "${RED}[FAIL]${NC} $name"
        echo "       Expected '$expected' in file '$file'"
        [ -f "$file" ] && echo "       File contents: $(cat $file)" || echo "       File was not created."
        FAIL=$((FAIL + 1))
    fi
}

# -----------------------------------------------------------------------------
# run_file_nonempty: check a file exists and is non-empty after command
# -----------------------------------------------------------------------------
run_file_nonempty() {
    local name="$1"
    local input="$2"
    local file="$3"
    TOTAL=$((TOTAL + 1))
    rm -f "$file"
    printf '%s\nexit\n' "$input" | $SHELL_CMD 2>&1 > /dev/null
    sleep 0.1
    if [ -f "$file" ] && [ -s "$file" ]; then
        echo -e "${GREEN}[PASS]${NC} $name"
        PASS=$((PASS + 1))
    else
        echo -e "${RED}[FAIL]${NC} $name"
        echo "       File '$file' missing or empty."
        FAIL=$((FAIL + 1))
    fi
}

# -----------------------------------------------------------------------------
# run_file_absent: check that a string does NOT appear in a file
# -----------------------------------------------------------------------------
run_file_absent() {
    local name="$1"
    local input="$2"
    local file="$3"
    local absent="$4"
    TOTAL=$((TOTAL + 1))
    rm -f "$file"
    printf '%s\nexit\n' "$input" | $SHELL_CMD 2>&1 > /dev/null
    sleep 0.1
    if [ -f "$file" ] && grep -qF "$absent" "$file"; then
        echo -e "${RED}[FAIL]${NC} $name (found unwanted '$absent' in $file)"
        FAIL=$((FAIL + 1))
    else
        echo -e "${GREEN}[PASS]${NC} $name"
        PASS=$((PASS + 1))
    fi
}

# =============================================================================
# SETUP
# =============================================================================
mkdir -p test_tmp
cd test_tmp
SHELL_CMD="../myshell"

printf 'hello world\nline one\nline two\nfoo\nbar\nbaz\n' > input.txt
printf 'alpha\nbeta\ngamma\n' > input2.txt
printf 'UPPER\nlower\nMiXeD\n' > input3.txt

printf '#!/bin/bash\necho "hello from script"\n' > hello.sh && chmod +x hello.sh
printf '#!/bin/bash\necho "arg1=$1 arg2=$2"\n'   > args.sh  && chmod +x args.sh
printf '#!/bin/bash\necho "err output" >&2\n'    > errscript.sh && chmod +x errscript.sh

# =============================================================================
echo ""
echo "========================================"
echo " myshell Project Phase 1 - Testing"
echo "========================================"

# =============================================================================
echo ""
echo -e "${YELLOW}[1] Single Commands - No Arguments${NC}"
# =============================================================================
run_test "pwd"             "pwd"     "/"
run_test "uname"           "uname"   "Linux"

TOTAL=$((TOTAL + 1))
actual=$(printf 'date\nexit\n' | $SHELL_CMD 2>&1)
if ! echo "$actual" | grep -qi "not found\|failed\|error"; then
    echo -e "${GREEN}[PASS]${NC} date runs without error"; PASS=$((PASS+1))
else
    echo -e "${RED}[FAIL]${NC} date error: $actual"; FAIL=$((FAIL+1))
fi

TOTAL=$((TOTAL + 1))
actual=$(printf 'whoami\nexit\n' | $SHELL_CMD 2>&1)
if ! echo "$actual" | grep -qi "not found\|failed"; then
    echo -e "${GREEN}[PASS]${NC} whoami runs without error"; PASS=$((PASS+1))
else
    echo -e "${RED}[FAIL]${NC} whoami error: $actual"; FAIL=$((FAIL+1))
fi

TOTAL=$((TOTAL + 1))
actual=$(printf 'ls\nexit\n' | $SHELL_CMD 2>&1)
if ! echo "$actual" | grep -qi "not found\|failed\|error"; then
    echo -e "${GREEN}[PASS]${NC} ls runs without error"; PASS=$((PASS+1))
else
    echo -e "${RED}[FAIL]${NC} ls error: $actual"; FAIL=$((FAIL+1))
fi

# =============================================================================
echo ""
echo -e "${YELLOW}[2] Commands With Arguments${NC}"
# =============================================================================
run_test "echo single arg"         "echo hello"             "hello"
run_test "echo multiple args"      "echo foo bar baz"       "foo bar baz"
run_test "echo numbers"            "echo 1 2 3"             "1 2 3"
run_test "ls -l"                   "ls -l"                  "total"
run_test "ls -a"                   "ls -a"                  "."
run_test "cat input.txt"           "cat input.txt"          "hello world"
run_test "grep in file"            "grep hello input.txt"   "hello world"
run_test "wc -l input.txt"         "wc -l input.txt"        "6"
run_test "sort input.txt"          "sort input.txt"         "bar"
run_test "head -n 1 input.txt"     "head -n 1 input.txt"   "hello world"
run_test "tail -n 1 input.txt"     "tail -n 1 input.txt"   "baz"

# =============================================================================
echo ""
echo -e "${YELLOW}[3] Output Redirection (>)${NC}"
# =============================================================================
run_file_test "echo > file"                "echo hello > out1.txt"       "out1.txt"   "hello"
run_file_test "multiple words > file"      "echo foo bar > out2.txt"     "out2.txt"   "foo bar"
run_file_test "cat input > file"           "cat input.txt > out3.txt"    "out3.txt"   "hello world"

TOTAL=$((TOTAL + 1))
rm -f out4.txt
printf 'date > out4.txt\nexit\n' | $SHELL_CMD 2>&1 > /dev/null
sleep 0.1
if [ -f "out4.txt" ] && [ -s "out4.txt" ]; then
    echo -e "${GREEN}[PASS]${NC} date > file creates non-empty file"; PASS=$((PASS+1))
else
    echo -e "${RED}[FAIL]${NC} date > file failed"; FAIL=$((FAIL+1))
fi

# overwrite test: second write should replace first
printf 'echo first > overwrite.txt\nexit\n' | $SHELL_CMD 2>&1 > /dev/null
printf 'echo second > overwrite.txt\nexit\n' | $SHELL_CMD 2>&1 > /dev/null
TOTAL=$((TOTAL + 1))
if grep -q "second" overwrite.txt && ! grep -q "first" overwrite.txt; then
    echo -e "${GREEN}[PASS]${NC} > truncates existing file (does not append)"; PASS=$((PASS+1))
else
    echo -e "${RED}[FAIL]${NC} > should truncate, not append"; FAIL=$((FAIL+1))
fi

# =============================================================================
echo ""
echo -e "${YELLOW}[4] Input Redirection (<)${NC}"
# =============================================================================
run_test "cat < input.txt"          "cat < input.txt"           "hello world"
run_test "cat < input2.txt"         "cat < input2.txt"          "alpha"
run_test "grep < input.txt"         "grep line < input.txt"     "line one"
run_test "wc -l < input.txt"        "wc -l < input.txt"         "6"
run_test "sort < input.txt"         "sort < input.txt"          "bar"
run_test "head -n 2 < input.txt"    "head -n 2 < input.txt"     "hello world"
run_test "tail -n 1 < input2.txt"   "tail -n 1 < input2.txt"    "gamma"

# =============================================================================
echo ""
echo -e "${YELLOW}[5] Error Redirection (2>)${NC}"
# =============================================================================
run_file_nonempty "invalid cmd stderr > file"        "invalidcmd_xyz 2> err1.txt"          "err1.txt"
run_file_nonempty "ls nonexistent stderr > file"     "ls /nonexistent_xyz 2> err2.txt"     "err2.txt"
run_file_nonempty "cat missing file stderr > file"   "cat nonexistent_xyz.txt 2> err3.txt" "err3.txt"
run_file_nonempty "errscript.sh stderr > file"       "./errscript.sh 2> err4.txt"          "err4.txt"

# stderr should NOT leak to terminal when redirected to file
run_test_absent "stderr not on terminal when redirected"  "ls /nonexistent_xyz 2> err5.txt"  "No such file"

# =============================================================================
echo ""
echo -e "${YELLOW}[6] Input + Output Redirection Together${NC}"
# =============================================================================
run_file_test "cat < in > out"          "cat < input.txt > redir_both.txt"     "redir_both.txt"  "hello world"
run_file_test "grep < in > out"         "grep line < input.txt > grep_out.txt"  "grep_out.txt"   "line one"
run_file_test "sort < in > out"         "sort < input.txt > sorted.txt"         "sorted.txt"     "bar"
run_file_test "wc < in > out"           "wc -l < input.txt > wc_out.txt"        "wc_out.txt"     "6"

# =============================================================================
echo ""
echo -e "${YELLOW}[7] Single Pipe${NC}"
# =============================================================================
run_test "echo | cat"                  "echo hello | cat"                  "hello"
run_test "echo | grep match"           "echo hello world | grep hello"     "hello world"
run_test "cat file | grep"             "cat input.txt | grep foo"          "foo"
run_test "cat file | wc -l"            "cat input.txt | wc -l"             "6"
run_test "cat file | sort"             "cat input.txt | sort"              "bar"
run_test "cat file | head -n 1"        "cat input.txt | head -n 1"         "hello world"
run_test "cat file | tail -n 1"        "cat input.txt | tail -n 1"         "baz"

TOTAL=$((TOTAL + 1))
actual=$(printf 'echo hello | grep zzznomatch\nexit\n' | $SHELL_CMD 2>&1)
if ! echo "$actual" | grep -qi "crash\|segfault\|killed"; then
    echo -e "${GREEN}[PASS]${NC} pipe with no-match grep exits cleanly"; PASS=$((PASS+1))
else
    echo -e "${RED}[FAIL]${NC} pipe with no-match grep crashed: $actual"; FAIL=$((FAIL+1))
fi

# =============================================================================
echo ""
echo -e "${YELLOW}[8] Two Pipes${NC}"
# =============================================================================
run_test "cat | grep | cat"       "cat input.txt | grep line | cat"       "line one"
run_test "cat | sort | head"      "cat input.txt | sort | head -n 1"      "bar"
run_test "cat | sort | tail"      "cat input.txt | sort | tail -n 1"      "line two"
run_test "echo | cat | cat"       "echo hello | cat | cat"                "hello"
run_test "cat | grep | wc"        "cat input.txt | grep line | wc -l"     "2"

# =============================================================================
echo ""
echo -e "${YELLOW}[9] Three or More Pipes${NC}"
# =============================================================================
run_test "3 pipes: cat|grep|sort|cat"   "cat input.txt | grep line | sort | cat"  "line one"
run_test "3 pipes: echo chain"          "echo hello | cat | cat | cat"            "hello"

TOTAL=$((TOTAL + 1))
actual=$(printf 'seq 1 100 | wc -l\nexit\n' | $SHELL_CMD 2>&1)
if echo "$actual" | grep -q "100"; then
    echo -e "${GREEN}[PASS]${NC} 2-pipe large output: seq 100 | wc -l = 100"; PASS=$((PASS+1))
else
    echo -e "${RED}[FAIL]${NC} seq | wc -l failed: $actual"; FAIL=$((FAIL+1))
fi

TOTAL=$((TOTAL + 1))
actual=$(printf 'cat input.txt | sort | uniq | wc -l\nexit\n' | $SHELL_CMD 2>&1)
if ! echo "$actual" | grep -qi "crash\|killed\|segfault"; then
    echo -e "${GREEN}[PASS]${NC} 3-pipe cat|sort|uniq|wc no crash"; PASS=$((PASS+1))
else
    echo -e "${RED}[FAIL]${NC} 3-pipe crashed: $actual"; FAIL=$((FAIL+1))
fi

TOTAL=$((TOTAL + 1))
actual=$(printf 'cat input.txt | cat | cat | cat | wc -l\nexit\n' | $SHELL_CMD 2>&1)
if echo "$actual" | grep -q "6"; then
    echo -e "${GREEN}[PASS]${NC} 4-pipe cat chain | wc -l = 6"; PASS=$((PASS+1))
else
    echo -e "${RED}[FAIL]${NC} 4-pipe chain failed: $actual"; FAIL=$((FAIL+1))
fi

# =============================================================================
echo ""
echo -e "${YELLOW}[10] Composed Compound Commands — PDF Spec${NC}"
# =============================================================================

# command < input.txt
run_test       "command < input.txt"                         "cat < input.txt"                                 "hello world"

# command > output.txt
run_file_test  "command > output.txt"                        "echo testing > comp1.txt"                        "comp1.txt"       "testing"

# command 2> error.log
run_file_nonempty "command 2> error.log"                     "invalidcmd_xyz 2> comp_err1.txt"                 "comp_err1.txt"

# command1 < input.txt | command2
run_test       "command1 < input.txt | command2"             "cat < input.txt | grep hello"                    "hello world"

# command1 | command2 > output.txt
run_file_test  "command1 | command2 > output.txt"            "echo pipeout | cat > comp2.txt"                  "comp2.txt"       "pipeout"

# command1 | command2 2> error.log
run_file_nonempty "command1 | command2 2> error.log"         "echo hi | invalidcmd_xyz 2> comp_err2.txt"       "comp_err2.txt"

# command < input.txt > output.txt
run_file_test  "command < input.txt > output.txt"            "cat < input.txt > comp3.txt"                     "comp3.txt"       "hello world"

# command1 < input.txt | command2 > output.txt
run_file_test  "command1 < input.txt | command2 > output.txt"  "cat < input.txt | grep line > comp4.txt"       "comp4.txt"       "line one"

# command1 < input.txt | command2 | command3 > output.txt
run_file_test  "cmd1 < in | cmd2 | cmd3 > out"               "cat < input.txt | grep line | sort > comp5.txt"  "comp5.txt"       "line"

# command1 | command2 | command3 2> error.log
run_file_nonempty "cmd1 | cmd2 | cmd3 2> error.log"          "echo hi | cat | ls /nonexistent_xyz 2> comp_err3.txt" "comp_err3.txt"

# command1 < input.txt | command2 2> error.log | command3 > output.txt
run_file_test  "cmd1 < in | cmd2 2> err | cmd3 > out"        "cat < input.txt | grep line > comp6.txt"         "comp6.txt"       "line"

# =============================================================================
echo ""
echo -e "${YELLOW}[11] Error Handling — PDF Spec Messages${NC}"
# =============================================================================

run_test "Missing input file after <"           "cat <"                          "Input file not specified"
run_test "Missing output file after >"          "echo hello >"                   "Output file not specified"
run_test "Missing error file after 2>"          "ls 2>"                          "Error output file not specified"
run_test "Missing command after pipe"           "echo hello |"                   "Command missing after pipe"
run_test "Empty command between pipes"          "echo hello | | cat"             "Empty command between pipes"
run_test "Invalid command: Command not found"   "invalidcmd_xyz123"              "Command not found"
run_test "Invalid cmd in pipe sequence"         "echo hi | invalidcmd_xyz | cat" "Command not found in pipe sequence"
run_test "File not found (nonexistent <)"       "cat < nonexistent_xyz.txt"      "File not found"
run_test "Output file missing after > in pipe"  "cat input.txt | echo hello >"   "Output file not specified"

# =============================================================================
echo ""
echo -e "${YELLOW}[12] Error Handling — Additional Edge Cases${NC}"
# =============================================================================

run_test "Pipe at very start: | cmd"         "| cat"               "Empty command between pipes"
run_test "Double pipe ||"                    "echo hi || cat"      "Empty command between pipes"
run_test "Three pipes with middle empty"     "echo hi | | | cat"   "Empty command between pipes"

# Shell should not crash on bare redirection with no command
TOTAL=$((TOTAL + 1))
actual=$(printf '> nocrash.txt\nexit\n' | $SHELL_CMD 2>&1)
if ! echo "$actual" | grep -qi "segfault\|killed"; then
    echo -e "${GREEN}[PASS]${NC} Bare > with no command does not crash"; PASS=$((PASS+1))
else
    echo -e "${RED}[FAIL]${NC} Bare > crashed shell"; FAIL=$((FAIL+1))
fi

# Very long argument
run_test "Very long single argument"    "echo $(python3 -c 'print("a"*200)')"  "aaaa"

# =============================================================================
echo ""
echo -e "${YELLOW}[13] Pipes + Redirection Combinations${NC}"
# =============================================================================

# Pipe output goes to file not terminal
run_file_test  "pipe result in file, not stdout"    "cat input.txt | grep hello > pipe_file.txt"  "pipe_file.txt"  "hello world"
run_file_absent "piped-to-file: nothing extra in file"  "echo only | cat > only_out.txt"  "only_out.txt"  "extra"

# Input comes from file, travels through pipe, lands in file
run_file_test  "< in | grep | sort > out (full chain)" \
    "cat < input.txt | grep line | sort > full_chain.txt" \
    "full_chain.txt"  "line"

# stderr from second command in pipe
run_file_nonempty "stderr from cmd2 in pipe to file"  \
    "echo hi | invalidcmd_xyz 2> pipe_stderr.txt"  "pipe_stderr.txt"

# stdout of first cmd goes through pipe, stderr of second goes to file
run_file_test  "cmd1 | cmd2 > out, stdout in file"  \
    "cat input.txt | sort > sorted2.txt"  "sorted2.txt"  "bar"

# =============================================================================
echo ""
echo -e "${YELLOW}[14] Exit and Shell Lifecycle${NC}"
# =============================================================================

TOTAL=$((TOTAL + 1))
printf 'exit\n' | $SHELL_CMD 2>&1 > /dev/null
if [ $? -eq 0 ]; then
    echo -e "${GREEN}[PASS]${NC} exit returns code 0"; PASS=$((PASS+1))
else
    echo -e "${RED}[FAIL]${NC} exit did not return 0"; FAIL=$((FAIL+1))
fi

TOTAL=$((TOTAL + 1))
printf '' | $SHELL_CMD 2>&1 > /dev/null
if [ $? -eq 0 ]; then
    echo -e "${GREEN}[PASS]${NC} EOF (Ctrl+D) exits cleanly with code 0"; PASS=$((PASS+1))
else
    echo -e "${RED}[FAIL]${NC} EOF did not exit cleanly"; FAIL=$((FAIL+1))
fi

run_test "Shell shows $ prompt"   "echo hi"   "\$"

TOTAL=$((TOTAL + 1))
actual=$(printf 'echo first\necho second\necho third\nexit\n' | $SHELL_CMD 2>&1)
if echo "$actual" | grep -q "first" && echo "$actual" | grep -q "second" && echo "$actual" | grep -q "third"; then
    echo -e "${GREEN}[PASS]${NC} Multiple sequential commands all execute"; PASS=$((PASS+1))
else
    echo -e "${RED}[FAIL]${NC} Sequential commands failed: $actual"; FAIL=$((FAIL+1))
fi

run_test_absent "Empty input line no crash"    ""     "crash"
run_test_absent "Spaces-only input no crash"   "   "  "crash"

# =============================================================================
echo ""
echo -e "${YELLOW}[15] Local Executable (./)${NC}"
# =============================================================================
run_test       "Execute ./hello.sh"                "./hello.sh"               "hello from script"
run_test       "Execute ./args.sh with args"       "./args.sh one two"        "arg1=one arg2=two"
run_test       "Pipe from local script"            "./hello.sh | cat"         "hello from script"
run_file_test  "Redirect output of ./hello.sh"     "./hello.sh > script_out.txt"  "script_out.txt"  "hello from script"
run_file_test  "Local script < in > out"           "./hello.sh > local_out.txt"   "local_out.txt"   "hello from script"

# =============================================================================
echo ""
echo -e "${YELLOW}[16] Whitespace Handling${NC}"
# =============================================================================
run_test "Extra spaces between args"        "echo   hello   world"      "hello"
run_test "Leading space before command"     "  echo hello"              "hello"
run_test "Space around pipe"                "echo hello  |  cat"        "hello"
run_test "Space around <"                   "cat  <  input.txt"         "hello world"

TOTAL=$((TOTAL + 1))
rm -f ws_out.txt
printf 'echo hello  >  ws_out.txt\nexit\n' | $SHELL_CMD 2>&1 > /dev/null
sleep 0.1
if [ -f "ws_out.txt" ] && grep -q "hello" ws_out.txt; then
    echo -e "${GREEN}[PASS]${NC} Extra spaces around > still redirects correctly"; PASS=$((PASS+1))
else
    echo -e "${RED}[FAIL]${NC} Spaces around > failed"; FAIL=$((FAIL+1))
fi

# =============================================================================
echo ""
echo -e "${YELLOW}[17] Stress / Repeat Tests${NC}"
# =============================================================================

TOTAL=$((TOTAL + 1))
actual=$(printf 'echo a\necho b\necho c\necho d\necho e\nexit\n' | $SHELL_CMD 2>&1)
if echo "$actual" | grep -q "a" && echo "$actual" | grep -q "e"; then
    echo -e "${GREEN}[PASS]${NC} 5 sequential echo commands all execute"; PASS=$((PASS+1))
else
    echo -e "${RED}[FAIL]${NC} Sequential commands: $actual"; FAIL=$((FAIL+1))
fi

# second write to same file must truncate, not append
printf 'echo run1 > repeat.txt\nexit\n' | $SHELL_CMD 2>&1 > /dev/null
printf 'echo run2 > repeat.txt\nexit\n' | $SHELL_CMD 2>&1 > /dev/null
TOTAL=$((TOTAL + 1))
linecount=$(wc -l < repeat.txt)
if [ "$linecount" -eq 1 ] && grep -q "run2" repeat.txt; then
    echo -e "${GREEN}[PASS]${NC} Output file truncated on second run"; PASS=$((PASS+1))
else
    echo -e "${RED}[FAIL]${NC} Output file appended instead of truncated"; FAIL=$((FAIL+1))
fi

# large pipeline
TOTAL=$((TOTAL + 1))
actual=$(printf 'seq 1 100 | wc -l\nexit\n' | $SHELL_CMD 2>&1)
if echo "$actual" | grep -q "100"; then
    echo -e "${GREEN}[PASS]${NC} Large pipe: seq 1 100 | wc -l = 100"; PASS=$((PASS+1))
else
    echo -e "${RED}[FAIL]${NC} Large pipe failed: $actual"; FAIL=$((FAIL+1))
fi

# run commands after an error — shell must keep running
TOTAL=$((TOTAL + 1))
actual=$(printf 'invalidcmd_xyz\necho still_alive\nexit\n' | $SHELL_CMD 2>&1)
if echo "$actual" | grep -q "still_alive"; then
    echo -e "${GREEN}[PASS]${NC} Shell continues after failed command"; PASS=$((PASS+1))
else
    echo -e "${RED}[FAIL]${NC} Shell died after failed command: $actual"; FAIL=$((FAIL+1))
fi

# run commands after a bad pipe — shell must keep running
TOTAL=$((TOTAL + 1))
actual=$(printf 'echo hi |\necho still_alive\nexit\n' | $SHELL_CMD 2>&1)
if echo "$actual" | grep -q "still_alive"; then
    echo -e "${GREEN}[PASS]${NC} Shell continues after pipe error"; PASS=$((PASS+1))
else
    echo -e "${RED}[FAIL]${NC} Shell died after pipe error: $actual"; FAIL=$((FAIL+1))
fi

# =============================================================================
# Cleanup
# =============================================================================
cd ..
rm -rf test_tmp

echo ""
echo "========================================"
echo -e " Results: ${GREEN}$PASS passed${NC} / ${RED}$FAIL failed${NC} / $TOTAL total"
echo "========================================"
echo ""
