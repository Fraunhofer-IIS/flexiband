#!/bin/bash

export LD_LIBRARY_PATH=/usr/local/lib/:/local/lib/:$LD_LIBRARY_PATH

/usr/local/bin/fwload_fx3 -f $1
