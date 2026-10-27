# OpenAL float-loader regression tests

These tests exercise the checked-in loader fragment using OpenAL Soft 1.25.2's
actual `f32` and GSL headers. They do not open an audio device.

```powershell
cmake -S tests/openal_loader -B build/openal-loader-tests -G Ninja `
    -DOPENAL_SOFT_SOURCE_DIR=C:/path/to/openal-soft-1.25.2
cmake --build build/openal-loader-tests
ctest --test-dir build/openal-loader-tests --output-on-failure
```

The C++ test includes the same fragment applied by the public dependency build,
not a duplicate implementation. It compares exact float bits for mono, stereo and
generic strides, every channel, source offsets, zero/small/tail counts and guarded
destinations. Coverage includes signed zero, infinities, quiet NaN payloads,
subnormals and finite extremes, and verifies that input is unchanged.

On Windows, the Python/PowerShell test checks application and idempotent reapply,
version/source/line-ending mismatch rejection without writes, portable fragment
line endings, corrupted-fragment rejection, and the build's existing fresh-folder
guard. It accepts either the known pristine or patched pinned source as input.
The source tree supplied to the tests is read only; temporary cases live inside
the test build directory.

This is focused loader/build-script coverage, not full-library integration,
physical playback or a performance claim. Build the full dependency and run the
ordinary OpenAL tests separately against that output.
