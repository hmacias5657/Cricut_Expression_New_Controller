# ArtifactEnvelope Template
# For Medium/Large tasks only. Skip for Trivial/Small per §0.

project:        esp32-gcode-plotter
branch:         <git branch>
head_before:    <sha before changes>
head_after:     <sha after changes>
files_changed:  [paths]
diff_summary:   <short description>

explore:
  status:       [pending | done]
  findings:     <key discoveries>

coder:
  status:       [pending | done]
  files:        [modified files]
  conventions:  <patterns followed>

builder:
  status:       [pending | done]
  command:      pio run
  output:       <last 20 lines>
  errors:       <any errors>

test-author:
  status:       [pending | done]
  files:        [test files created]
  test_count:   <number>
  coverage:     [areas tested]

test-executor:
  status:       [pending | done]
  command:      pio test
  total:        <count>
  passed:       <count>
  failed:       <count>
  skipped:      <count>
  failures:     [list]
  duration:     <time>

verifier:
  status:       [pending | done]
  checks:       [Level 1: compile, Level 2: lint]
  issues:       [list]

git:
  status:       [pending | done]
  commit_hash:  <sha>
  staged_files: [list]

docs:
  status:       [pending | done]
  files:        [updated files]