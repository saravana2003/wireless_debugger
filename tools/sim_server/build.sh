#!/bin/sh
# Builds .pio/sim_server.exe with the MinGW gcc on PATH. Run from the repo root (Git Bash).
set -e
OUT=.pio/sim_server_obj
mkdir -p "$OUT"
INC="-Iinclude -Ilib/dap_engine -Ilib/swd_phy -Ilib/swd_sim -Ilib/net_server"
gcc -O1 $INC -c lib/dap_engine/DAP.c -o "$OUT/DAP.o"
g++ -std=gnu++17 -O1 $INC \
    tools/sim_server/sim_server.cpp lib/dap_engine/dap_glue.cpp lib/swd_sim/swd_sim.cpp lib/swd_sim/sim_core.cpp \
    lib/net_server/framing_dap_tcp.cpp "$OUT/DAP.o" -lws2_32 -static -o .pio/sim_server.exe
echo "built .pio/sim_server.exe"
