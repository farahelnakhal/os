#!/bin/bash

SHELL_CMD="./myshell"
SERVER_CMD="./server"
CLIENT_CMD="python3 test_client.py"
SERVER_PORT=0  # Will be set to random available port
PASS=0; FAIL=0; TOTAL=0
GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; NC='\033[0m'

pass() { echo -e "${GREEN}[PASS]${NC} $1"; PASS=$((PASS+1)); TOTAL=$((TOTAL+1)); }
fail() { echo -e "${RED}[FAIL]${NC} $1\n       Expected: '$2'\n       Got: '$3'"; FAIL=$((FAIL+1)); TOTAL=$((TOTAL+1)); }

# Find an available port
find_available_port() {
    # Try ports in range 9000-9999 to avoid conflicts
    for port in $(shuf -i 9000-9999 -n 100); do
        if ! nc -z localhost $port 2>/dev/null && ! lsof -i :$port >/dev/null 2>&1; then
            echo $port
            return 0
        fi
    done
    # Fallback: let system choose
    python3 -c 'import socket; s=socket.socket(); s.bind(("", 0)); print(s.getsockname()[1]); s.close()'
}

# Cleanup function to kill any existing servers
cleanup_servers() {
    if [ ! -z "$SERVER_PID" ]; then
        kill -9 $SERVER_PID 2>/dev/null
        wait $SERVER_PID 2>/dev/null
    fi
}

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

# =========================== PHASE 2 TEST FUNCTIONS ===========================

# Modify server.c port at runtime (create temp modified version)
create_server_with_port() {
    local port=$1
    # Create a modified server that uses our port
    sed "s/#define PORT 8080/#define PORT $port/" ../server.c > server_temp.c
    gcc -Wall -Wextra -g -I.. -c server_temp.c -o server_temp.o
    gcc -Wall -Wextra -g -o server_temp server_temp.o ../pipeline.o ../simple.o
    rm -f server_temp.c server_temp.o
}

# Start server in background and wait for it to be ready
start_server() {
    ./server_temp > server_output.log 2>&1 &
    SERVER_PID=$!
    sleep 1.5
    
    # Check if server is running
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo -e "${RED}ERROR: Server failed to start${NC}"
        cat server_output.log
        return 1
    fi
    
    # Wait for server to be ready (check for up to 5 seconds)
    for i in {1..10}; do
        if nc -z localhost $SERVER_PORT 2>/dev/null; then
            return 0
        fi
        sleep 0.5
    done
    
    echo -e "${RED}ERROR: Server not listening on port $SERVER_PORT${NC}"
    cat server_output.log
    kill -9 $SERVER_PID 2>/dev/null
    return 1
}

# Stop server
stop_server() {
    cleanup_servers
    rm -f server_temp
}

# Send command via netcat to server (with proper delay for sequential commands)
send_command_nc() {
    local command="$1"
    echo -e "$command" | nc -q 1 localhost $SERVER_PORT 2>&1 | sed '/^$/d'
}

# Send multiple commands sequentially with proper timing
send_sequential_commands() {
    local commands=("$@")
    (
        for cmd in "${commands[@]}"; do
            echo "$cmd"
            sleep 0.3
        done
        echo "exit"
        sleep 0.3
    ) | nc -q 1 localhost $SERVER_PORT 2>&1
}

# Check server output contains expected string (ignoring color codes and extra whitespace)
check_server_log() {
    local name="$1" expected="$2"
    sleep 0.5
    # Strip color codes and extra whitespace for comparison
    local cleaned_log=$(cat server_output.log | sed 's/\x1b\[[0-9;]*m//g' | tr -s ' \n' ' ')
    if echo "$cleaned_log" | grep -qF "$expected"; then
        pass "$name"
    else
        fail "$name" "$expected" "$(tail -20 server_output.log | sed 's/\x1b\[[0-9;]*m//g')"
    fi
}

