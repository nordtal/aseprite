#! /bin/bash
# Copyright (C) 2018 Igara Studio S.A.
# Copyright (C) 2018 David Capello

if [[ "$ASEPRITE" == "" ]]; then
    echo ASEPRITE env var must be pointing to the Aseprite executable
    exit 1
fi

function fail() {
    echo FAIL
    echo $BASH_SOURCE:$BASH_LINENO: error: $*
    exit 1
}

function expect() {
    if [[ $1 != "$($2 | tr -d "\r")" ]] ; then
        echo "FAILED: $2"
        echo "EXPECTED: $1"
        echo "RESULT: $($2)"
        exit 1
    fi
}

# General information
echo ----------------------------------------------------------------------
echo $ASEPRITE --version
$ASEPRITE --version

filter="$*"
if [[ "$filter" != "" ]]; then
    echo Filter: $filter
fi

t=$(mktemp -d)
if [[ "$(uname)" =~ "MINGW" ]] || [[ "$(uname)" =~ "MSYS" ]] ; then
    PWDARG=-W
    t=$(cd "$t" && pwd $PWDARG)
else
    PWDARG=
fi
echo Temp dir: $t
export ASEPRITE_USER_FOLDER=$t

if [[ "$filter" == "" ]] || [[ "console" =~ $filter ]]; then
    echo ----------------------------------------------------------------------
    echo "Testing console..."
    echo "uname=$(uname)"

    $ASEPRITE -b --script scripts/console_assert.lua >$t/tmp 2>&1
    ! grep -q "this should be in the output" $t/tmp && fail "print() text not found in output"
    ! grep -q "assertion failed" $t/tmp && fail "assert() text not found in output"
    grep -q "this should not be in the output" $t/tmp && fail "text that shouldn't be in the output is"

    if [[ "$(uname)" =~ "MINGW" ]] || [[ "$(uname)" =~ "MSYS" ]] ; then
        echo Ignore console tests on Windows
    else
        $ASEPRITE -b --script scripts/console_print.lua >$t/tmp 2>&1
        echo -e "hello world\n1\t2\t3" >$t/tmp_expected
        ! diff -u $t/tmp $t/tmp_expected && fail
    fi
fi

# Runs a command and kills it after $limit seconds, so a hung script
# fails the run instead of holding it; macOS has no timeout command
limit=${ASEPRITE_TEST_TIMEOUT:-300}
function run_limited() {
    "$@" &
    local pid=$!
    ( for ((i = 0; i < limit; i++)); do
          sleep 1
          kill -0 $pid 2>/dev/null || exit 0
      done
      echo "killed after $limit s" >&2
      kill -KILL $pid 2>/dev/null ) &
    local watchdog=$!
    wait $pid
    local status=$?
    kill $watchdog 2>/dev/null
    wait $watchdog 2>/dev/null
    return $status
}

first=0
result=0
for script in scripts/*.lua ; do
    [[ $script =~ console ]] && continue
    if [[ "$filter" != "" ]]; then
        [[ $script =~ $filter ]] || continue
    fi
    if [ $first == 0 ]; then
        echo ----------------------------------------------------------------------
        echo "Testing scripts..."
        first=1
    fi
    echo "Running $script"
    if ! run_limited $ASEPRITE -b --script $script >$t/tmp 2>&1 ; then
        echo FAILED && cat $t/tmp
        result=1
    fi
done

first=0
for script in cli/*.sh ; do
    if [[ "$filter" == "" ]] || [[ $script =~ $filter ]]; then
        if [ $first == 0 ]; then
            echo ----------------------------------------------------------------------
            echo "Testing CLI..."
            first=1
        fi
        echo "Running $script"
        source $script
    fi
done

echo ----------------------------------------------------------------------
if [ $result == 0 ] ; then
    echo Done
else
    echo FAILED
fi
echo ----------------------------------------------------------------------

exit $result
