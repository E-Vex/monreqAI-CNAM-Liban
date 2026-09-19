# ISAE Monitor C

Pure C11 implementation of the ISAE School announcement monitoring system. This is a complete rewrite of the Python version with zero dependencies on Python runtime.

## Features

- **Multi-department feed monitoring**: Monitors RSS/Atom feeds from 9 engineering departments
- **AI-powered classification**: Uses Google Gemini or OpenRouter (Llama) for intelligent announcement classification
- **Keyword fallback**: Falls back to keyword-based classification when AI is unavailable
- **Telegram notifications**: Sends formatted notifications to Telegram channels
- **Persistent state**: Tracks processed announcements to avoid duplicates
- **Memory safe**: Zero memory leaks, valgrind-clean code

## Architecture

```
isae_monitor/
├── include/isae_monitor/   # Header files
│   ├── common.h           # Types, constants, error codes
│   ├── departments.h      # Department registry
│   ├── models.h           # Data structures
│   ├── config.h           # Configuration parsing
│   ├── state.h            # State persistence
│   ├── httpclient.h       # HTTP client wrapper
│   ├── feed.h             # Feed fetching/parsing
│   ├── providers.h        # AI provider APIs
│   ├── keywords.h         # Keyword classifier
│   ├── classifier.h       # Classification orchestration
│   ├── telegram.h         # Telegram API
│   └── pipeline.h         # Main orchestration
├── src/                    # Implementation files
│   ├── main.c
│   ├── departments.c
│   ├── models.c
│   ├── config.c
│   ├── state.c
│   ├── httpclient.c
│   ├── feed.c
│   ├── providers.c
│   ├── keywords.c
│   ├── classifier.c
│   ├── telegram.c
│   └── pipeline.c
├── CMakeLists.txt         # CMake build configuration
├── Makefile               # GNU Make build configuration
└── README.md              # This file
```

## Dependencies

### System Requirements
- GCC or Clang with C11 support
- CMake 3.10+ (optional, for CMake build)
- pkg-config

### External Libraries
- **libcurl** - HTTP/HTTPS requests
- **libxml2** - XML/RSS/Atom feed parsing

### Installation (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libcurl4-openssl-dev libxml2-dev
```

### Installation (macOS)
```bash
brew install cmake pkg-config curl libxml2
```

## Building

### Using CMake (Recommended)
```bash
mkdir build && cd build
cmake ..
make
./isae_monitor --help
```

### Using Make
```bash
make
./isae_monitor --help
```

### Debug Build
```bash
make debug
# or
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

## Configuration

Configuration is done via environment variables:

| Variable | Description | Default |
|----------|-------------|---------|
| `ISAE_STATE_FILE` | Path to state JSON file | `~/.isae_monitor_state.json` |
| `ISAE_POLL_INTERVAL` | Polling interval in seconds | `300` |
| `ISAE_HTTP_TIMEOUT` | HTTP request timeout in seconds | `30` |
| `GEMINI_API_KEY` | Google Gemini API key | (none) |
| `OPENROUTER_API_KEY` | OpenRouter API key | (none) |
| `TELEGRAM_BOT_TOKEN` | Telegram bot token | (none) |
| `TELEGRAM_CHAT_IDS` | Comma-separated chat IDs | (none) |

### Legacy Environment Variables
The following legacy variables are also supported:
- `STATE_FILE` → `ISAE_STATE_FILE`
- `POLL_INTERVAL` → `ISAE_POLL_INTERVAL`
- `HTTP_TIMEOUT` → `ISAE_HTTP_TIMEOUT`

## Usage

### One-shot Mode (Default)
Run once and exit:
```bash
./isae_monitor
```

### Continuous Monitoring Mode
Run continuously with specified interval:
```bash
./isae_monitor --interval 60
```

### Show Help
```bash
./isae_monitor --help
```

### Show Version
```bash
./isae_monitor --version
```

## Example: Running with Telegram Notifications

```bash
export GEMINI_API_KEY="your-gemini-api-key"
export TELEGRAM_BOT_TOKEN="your-bot-token"
export TELEGRAM_CHAT_IDS="123456789,-987654321"
export ISAE_POLL_INTERVAL="60"

./isae_monitor --interval 60
```

## Departments Monitored

1. Computer Science (INFO)
2. Electronics (ELECM)
3. Electrical Engineering (ELM)
4. Mechanical Engineering (MECM)
5. Civil Engineering (CECM)
6. Industrial Engineering (GPIM)
7. Applied Economics (EAC)
8. Mathematics (Maths)
9. Physics (Physique)

## Memory Safety

This implementation is designed to be memory-safe:
- All allocations are checked for NULL
- Every `malloc`/`calloc`/`realloc` has a corresponding `free`
- String operations use bounded functions (`strncpy`, `snprintf`)
- The code passes `valgrind --leak-check=full` cleanly

Run valgrind check:
```bash
make valgrind
```

## Error Handling

The C version uses explicit error codes instead of exceptions:

| Error Code | Value | Description |
|------------|-------|-------------|
| `ISAE_OK` | 0 | Success |
| `ISAE_ERR_MEMORY` | -1 | Memory allocation failed |
| `ISAE_ERR_INVALID_PARAM` | -2 | Invalid parameter |
| `ISAE_ERR_IO` | -3 | I/O error |
| `ISAE_ERR_PARSE` | -4 | Parse error |
| `ISAE_ERR_HTTP` | -5 | HTTP error |
| `ISAE_ERR_CONFIG` | -6 | Configuration error |
| `ISAE_ERR_CLASSIFICATION` | -7 | Classification failed |

## Differences from Python Version

1. **No Python Runtime**: Pure native C executable, ~50KB binary
2. **Faster Startup**: No interpreter overhead
3. **Lower Memory**: ~5MB vs ~50MB for Python
4. **Static Typing**: Compile-time type checking
5. **Explicit Error Handling**: Return codes instead of exceptions

## License

Same license as the original Python project.

## Contributing

1. Follow the existing code style (C11, explicit types, goto cleanup pattern)
2. Add comments explaining Python-to-C translations
3. Ensure valgrind-clean code
4. Test with both CMake and Make builds

## Troubleshooting

### Build fails with "libxml2 not found"
```bash
sudo apt-get install libxml2-dev
# or
brew install libxml2
```

### Build fails with "libcurl not found"
```bash
sudo apt-get install libcurl4-openssl-dev
# or
brew install curl
```

### Runtime error: "Failed to fetch feed"
- Check network connectivity
- Verify feed URLs are accessible
- Increase timeout: `export ISAE_HTTP_TIMEOUT=60`

### No Telegram notifications received
- Verify bot token is valid
- Ensure chat IDs are correct (negative for groups/channels)
- Check that the bot is added to the channels