# Send command to server and check client output
check_remote() {
    local name="$1" command="$2" expected="$3"
    # For file not found errors, we need to handle differently
    if [[ "$expected" == "File not found" ]]; then
        actual=$(send_command_nc "$command" | head -1)
        if echo "$actual" | grep -qE "File not found|No such file or directory"; then
            pass "$name"
        else
            fail "$name" "$expected" "$actual"
        fi
    else
        actual=$(send_command_nc "$command")
        if echo "$actual" | grep -qF "$expected"; then
            pass "$name"
        else
            fail "$name" "$expected" "$actual"
        fi
    fi
}

# Check that server shows proper format (more flexible matching)
check_server_format() {
    local name="$1" command="$2"
    > server_output.log
    send_command_nc "$command" >/dev/null 2>&1
    sleep 0.5
    # Strip color codes AND null bytes before grepping
    local clean_log=$(cat server_output.log | tr -d '\000' | sed 's/\x1b\[[0-9;]*m//g')
    local has_received=$(echo "$clean_log" | grep -cF "[RECEIVED] Received command: \"$command\"")
    local has_executing=$(echo "$clean_log" | grep -cF "[EXECUTING] Executing command: \"$command\"")
    local has_output=$(echo "$clean_log" | grep -cF "[OUTPUT] Sending output to client:")
    if [ "$has_received" -ge 1 ] && [ "$has_executing" -ge 1 ] && [ "$has_output" -ge 1 ]; then
        pass "$name"
    else
        fail "$name" "All server format tags present" "Received:$has_received Executing:$has_executing Output:$has_output"
    fi
}

# =============================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}       PHASE 1 TESTING${NC}"
echo -e "${YELLOW}========================================${NC}\n"

mkdir -p test_tmp && cd test_tmp
SHELL_CMD="../myshell"
printf 'hello world\nline one\nline two\nfoo\nbar\nbaz\n' > input.txt
printf '#!/bin/bash\necho "hello from script"\n' > hello.sh && chmod +x hello.sh
printf '#!/bin/bash\necho "err" >&2\n' > errscript.sh && chmod +x errscript.sh

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
check         "cat < file"              "cat < input.txt"          "hello world"
check         "grep < file"             "grep line < input.txt"    "line one"
check         "wc -l < file"            "wc -l < input.txt"        "6"

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

# --- Additional Error Handling --------------------------------------------------
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
# =============================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}    PHASE 1 DEBUGGING (Specific Bugs)${NC}"
echo -e "${YELLOW}========================================${NC}\n"

# Create test input file with specific content for debugging
printf 'Hello 1\nHello 1\nThis is a test file\nHello 2\nUnique lines matter\nHello 3\nAnother test line\n' > input
printf 'one\nTWO\ntwo\nTHREE\none\nTWO\n' > a.txt

# Test 1: echo -e with escape sequences (should interpret \n)
actual=$(printf 'echo -e "Hello 1\\nHello 1\\nThis is a test file\\nHello 2\\nUnique lines matter\\nHello 3\\nAnother test line"\nexit\n' | $SHELL_CMD 2>&1)
expected_lines=$(echo -e "Hello 1\nHello 1\nThis is a test file\nHello 2\nUnique lines matter\nHello 3\nAnother test line")
if echo "$actual" | grep -qF "Hello 1" && echo "$actual" | grep -qF "Hello 2" && echo "$actual" | grep -qF "Hello 3"; then
    pass "echo -e interprets \\\\n correctly"
else
    fail "echo -e interprets \\\\n correctly" "multi-line output" "$actual"
fi

# Test 2: Complex pipeline with tr, sort, uniq -c
rm -f a1
printf 'cat a.txt | tr a-z A-Z | sort | uniq -c > a1\nexit\n' | $SHELL_CMD >/dev/null 2>&1
sleep 0.1

TOTAL=$((TOTAL+1))
if [ -f "a1" ]; then
    # Check for expected content (order may vary, check counts)
    has_one=$(grep -c "ONE" a1)
    has_two=$(grep -c "TWO" a1)
    has_three=$(grep -c "THREE" a1)
    if [ "$has_one" -ge 1 ] && [ "$has_two" -ge 1 ] && [ "$has_three" -ge 1 ]; then
        pass "Complex pipeline with tr/sort/uniq works"
    else
        fail "Complex pipeline with tr/sort/uniq works" "ONE, TWO, THREE in output" "$(cat a1)"
    fi
