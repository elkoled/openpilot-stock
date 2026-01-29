#!/usr/bin/env python3
import argparse
import sys
from collections import Counter

from openpilot.tools.lib.logreader import LogReader
from openpilot.selfdrive.test.process_replay.process_replay import (
  replay_process, get_process_config,
)

CARD_CFG = get_process_config("card")


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("route")
  args = parser.parse_args()

  print(f"Replaying: {args.route}")
  lr = list(LogReader(args.route))
  result = replay_process(CARD_CFG, lr)
  print(f"Got {len(result)} msgs: {dict(Counter(m.which() for m in result))}")


if __name__ == "__main__":
  main()
