#! /bin/bash

# Scripts in ui/ need the GUI, so they run without -b, after
# ui-helper.lua, which defines ui_test(). Each one ends the app itself
# and prints PASS. A script that hangs is stopped.

# In the GUI a script asks before it runs a program, so the scripts in
# ui/ get that access beforehand
{
    echo "[script_access]"
    for script in ui/*.lua ; do
        echo "$(printf %s "$PWD/$script" | shasum | cut -c1-40) = 1"
    done
} >>$t/aseprite.ini

for script in ui/*.lua ; do
    echo "Running $script"
    $ASEPRITE --script ui-helper.lua --script $script >$t/ui.out 2>&1 &
    pid=$!
    (
        for i in $(seq 120) ; do
            kill -0 $pid 2>/dev/null || exit 0
            sleep 0.5
        done
        kill -9 $pid 2>/dev/null
    ) &
    watchdog=$!
    wait $pid
    status=$?
    kill $watchdog 2>/dev/null
    # The app has to end cleanly too
    if [ $status -ne 0 ] || ! grep -q "^PASS" $t/ui.out ; then
        echo "FAILED: $script (exit code $status)"
        cat $t/ui.out
        result=1
    fi
done