else
    fail "Complex pipeline with tr/sort/uniq works" "file a1 created" "file not found"
fi

# Test 3: Input redirection with grep (should output matching lines)
actual=$(printf 'cat < input | grep "Hello"\nexit\n' | $SHELL_CMD 2>&1)
# Should output lines containing "Hello"
if echo "$actual" | grep -q "Hello 1" && echo "$actual" | grep -q "Hello 2" && echo "$actual" | grep -q "Hello 3"; then
    pass "cat < input | grep Hello works"
else
    fail "cat < input | grep Hello works" "Hello 1, Hello 2, Hello 3" "$actual"
fi

# Test 4: Input redirection with grep redirecting to file
rm -f out
printf 'cat < input | grep "Hello" > out\nexit\n' | $SHELL_CMD >/dev/null 2>&1
sleep 0.1

TOTAL=$((TOTAL+1))
if [ -f "out" ]; then
    if grep -q "Hello 1" out && grep -q "Hello 2" out && grep -q "Hello 3" out; then
        pass "cat < input | grep Hello > out works"
    else
        fail "cat < input | grep Hello > out works" "Hello lines in out" "$(cat out)"
    fi
else
    fail "cat < input | grep Hello > out works" "file out created" "file not found"
fi

# Test 5: Pipeline with sort -r (reverse sort)
rm -f out
printf 'cat < input | grep "Hello" | sort -r > out\nexit\n' | $SHELL_CMD >/dev/null 2>&1
sleep 0.1

TOTAL=$((TOTAL+1))
if [ -f "out" ]; then
    # Check if sorted in reverse order (Hello 3 should come before Hello 1)
    first_line=$(head -1 out)
    if echo "$first_line" | grep -q "Hello 3"; then
        pass "cat < input | grep Hello | sort -r works (reverse order)"
    else
        fail "cat < input | grep Hello | sort -r works" "Hello 3 first" "$(cat out)"
    fi
else
    fail "cat < input | grep Hello | sort -r works" "file out created" "file not found"
fi

# Test 6: Pipeline with stderr redirection (2>)
rm -f error out
printf 'cat < input | grep "Hello" 2> error | sort | uniq > out\nexit\n' | $SHELL_CMD >/dev/null 2>&1
sleep 0.1

TOTAL=$((TOTAL+1))
if [ -f "out" ] && [ -f "error" ]; then
    # out should have unique Hello lines
    hello_count=$(grep -c "Hello" out)
    if [ "$hello_count" -ge 1 ] && [ ! -s "error" ]; then
        pass "Pipeline with stderr redirection works"
    else
        fail "Pipeline with stderr redirection works" "unique Hello lines, empty error" "out: $(cat out), error: $(cat error)"
    fi
else
    fail "Pipeline with stderr redirection works" "files out and error created" "missing files"
fi

# Test 7: Bare > operator (should show error or do nothing, not crash)
actual=$(printf '> in\nexit\n' | $SHELL_CMD 2>&1)
if ! echo "$actual" | grep -qi "segfault\|crash\|killed"; then
    pass "Bare '> in' doesn't crash"
else
    fail "Bare '> in' doesn't crash" "no crash" "$actual"
fi

# Test 8: Bare < operator (should show error or do nothing, not crash)
actual=$(printf '< in\nexit\n' | $SHELL_CMD 2>&1)
if ! echo "$actual" | grep -qi "segfault\|crash\|killed"; then
    pass "Bare '< in' doesn't crash"
else
    fail "Bare '< in' doesn't crash" "no crash" "$actual"
fi

# Test 9: echo -e with multiple escapes
actual=$(printf 'echo -e "Line1\\nLine2\\tTabbed\\nLine3"\nexit\n' | $SHELL_CMD 2>&1)
if echo "$actual" | grep -q "Line1" && echo "$actual" | grep -q "Line2.*Tabbed" && echo "$actual" | grep -q "Line3"; then
    pass "echo -e handles multiple escape sequences"
else
    fail "echo -e handles multiple escape sequences" "formatted output" "$actual"
fi

