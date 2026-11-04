#! /bin/bash

# A script that hangs is killed after the time limit, together with
# what it started, and one that ends keeps its exit status
d=$t/run-limited
mkdir -p $d
saved=$limit
limit=2
run_limited true || { echo "FAILED: run_limited true"; exit 1; }
run_limited sh -c 'exit 7'
[[ $? == 7 ]] || { echo "FAILED: run_limited keeps the exit status"; exit 1; }
# The command after the inner sh keeps a shell from exec'ing it, so the
# inner sh is not the direct child
run_limited sh -c "sh -c 'echo \$\$ > $d/pid; exec sleep 60'; true" 2>/dev/null &&
    { echo "FAILED: run_limited ends a hung command with success"; exit 1; }
[[ -s $d/pid ]] || { echo "FAILED: the hung command did not start"; exit 1; }
stat=$(ps -p $(cat $d/pid) -o stat= 2>/dev/null)
[[ "$stat" == "" ]] || [[ "$stat" =~ Z ]] ||
    { echo "FAILED: run_limited leaves what the command started running"; exit 1; }
limit=$saved
