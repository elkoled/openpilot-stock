#!/usr/bin/env python3
import sys
from collections import Counter

from openpilot.tools.lib.logreader import LogReader
from openpilot.selfdrive.test.process_replay.process_replay import (
  replay_process, get_process_config,
)

CARD_CFG = get_process_config("card")

route = sys.argv[1]
print(f"Replaying: {route}")
lr = list(LogReader(route))
result = replay_process(CARD_CFG, lr)
print(f"Got {len(result)} msgs: {dict(Counter(m.which() for m in result))}")