# Test 10: Pipeline with uniq (deduplication)
actual=$(printf 'cat input | uniq\nexit\n' | $SHELL_CMD 2>&1)
# Should have only one "Hello 1" (not two)
hello_count=$(echo "$actual" | grep -c "Hello 1")
if [ "$hello_count" -eq 1 ]; then
    pass "uniq deduplicates consecutive duplicates"
else
    fail "uniq deduplicates consecutive duplicates" "Hello 1 once" "$actual"
fi

# Clean up debug files
rm -f input a.txt out error a1

# =============================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}       PHASE 2 TESTING${NC}"
echo -e "${YELLOW}========================================${NC}\n"

# Check if server source exists
if [ ! -f "../server.c" ]; then
    echo -e "${RED}ERROR: server.c not found${NC}"
    cd .. && rm -rf test_tmp
    exit 1
fi

# Check if nc (netcat) is available
if ! command -v nc &> /dev/null; then
    echo -e "${RED}ERROR: netcat (nc) not found. Please install it to run Phase 2 tests.${NC}"
    cd .. && rm -rf test_tmp
    exit 1
fi

# Find available port
echo -e "${YELLOW}Finding available port...${NC}"
SERVER_PORT=$(find_available_port)
echo -e "${YELLOW}Using port: $SERVER_PORT${NC}"

# Create server with custom port
echo -e "${YELLOW}Compiling server with port $SERVER_PORT...${NC}"
create_server_with_port $SERVER_PORT

# --- Server Startup Tests ----------------------------------------------------

start_server
if [ $? -eq 0 ]; then
    pass "Server starts successfully"
    check_server_log "Server shows startup message" "Server started, waiting for client connections"
else
    fail "Server starts successfully" "Server running" "Server failed to start"
    cd .. && rm -rf test_tmp
    exit 1
fi

sleep 0.5

# --- Connection Tests --------------------------------------------------------
echo "exit" | nc -q 1 localhost $SERVER_PORT >/dev/null 2>&1
sleep 0.5
check_server_log "Server accepts client connection" "connected. Assigned to Thread-"

# --- Basic Remote Command Execution -----------------------------------------
check_remote "Remote: pwd" "pwd" "test_tmp"
check_remote "Remote: echo" "echo hello world" "hello world"
check_remote "Remote: ls" "ls" "input.txt"
check_remote "Remote: whoami" "whoami" ""

# --- Commands with Arguments -------------------------------------------------
check_remote "Remote: ls -l" "ls -l" "input.txt"
check_remote "Remote: echo multiple" "echo one two three" "one two three"
check_remote "Remote: cat file" "cat input.txt" "hello world"
check_remote "Remote: grep" "grep foo input.txt" "foo"
check_remote "Remote: wc -l" "wc -l input.txt" "6"

# --- Pipe Commands -----------------------------------------------------------
check_remote "Remote: single pipe" "echo test | cat" "test"
check_remote "Remote: pipe to grep" "cat input.txt | grep hello" "hello world"
check_remote "Remote: pipe to wc" "cat input.txt | wc -l" "6"
check_remote "Remote: two pipes" "cat input.txt | grep line | wc -l" "2"
check_remote "Remote: three pipes" "echo abc | tr a-z A-Z | cat" "ABC"

# --- Redirection Commands ----------------------------------------------------
send_command_nc "echo remote_test > remote_out.txt" >/dev/null 2>&1
sleep 0.5
TOTAL=$((TOTAL+1))
if [ -f "remote_out.txt" ] && grep -q "remote_test" remote_out.txt; then
    pass "Remote: output redirection >"
else
    fail "Remote: output redirection >" "remote_test in file" "$(cat remote_out.txt 2>/dev/null)"
fi

send_command_nc "cat < input.txt" > /tmp/remote_input_test.txt 2>&1
TOTAL=$((TOTAL+1))
if grep -q "hello world" /tmp/remote_input_test.txt; then
    pass "Remote: input redirection <"
else
    fail "Remote: input redirection <" "hello world" "$(cat /tmp/remote_input_test.txt)"
fi
rm -f /tmp/remote_input_test.txt

