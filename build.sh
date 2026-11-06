#!/bin/sh
# Compila Freon Machine.
# Mac: necesita las herramientas de Xcode (xcode-select --install).
# Linux / Raspberry Pi: necesita g++ y libasound2-dev.
set -e
cd "$(dirname "$0")"
mkdir -p bin
CXX="${CXX:-c++}"
FLAGS="-std=c++17 -O3 -fno-math-errno -g -Wall -Isrc -Ithird_party"
# Raspberry Pi: instrucciones de su procesador (Pi 5: Cortex-A76; Pi 4: A72; Pi 3: A53)
MODEL="$(tr -d '\0' < /proc/device-tree/model 2>/dev/null || true)"
case "$MODEL" in
  *"Raspberry Pi 5"*) FLAGS="$FLAGS -mcpu=cortex-a76" ;;
  *"Raspberry Pi 4"*) FLAGS="$FLAGS -mcpu=cortex-a72 -DJUNO_OS=1" ;;   # Pi 3 y 4: Juno sin sobremuestreo (menos CPU)
  *"Raspberry Pi 3"*) FLAGS="$FLAGS -mcpu=cortex-a53 -DJUNO_OS=1" ;;
esac
# con poca memoria (Pi 3: 1 GB) se compila sin información de depuración, que es lo que más memoria gasta
MEM_KB="$(awk '/MemTotal/ {print $2}' /proc/meminfo 2>/dev/null || echo 0)"
if [ "$MEM_KB" -gt 0 ] && [ "$MEM_KB" -lt 2000000 ]; then FLAGS="$(echo "$FLAGS" | sed 's/ -g / /')"; echo "Poca memoria: compilo sin depuración (tarda unos minutos)."; fi
# ./build.sh debug → versión con comprobaciones de memoria (más lenta, para buscar fallos)
OUT=bin/freon-machine
if [ "$1" = "debug" ]; then FLAGS="-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Isrc -Ithird_party"; OUT=bin/freon-machine-debug; fi
if [ "$1" = "tsan" ]; then FLAGS="-std=c++17 -O1 -g -fsanitize=thread -Wall -Isrc -Ithird_party"; OUT=bin/freon-machine-tsan; fi
case "$(uname)" in
  Darwin) DEFS="-D__MACOSX_CORE__"; LIBS="-framework CoreAudio -framework CoreMIDI -framework CoreFoundation -framework AudioToolbox -lpthread" ;;
  Linux)  DEFS="-D__LINUX_ALSA__"; LIBS="-lasound -lpthread" ;;
  *) echo "Sistema no soportado todavía: $(uname)"; exit 1 ;;
esac
echo "Compilando Freon Machine (tarda un minuto)..."
$CXX $FLAGS -fno-strict-aliasing -fwrapv -DTEST -Ithird_party/mi $DEFS src/freon-machine.cpp third_party/RtAudio.cpp third_party/RtMidi.cpp src/open303_glue.cpp third_party/open303/*.cpp src/plaits_glue.cpp src/dx7_glue.cpp third_party/msfa/*.cc third_party/mi/stmlib/dsp/units.cc third_party/mi/stmlib/utils/random.cc -o $OUT $LIBS
echo "Listo: ./$OUT"
