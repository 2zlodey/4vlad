# Perf Tests (Boost.Test)

Perf probe tests are now part of the shared two_way_lib test suite.

## Test Sources

- `lib/two_way_lib/tests/core/src/perf/test_perf_probe.cpp`
- `lib/two_way_lib/tests/core/src/perf/perf_probe_hal_mock.c`

The test target also compiles:
- `lib/two_way_lib/src/perf_lib/perf_probe.c`

## Build and Run

From repository root, configure and build the `core` suite with a desktop platform (example: `demo_cpp`):

```powershell
cmake -S lib/two_way_lib/tests -B build/twlib-tests -DTEST_SUITE=core -DPLATFORM_DIR=lib/two_way_lib/platforms/demo_cpp
cmake --build build/twlib-tests
```

Run the produced test executable from the build folder.

## Notes

- `CONFIG_PERF_ENABLE=1` is set for the core test target so perf logic is active during tests.
- The old ad-hoc C test files under `src/perf_lib/tests/` were removed after migration.