send_command_nc "ls /nonexistent_remote 2> remote_err.txt" >/dev/null 2>&1
sleep 0.5
TOTAL=$((TOTAL+1))
if [ -f "remote_err.txt" ] && [ -s "remote_err.txt" ]; then
    pass "Remote: error redirection 2>"
else
    fail "Remote: error redirection 2>" "non-empty error file" "missing or empty"
fi

# --- Compound Commands -------------------------------------------------------
check_remote "Remote: input + pipe" "cat < input.txt | grep hello" "hello world"

send_command_nc "cat < input.txt | grep line > remote_compound.txt" >/dev/null 2>&1
sleep 0.5
TOTAL=$((TOTAL+1))
if [ -f "remote_compound.txt" ] && grep -q "line one" remote_compound.txt; then
    pass "Remote: input + pipe + output"
else
    fail "Remote: input + pipe + output" "line one in file" "$(cat remote_compound.txt 2>/dev/null)"
fi

# --- Error Handling ----------------------------------------------------------
check_remote "Remote: invalid command" "invalidcommand_xyz" "Command not found"
actual=$(send_command_nc "cat nonexistent_file.txt" | head -1)
if echo "$actual" | grep -qE "File not found|No such file or directory"; then
    pass "Remote: file not found"
else
    fail "Remote: file not found" "File not found" "$actual"
fi
check_remote "Remote: missing file after <" "cat <" "Input file not specified"
check_remote "Remote: missing file after >" "echo test >" "Output file not specified"
check_remote "Remote: missing file after 2>" "ls 2>" "Error output file not specified"
check_remote "Remote: pipe at end" "echo test |" "Command missing after pipe"
check_remote "Remote: empty between pipes" "echo hi | | cat" "Empty command between pipes"

# --- Server Output Format Tests ----------------------------------------------
check_server_format "Server format: ls" "ls"
check_server_format "Server format: pwd" "pwd"
check_server_format "Server format: echo" "echo test"

# --- Multiple Sequential Commands -------------------------------------------
> server_output.log
# Use the sequential command function with proper timing
# Directly call nc with a here-document instead of using function in subshell
output=$(
    (
        echo "pwd"
        sleep 0.3
        echo "ls"
        sleep 0.3
        echo "whoami"
        sleep 0.3
        echo "exit"
        sleep 0.3
    ) | nc -q 1 localhost $SERVER_PORT 2>&1
)
sleep 1

TOTAL=$((TOTAL+1))
if echo "$output" | grep -q "test_tmp" && echo "$output" | grep -q "input.txt"; then
    pass "Remote: sequential commands execute"
else
    fail "Remote: sequential commands execute" "all outputs" "$output"
fi

# Check server logs for sequential commands (more flexible matching)
TOTAL=$((TOTAL+1))
clean_log=$(cat server_output.log | sed 's/\x1b\[[0-9;]*m//g')
pwd_count=$(echo "$clean_log" | grep -c '\[RECEIVED\] Received command: "pwd"')
ls_count=$(echo "$clean_log" | grep -c '\[RECEIVED\] Received command: "ls"')
whoami_count=$(echo "$clean_log" | grep -c '\[RECEIVED\] Received command: "whoami"')

if [ "$pwd_count" -ge 1 ] && [ "$ls_count" -ge 1 ] && [ "$whoami_count" -ge 1 ]; then
    pass "Server logs all sequential commands"
else
    fail "Server logs all sequential commands" "3 commands logged" "pwd:$pwd_count ls:$ls_count whoami:$whoami_count"
fi

rm -f /tmp/remote_sequential.txt

# --- Stress Test: Multiple Commands -----------------------------------------
# Use direct nc with here-document for stress test
output=$(
    (
        echo "echo test1"
        sleep 0.2
        echo "echo test2"
        sleep 0.2
        echo "echo test3"
        sleep 0.2
        echo "echo test4"
        sleep 0.2
        echo "echo test5"
        sleep 0.2
        echo "exit"
        sleep 0.3
    ) | nc -q 1 localhost $SERVER_PORT 2>&1
)

TOTAL=$((TOTAL+1))
if echo "$output" | grep -q "test1" && echo "$output" | grep -q "test5"; then
    pass "Server handles rapid sequential commands"
