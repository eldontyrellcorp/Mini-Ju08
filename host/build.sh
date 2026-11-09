#!/bin/sh
# Host tests of the fixed-point Jupiter-8 engine against the float reference (JU_REF, default ~/jupiter8_esp32).
set -e
cd "$(dirname "$0")"
REF=${JU_REF:-$HOME/jupiter8_esp32}
mkdir -p out
REFD="$REF/src/dsp"
g++ -std=c++17 -O1 -w -I$REF/src -I$REF/src/dsp gen_presets.cpp $REFD/params.cpp $REFD/presets.cpp $REFD/fx_params.cpp $REFD/engine.cpp $REFD/fx.cpp -o out/gen_presets && ./out/gen_presets ../firmware/src/ju_presets.h > /dev/null
python3 ../tools/gen_ju_tables.py ../firmware/src/ju_tables.h > /dev/null
gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -I../firmware/src -c ../firmware/src/ju_dsp.c -o out/ju_dsp.o
gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -I../firmware/src -c ../firmware/src/ju_fxdef.c -o out/ju_fxdef.o
gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -I../firmware/src -c ../firmware/src/ju_pinfo.c -o out/ju_pinfo.o
D="$REF/src/dsp/engine.cpp $REF/src/dsp/params.cpp $REF/src/dsp/presets.cpp $REF/src/dsp/fx.cpp $REF/src/dsp/fx_params.cpp"
g++ -std=c++17 -O2 -ffast-math -w -I$REF/src -I$REF/host -I. -I../firmware/src ju_test.cpp out/ju_dsp.o out/ju_fxdef.o out/ju_pinfo.o $D -o out/ju_test
rc=0
./out/ju_test "$@" || rc=1
g++ -std=c++17 -O2 -ffast-math -w -I$REF/src -I../firmware/src ju_fx_test.cpp out/ju_dsp.o out/ju_fxdef.o out/ju_pinfo.o $D -o out/ju_fx_test
./out/ju_fx_test || rc=1
gcc -std=c11 -O1 -g -fsanitize=undefined,address -fno-sanitize-recover=undefined -I../firmware/src ju_fuzz.c ../firmware/src/ju_dsp.c ../firmware/src/ju_fxdef.c -o out/ju_fuzz
./out/ju_fuzz 200 || rc=1
gcc -std=gnu11 -O1 -g -fsanitize=undefined,address -Wall -Wno-unused-function -I../firmware/src ju_ui_test.c ../firmware/src/ju_dsp.c ../firmware/src/ju_fxdef.c ../firmware/src/ju_pinfo.c -o out/ju_ui_test
./out/ju_ui_test || rc=1
gcc -std=gnu11 -O1 -Wall -Wno-unused-function -I../firmware/src ju_ui_snap.c ../firmware/src/ju_dsp.c ../firmware/src/ju_fxdef.c ../firmware/src/ju_pinfo.c -o out/ju_ui_snap
mkdir -p out/ui && ./out/ju_ui_snap out/ui > /dev/null
exit $rc
