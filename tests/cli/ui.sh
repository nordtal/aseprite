#! /bin/bash

# Scripts in ui/ need the GUI, so they run without -b. Each one ends
# the app itself and prints PASS. A script that hangs is stopped.
for script in ui/*.lua ; do
    echo "Running $script"
    $ASEPRITE --script $script >$t/ui.out 2>&1 &
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
    kill $watchdog 2>/dev/null
    if ! grep -q "^PASS" $t/ui.out ; then
        echo "FAILED: $script"
        cat $t/ui.out
        result=1
    fi
done