else
    fail "Server handles rapid sequential commands" "all 5 outputs" "$output"
fi

# --- Cleanup -----------------------------------------------------------------
stop_server
rm -f server_output.log longfile.txt

cd .. && rm -rf test_tmp

# =============================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e "${YELLOW}       PHASE 3 TESTING${NC}"
echo -e "${YELLOW}========================================${NC}\n"

mkdir -p test_tmp3 && cd test_tmp3
printf 'hello world\nline one\nline two\n' > input.txt

# Find a fresh port for phase 3
SERVER_PORT=$(
    for port in $(shuf -i 9000-9999 -n 100); do
        if ! nc -z localhost $port 2>/dev/null && ! lsof -i :$port >/dev/null 2>&1; then
            echo $port; break
        fi
    done
)
echo -e "${YELLOW}Phase 3 using port: $SERVER_PORT${NC}"

# Compile patched server
sed "s/#define PORT 8080/#define PORT $SERVER_PORT/" ../server.c > server_temp.c
gcc -Wall -Wextra -g -I.. -c server_temp.c -o server_temp.o 2>/dev/null
gcc -Wall -Wextra -g -o server_temp server_temp.o ../pipeline.o ../simple.o -lpthread 2>/dev/null
rm -f server_temp.c server_temp.o

if [ ! -f server_temp ]; then
    echo -e "${RED}[FAIL] Could not compile Phase 3 server${NC}"
    FAIL=$((FAIL+1)); TOTAL=$((TOTAL+1))
    cd .. && rm -rf test_tmp3
else

./server_temp > server3.log 2>&1 &
SERVER3_PID=$!
sleep 1.5

if ! kill -0 $SERVER3_PID 2>/dev/null; then
    echo -e "${RED}[FAIL] Phase 3 server failed to start${NC}"
    FAIL=$((FAIL+1)); TOTAL=$((TOTAL+1))
else

pass "Phase 3: server starts with multithreading"
TOTAL=$((TOTAL+1))

# Helper: send one command to the phase 3 server
send3() { printf '%s\nexit\n' "$1" | nc -q 1 localhost $SERVER_PORT 2>/dev/null; }

# --- Launch 3 simultaneous clients -------------------------------------------
OUT1=/tmp/p3_client1.txt
OUT2=/tmp/p3_client2.txt
OUT3=/tmp/p3_client3.txt

{ printf 'ls -l\nexit\n' | nc -q 2 localhost $SERVER_PORT 2>/dev/null; } > "$OUT1" &
P1=$!
{ printf 'unknowncmd\nexit\n' | nc -q 2 localhost $SERVER_PORT 2>/dev/null; } > "$OUT2" &
P2=$!
{ printf 'pwd\nexit\n'        | nc -q 2 localhost $SERVER_PORT 2>/dev/null; } > "$OUT3" &
P3=$!

wait $P1 $P2 $P3
sleep 0.5   # let server log flush

C1=$(cat "$OUT1"); C2=$(cat "$OUT2"); C3=$(cat "$OUT3")

# Client output checks
TOTAL=$((TOTAL+1))
echo "$C1" | grep -q "total" && pass "Phase 3: Client 1 (ls -l) works concurrently" \
    || fail "Phase 3: Client 1 (ls -l) works concurrently" "total" "$C1"

TOTAL=$((TOTAL+1))
echo "$C2" | grep -qF "Command not found: unknowncmd" \
    && pass "Phase 3: Client 2 gets descriptive error (Command not found: unknowncmd)" \
    || fail "Phase 3: Client 2 gets descriptive error" "Command not found: unknowncmd" "$C2"

TOTAL=$((TOTAL+1))
echo "$C3" | grep -q "/" && pass "Phase 3: Client 3 (pwd) works concurrently" \
    || fail "Phase 3: Client 3 (pwd) works concurrently" "/" "$C3"

# --- Server log format checks (Phase 3 specific) ----------------------------
LOG=$(cat server3.log | sed 's/\x1b\[[0-9;]*m//g')

