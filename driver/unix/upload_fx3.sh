#!/bin/bash

if [[ "$OSTYPE" == "darwin"* ]]; then
    # Mac OSX
    export DYLD_LIBRARY_PATH=/usr/local/lib/:/local/lib/:$DYLD_LIBRARY_PATH
else
    # Linux
    export LD_LIBRARY_PATH=/usr/local/lib/:/local/lib/:$LD_LIBRARY_PATH
fi

/usr/local/bin/fwload_fx3 $@
