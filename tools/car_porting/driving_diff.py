#!/usr/bin/env python3
import argparse
import pickle
import sys
import tempfile
import traceback
import zstandard as zstd
from collections import defaultdict
from pathlib import Path

from opendbc.car.tests.car_diff import dict_diff, format_diff, download_refs, run_replay
from openpilot.tools.lib.logreader import LogReader
from openpilot.selfdrive.test.process_replay.process_replay import (
  replay_process_with_name, get_custom_params_from_lr
)
from openpilot.selfdrive.test.process_replay.test_processes import source_segments


TOLERANCE = 0.05
DIFF_BUCKET = "driving_diff"
IGNORE_FIELDS = ["modelMonoTime", "solverExecutionTime", "processingDelay"]


def process_segment(args):
  platform, seg, ref_path, update = args
  try:
    lr = LogReader(seg, sort_by_time=True)
    msgs = list(lr)
    custom_params = get_custom_params_from_lr(msgs, initial_state="first")
    replayed = replay_process_with_name("plannerd", msgs, custom_params=custom_params, return_all_logs=True, disable_progress=True)
    data = [(m.logMonoTime, m.longitudinalPlan.to_dict()) for m in replayed if m.which() == "longitudinalPlan"]
    ref_file = Path(ref_path) / f"{platform}_{seg.replace('/', '_').replace('|', '_')}.zst"

    if update:
      ref_file.write_bytes(zstd.compress(pickle.dumps(data), 10))
      return (platform, seg, [], None, None)

    if not ref_file.exists():
      return (platform, seg, [], None, "no ref")

    ref = pickle.loads(zstd.decompress(ref_file.read_bytes()))
    diffs = []
    for i, ((ts, ref_state), (_, state)) in enumerate(zip(ref, data, strict=True)):
      for diff in dict_diff(ref_state, state, ignore=IGNORE_FIELDS, tolerance=TOLERANCE):
        diffs.append((diff[1], i, diff[2], ts))
    return (platform, seg, diffs, ref, None)
  except Exception:
    return (platform, seg, [], None, traceback.format_exc())


def main(platform=None, update_refs=False, all_platforms=False):
  cwd = Path(__file__).resolve().parents[2]
  ref_path = cwd / DIFF_BUCKET
  if not update_refs:
    ref_path = Path(tempfile.mkdtemp())
  ref_path.mkdir(exist_ok=True)
  database = dict(source_segments)

  if all_platforms:
    print("Running all platforms...")
    platforms = list(database.keys())
  elif platform and platform in database:
    platforms = [platform]
  else:
    platforms = list(database.keys())

  segments = {p: [database[p]] for p in platforms}
  n_segments = sum(len(s) for s in segments.values())
  print(f"{'Generating' if update_refs else 'Testing'} {n_segments} segments for: {', '.join(platforms)}")

  if update_refs:
    results = run_replay(platforms, segments, ref_path, update=True, process_fn=process_segment)
    errors = [e for _, _, _, _, e in results if e]
    assert len(errors) == 0, f"Segment failures: {errors}"
    print(f"Generated {n_segments} refs to {ref_path}")
    return 0

  download_refs(ref_path, platforms, segments, diff_bucket=DIFF_BUCKET)
  results = run_replay(platforms, segments, ref_path, update=False, process_fn=process_segment)

  with_diffs = [(p, s, d, r) for p, s, d, r, e in results if d]
  errors = [(p, s, e) for p, s, d, r, e in results if e]
  n_passed = len(results) - len(with_diffs) - len(errors)

  print(f"\nResults: {n_passed} passed, {len(with_diffs)} with diffs, {len(errors)} errors")

  for plat, seg, err in errors:
    print(f"\nERROR {plat} - {seg}: {err}")

  if with_diffs:
    print("```")
    for plat, seg, diffs, ref in with_diffs:
      print(f"\n{plat} - {seg}")
      by_field = defaultdict(list)
      for d in diffs:
        by_field[d[0]].append(d)
      for field, fd in sorted(by_field.items()):
        print(f"  {field} ({len(fd)} diffs)")
        for line in format_diff(fd, ref, field):
          print(line)
    print("```")

  return 1 if errors else 0


if __name__ == "__main__":
  parser = argparse.ArgumentParser()
  parser.add_argument("--platform", help="diff single platform")
  parser.add_argument("--update-refs", action="store_true", help="update refs based on current commit")
  parser.add_argument("--all", action="store_true", help="run diff on all platforms")
  args = parser.parse_args()
  sys.exit(main(args.platform, args.update_refs, args.all))