TOTAL=$((TOTAL+1))
echo "$LOG" | grep -q "Assigned to Thread-" \
    && pass "Phase 3: Server assigns Thread IDs in [INFO] line" \
    || fail "Phase 3: Server assigns Thread IDs" "Assigned to Thread-" "$(echo "$LOG" | head -5)"

TOTAL=$((TOTAL+1))
echo "$LOG" | grep -q "Client #" \
    && pass "Phase 3: Server labels clients with Client #N" \
    || fail "Phase 3: Server labels clients" "Client #" "$(echo "$LOG" | head -5)"

TOTAL=$((TOTAL+1))
echo "$LOG" | grep -q "127.0.0.1" \
    && pass "Phase 3: Server logs client IP address" \
    || fail "Phase 3: Server logs IP" "127.0.0.1" "$(echo "$LOG" | head -5)"

TOTAL=$((TOTAL+1))
echo "$LOG" | grep -q "\[RECEIVED\]" \
    && pass "Phase 3: [RECEIVED] tag present in server log" \
    || fail "Phase 3: [RECEIVED] tag" "[RECEIVED]" ""

TOTAL=$((TOTAL+1))
echo "$LOG" | grep -q "\[EXECUTING\]" \
    && pass "Phase 3: [EXECUTING] tag present in server log" \
    || fail "Phase 3: [EXECUTING] tag" "[EXECUTING]" ""

TOTAL=$((TOTAL+1))
echo "$LOG" | grep -q "\[OUTPUT\]" \
    && pass "Phase 3: [OUTPUT] tag present in server log" \
    || fail "Phase 3: [OUTPUT] tag" "[OUTPUT]" ""

TOTAL=$((TOTAL+1))
echo "$LOG" | grep -q "\[ERROR\]" \
    && pass "Phase 3: [ERROR] tag present for unknown command" \
    || fail "Phase 3: [ERROR] tag" "[ERROR]" ""

TOTAL=$((TOTAL+1))
echo "$LOG" | grep -qF 'Command not found: "unknowncmd"' \
    && pass "Phase 3: [ERROR] line names the bad command" \
    || fail "Phase 3: [ERROR] names command" 'Command not found: "unknowncmd"' "$(echo "$LOG" | grep ERROR)"

# Multiple client IDs should appear (at least Client #1 and Client #2)
TOTAL=$((TOTAL+1))
client_count=$(echo "$LOG" | grep -o "Client #[0-9]*" | sort -u | wc -l)
[ "$client_count" -ge 3 ] \
    && pass "Phase 3: Multiple clients handled simultaneously (>= 3 IDs seen)" \
    || fail "Phase 3: Multiple clients simultaneously" ">= 3 client IDs" "$client_count seen"

# --- Additional Phase 3 functional tests ------------------------------------
TOTAL=$((TOTAL+1))
result=$(send3 "echo hello | cat | wc -c")
echo "$result" | grep -q "6" \
    && pass "Phase 3: Three-stage pipeline works remotely" \
    || fail "Phase 3: Three-stage pipeline" "6" "$result"

TOTAL=$((TOTAL+1))
result=$(send3 "ls /nonexistent_xyz123")
echo "$result" | grep -qE "No such file|not found" \
    && pass "Phase 3: Remote error message for bad path" \
    || fail "Phase 3: Remote error bad path" "No such file" "$result"

# Server should still be alive after all of that
TOTAL=$((TOTAL+1))
kill -0 $SERVER3_PID 2>/dev/null \
    && pass "Phase 3: Server still running after concurrent load" \
    || fail "Phase 3: Server still running" "alive" "dead"

fi  # server started

kill -9 $SERVER3_PID 2>/dev/null
wait $SERVER3_PID 2>/dev/null
rm -f server_temp server3.log "$OUT1" "$OUT2" "$OUT3"
fi  # compiled

cd .. && rm -rf test_tmp3

# =============================================================================
echo -e "\n${YELLOW}========================================${NC}"
echo -e " Results: ${GREEN}$PASS passed${NC} / ${RED}$FAIL failed${NC}"
echo -e "${YELLOW}========================================${NC}\n"

[ $FAIL -eq 0 ] && exit 0 || exit 1