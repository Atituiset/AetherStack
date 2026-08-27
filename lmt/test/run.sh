#!/bin/sh
# Compile + run the derivation unit assertions (no test runner in this repo).
set -e
cd "$(dirname "$0")/.."
npx tsc -p test/tsconfig.json
# package.json says "type": "module"; the compiled output is CommonJS
printf '{"type":"commonjs"}\n' > test/.out/package.json
node test/.out/test/derive.test.js
