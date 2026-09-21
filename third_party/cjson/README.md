# cJSON (vendored, v1.7.18)

Single-file C JSON parser from <https://github.com/DaveGamble/cJSON>
released under the MIT license. Vendored so the project builds without
the `libcjson-dev` / `libcjson-devel` system package, which is missing
from many minimal containers and macOS Homebrew setups.

## Updating

```bash
cd third_party/cjson
curl -fsSL https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.18/cJSON.h -o cJSON.h
curl -fsSL https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.18/cJSON.c -o cJSON.c
```

Re-check the upstream LICENSE block at the top of `cJSON.h` (MIT) before
committing the update.
