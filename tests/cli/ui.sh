#! /bin/bash

# Scripts in ui/ need the GUI, so they run without -b, after
# ui-helper.lua, which defines ui_test(). Each one ends the app itself
# and prints PASS. A script that hangs is stopped. They share the user
# folder $t, in the order of their names.
#
# Scripts in ui/mirrored/ run the same way in a user folder of their
# own, which starts with Aseprite's mirrored layout.

# In the GUI a script asks before it runs a program, so the scripts in
# the folder $1 get that access beforehand in the user folder $2
function grant_ui_access() {
    {
        echo "[script_access]"
        for script in $1/*.lua ; do
            echo "$(printf %s "$PWD/$script" | shasum | cut -c1-40) = 1"
        done
    } >>$2/aseprite.ini
}

# Runs the scripts in the folder $1 with the user folder $2
function run_ui_scripts() {
    for script in $1/*.lua ; do
        echo "Running $script"
        ASEPRITE_USER_FOLDER=$2 $ASEPRITE --script ui-helper.lua --script $script >$2/ui.out 2>&1 &
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
        if [ $status -ne 0 ] || ! grep -q "^PASS" $2/ui.out ; then
            echo "FAILED: $script (exit code $status)"
            cat $2/ui.out
            result=1
        fi
    done
}

grant_ui_access ui $t
run_ui_scripts ui $t

mirrored=$t/mirrored
mkdir -p $mirrored
printf "[general]\nworkspace_layout = _mirrored_default_\n" >$mirrored/aseprite.ini
grant_ui_access ui/mirrored $mirrored
run_ui_scripts ui/mirrored $mirrored
