#!/bin/sh
# Builds the cable and one main<L> per frame size in build/, leaving Lab1/ untouched.
set -e
cd "$(dirname "$0")"
mkdir -p build work
gcc -Wall -o build/cable ../Lab1/cable/cable.c
for L in 125 250 500 1000; do
    rm -rf "build/src$L"
    cp -R ../Lab1/src "build/src$L"
    sed -i '' "s/^#define DATA_CHUNK_SIZE .*/#define DATA_CHUNK_SIZE ($L - DATA_HEADER_SIZE)/" "build/src$L/application_layer.c"
    gcc -Wall -o "build/main$L" "build/src$L"/*.c
done

# Measurement build that injects frame errors at the receiver: with probability INJECT_FER (an
# environment variable) a correct new I frame is treated as having a bad BCC2, so it is answered with REJ.
rm -rf build/srcinject
cp -R ../Lab1/src build/srcinject
python3 - build/srcinject/link_layer.c <<'PY'
import sys
path = sys.argv[1]
s = open(path).read()
hook = '''// Measurement only: answers a correct frame with REJ with probability INJECT_FER (random(): macOS rand() repeats its first value)
static int injectError(void)
{
    static double p = -1;
    if (p < 0)
    {
        const char *value = getenv("INJECT_FER");
        p = value ? atof(value) : 0;
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        srandom((unsigned)now.tv_nsec ^ ((unsigned)getpid() << 16) ^ (unsigned)now.tv_sec);
    }
    return p > 0 && (double)random() / 2147483648.0 < p;
}

// Answers a complete I-frame with RR or REJ.'''
old_check = "    if (!payloadOk(p))\n"
assert s.count("// Answers a complete I-frame with RR or REJ.") == 1 and s.count(old_check) == 1
s = s.replace("// Answers a complete I-frame with RR or REJ.", hook)
s = s.replace(old_check, "    if (!payloadOk(p) || injectError())\n")
open(path, "w").write(s)
PY
gcc -Wall -o build/maininject build/srcinject/*.c
