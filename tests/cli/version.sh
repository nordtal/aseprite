#! /bin/bash
# Copyright (C) 2018 Igara Studio S.A.

if ! $ASEPRITE --version | grep "Nordtal Designer 1\\." > /dev/null ; then
    echo "FAILED: --version doesn't include 'Nordtal Designer 1.' string"
    exit 1
fi
