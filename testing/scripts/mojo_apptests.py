#!/usr/bin/env python
# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import sys


import common


def main_run(args):
  runner = os.path.join(common.SRC_DIR, 'mojo', 'tools', 'apptest_runner.py')
  build_dir = os.path.join(common.SRC_DIR, 'out', args.build_config_fs)

  with common.temporary_file() as tempfile_path:
    rc = common.run_command([sys.executable, runner, build_dir, '--verbose',
                             '--write-full-results-to', tempfile_path])
    with open(tempfile_path) as f:
      results = json.load(f)

  parsed_results = common.parse_common_test_results(results, test_separator='.')
  failures = parsed_results['unexpected_failures']

  json.dump({
      'valid': bool(rc <= common.MAX_FAILURES_EXIT_STATUS and
                   ((rc == 0) or failures)),
      'failures': failures.keys(),
  }, args.output)

  return rc


def main_compile_targets(args):
  json.dump(['mojo_apptests'], args.output)


if __name__ == '__main__':
  funcs = {
    'run': main_run,
    'compile_targets': main_compile_targets,
  }
  sys.exit(common.run_script(sys.argv[1:], funcs))
