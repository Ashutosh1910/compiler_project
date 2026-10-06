#!/bin/bash
# Records docs/demo/toycc-demo.mp4: a real xterm on a virtual X display
# (Xvfb), driven with xdotool and captured with ffmpeg, showing toycc -- a
# compiler for TL written in the extended course language -- being built by
# our compiler, then compiling and running a TL program.
# needs: Xvfb xterm xdotool ffmpeg nasm gcc;  usage: record_demo.sh <repo> <out.mp4>
set -e
REPO=$1
OUT=$2
WORK=/tmp/claude-0/rec/demo
rm -rf "$WORK" && mkdir -p "$WORK"
cp "$REPO/docs/demo/demo.tl" "$REPO/docs/demo/oops.tl" "$WORK"/
cd "$WORK"
ln -s "$REPO/compiler" compiler
ln -s "$REPO/toy" toy
ln -s "$REPO/grammar.txt" grammar.txt

export DISPLAY=:99
Xvfb :99 -screen 0 1280x800x24 >/dev/null 2>&1 &
XPID=$!
sleep 1
# a clean bash with a short prompt
cat > "$WORK/.bashrc" <<'RC'
PS1='\[\e[1;32m\]demo\[\e[0m\]$ '
cd /tmp/claude-0/rec/demo
RC
xterm -geometry 122x38+0+0 -fa 'DejaVu Sans Mono' -fs 13 -bg '#1d1f21' \
  -fg '#e6e6e6' -bc -e bash --rcfile "$WORK/.bashrc" -i &
TPID=$!
sleep 2
ffmpeg -y -loglevel error -f x11grab -video_size 1280x800 -framerate 25 \
  -i :99 -c:v libx264 -preset veryfast -pix_fmt yuv420p "$OUT" &
FPID=$!
sleep 1

say() { # type a command like a person, then run it and wait
  xdotool type --delay 35 "$1"
  sleep 0.4
  xdotool key Return
  sleep "${2:-2}"
}

say "# toycc: a compiler for the toy language TL, written in OUR course language" 1
say "head -32 toy/toycc.txt" 5
say "wc -l toy/toycc.txt" 2
say "# 1. our C compiler compiles the toy compiler into a native program" 1
say "./compiler --build toy/toycc.txt toycc && ls -l toycc" 3
say "clear" 1
say "# 2. a TL program: Fibonacci numbers and primes" 1
say "cat demo.tl" 5
say "# 3. toycc compiles it to x86-64 assembly" 1
say "./toycc < demo.tl > demo.asm && wc -l demo.asm && head -22 demo.asm" 6
say "clear" 1
say "# 4. assemble, link and run the program toycc produced" 1
say "nasm -f elf64 demo.asm -o demo.o && gcc -no-pie demo.o -o demo && ./demo | paste -sd' '" 4
say "# 5. errors are reported with their line" 1
say "cat oops.tl; ./toycc < oops.tl | tail -1; echo \"toycc exit status \${PIPESTATUS[0]}\"" 4
say "# 6. the toy compiler's 38 acceptance tests, then the whole test suite" 1
say "python3 $REPO/tests/run_tests.py -k toy | tail -1" 6
say "python3 $REPO/tests/run_tests.py -j 16 | tail -1" 10
sleep 4

kill -INT $FPID; wait $FPID || true
kill $TPID $XPID 2>/dev/null || true
