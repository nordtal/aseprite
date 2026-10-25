#! /bin/bash

# The user config directory is the app's own, never the official Aseprite's
d=$t/config-dir
mkdir -p $d/home
echo 'print(app.fs.userConfigPath)' > $d/path.lua
path=$(env -u ASEPRITE_USER_FOLDER -u XDG_CONFIG_HOME HOME=$d/home APPDATA=$d/home \
           $ASEPRITE -b --script $d/path.lua | tr -d "\r")
lower=$(echo "$path" | tr '[:upper:]' '[:lower:]')
if [[ ! "$lower" =~ nordtal ]] || [[ "$lower" =~ aseprite ]] ; then
    echo "FAILED: user config path is '$path'"
    exit 1
fi
