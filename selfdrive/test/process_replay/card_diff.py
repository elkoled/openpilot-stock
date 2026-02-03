#!/usr/bin/env python3
import concurrent.futures
import os
import sys
import traceback
from collections import defaultdict
from tqdm import tqdm
from typing import Any

from openpilot.selfdrive.test.process_replay.compare_logs import compare_logs, format_process_diff
from openpilot.selfdrive.test.process_replay.process_replay import FAKEDATA, replay_process, \
                                                                   get_process_config, check_most_messages_valid
from openpilot.selfdrive.test.process_replay.test_processes import segments, REF_COMMIT_FN
from openpilot.tools.lib.filereader import FileReader
from openpilot.tools.lib.logreader import LogReader
from openpilot.tools.lib.openpilotci import get_url
from openpilot.tools.lib.url_file import URLFile

BASE_URL = "https://raw.githubusercontent.com/commaai/ci-artifacts/refs/heads/process-replay/"
CARD_CFG = get_process_config("card")


# copied from test_processes.py

def get_log_data(segment):
  r, n = segment.rsplit("--", 1)
  with FileReader(get_url(r, n, "rlog.zst")) as f:
    return (segment, f.read())


def test_process(cfg, lr, segment, ref_log_path):
  ref_log_msgs = list(LogReader(ref_log_path))

  try:
    log_msgs = replay_process(cfg, lr, disable_progress=True)
  except Exception as e:
    raise Exception("failed on segment: " + segment) from e

  if not check_most_messages_valid(log_msgs):
    return "Route did not have enough valid messages"

  seen_msgs = {m.which() for m in log_msgs}
  expected_msgs = set(cfg.subs)
  if seen_msgs != expected_msgs:
    return f"Expected messages: {expected_msgs}, but got: {seen_msgs}"

  try:
    return compare_logs(ref_log_msgs, log_msgs, cfg.ignore, [], cfg.tolerance)
  except Exception as e:
    return str(e)


def run_test_process(data):
  segment, car_brand, cfg, ref_log_path, lr_dat = data
  try:
    lr = LogReader.from_bytes(lr_dat)
    res = test_process(cfg, lr, segment, ref_log_path)
    return (car_brand, segment, res)
  except Exception:
    return (car_brand, segment, traceback.format_exc())


# -- copied from opendbc car_diff.py main() --

def main() -> int:
  jobs = max((os.cpu_count() or 2) - 2, 1)

  try:
    with open(REF_COMMIT_FN) as f:
      ref_commit = f.read().strip()
  except FileNotFoundError:
    ref_commit = URLFile(BASE_URL + "ref_commit", cache=False).read().decode().strip()

  print("## Process replay report")
  print("Replays driving segments through card and compares the output to master.")
  print("Please review any changes carefully to ensure they are expected.\n")

  # download segment logs (same as test_processes.py)
  download_segments = [seg for _, seg in segments]
  log_data: dict[str, bytes] = {}
  with concurrent.futures.ProcessPoolExecutor(max_workers=jobs) as pool:
    for segment, lr_dat in tqdm(pool.map(get_log_data, download_segments), desc="Getting Logs", total=len(download_segments)):
      log_data[segment] = lr_dat

  # build work items — card only, all segments (same as test_processes.py)
  pool_args: Any = []
  for car_brand, segment in segments:
    ref_log_fn = os.path.join(FAKEDATA, f"{segment}_card_{ref_commit}.zst".replace("|", "_"))
    ref_log_path = ref_log_fn if os.path.exists(ref_log_fn) else BASE_URL + os.path.basename(ref_log_fn)
    pool_args.append((segment, car_brand, CARD_CFG, ref_log_path, log_data[segment]))

  # run replays (same as test_processes.py)
  results: defaultdict[str, dict] = defaultdict(dict)
  with concurrent.futures.ProcessPoolExecutor(max_workers=jobs) as pool:
    for car_brand, segment, result in tqdm(pool.map(run_test_process, pool_args), desc="Running Tests", total=len(pool_args)):
      results[car_brand][segment] = result

  # format output (same pattern as opendbc car_diff.py main())
  with_diffs = [(car, seg, diff) for car, segs in results.items() for seg, diff in segs.items()
                if isinstance(diff, str) or (diff is not None and len(diff) > 0)]
  errors = [(car, seg, diff) for car, seg, diff in with_diffs if isinstance(diff, str)]
  n_passed = sum(1 for car, segs in results.items() for seg, diff in segs.items()
                 if not isinstance(diff, str) and (diff is None or len(diff) == 0))

  icon = "⚠️" if with_diffs else "✅"
  print(f"{icon}  {len(with_diffs) - len(errors)} changed, {n_passed} passed, {len(errors)} errors")
  print(f"_ref: `{ref_commit[:12]}`_")

  for car, seg, err in errors:
    print(f"\nERROR {car} - {seg}:\n{err}")

  diffs_only = [(car, seg, diff) for car, seg, diff in with_diffs if not isinstance(diff, str)]
  if diffs_only:
    print("\n<details><summary><b>Show changes</b></summary>\n\n```")
    for car, seg, diff in diffs_only:
      print(f"\n{car} - {seg}")
      diff_short, _ = format_process_diff(diff)
      print(diff_short)
    print("```\n</details>")

  return 1 if errors else 0


if __name__ == "__main__":
  sys.exit(main())
