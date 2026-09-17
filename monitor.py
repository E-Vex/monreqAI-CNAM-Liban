#!/usr/bin/env python3
"""
Backwards-compatible shim.

The monitor now lives in the `isae_monitor` package. Existing cron entries
that call `python3 monitor.py` keep working; new ones should prefer
`python3 -m isae_monitor`.
"""

import sys

from isae_monitor.cli import main

if __name__ == "__main__":
    sys.exit(main())
