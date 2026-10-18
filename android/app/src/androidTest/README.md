# Android runtime contract tests

The test APK uses framework Instrumentation and a test-only DocumentsProvider;
there are no added AndroidX/JUnit dependencies or production configuration seams.
The runner class is `com.monsterengine.viewer.AndroidContractInstrumentation`.
Supply `defaultConfig.testInstrumentationRunner` through the local verification
init script when building `:app:assembleDebugAndroidTest`.

Install the matching production and test APKs without uninstalling existing data.
Run only after the foreground build worker retires: instrumentation starts the
target process, and the native validator uses the actual aggregate budget.

    adb shell am instrument -w -r com.monsterengine.viewer.debug.test/com.monsterengine.viewer.AndroidContractInstrumentation

The complete case report is written to the target app's private
`files/android-contract-tests.json`; instrumentation also emits per-case JSON.
No measured line-coverage claim is made.

The provider authority is `com.monsterengine.viewer.debug.test.documents`, and
its picker root is **Monster Verification**. `small.ply` contains one valid ASCII
Gaussian, `slow.ply` streams 160000 valid records through a pipe with 200ms pacing
per 2048 records, and `malformed.ply` contains a deliberately invalid document.
The same provider is usable through the actual system SAF picker for manual
active-copy cancellation. Picker cancellation before selection is a separate
case. Direct instrumentation import cases obtain a read URI grant from the
test-only provider's owning process. Its custom setup call accepts only the
fixed production debug package's actual UID and the three generated document
IDs; application import/cancellation/cleanup code is unchanged. No shell
permission adoption is needed.

Additional complete package fixtures may be placed in the **test APK's** private
`files/fixtures/` directory. Names must contain only letters, digits, `_`, `-`,
and `.`. They appear in the picker with their ordinary filenames and resolve to
document IDs prefixed by `fixture-`. This test-only fixture mechanism does not
alter production import limits or package validation.

Each JNI case creates and removes only its own unique source/job/output names.
The checkpoint compatibility case completes a real one-record native build,
then changes only the isolated request's supportSigma from 3 to 4 and uses a new
absent output name. It requires explicit identity rejection and unchanged source,
journal and original output hashes, with no new output publication.
Import cases publish a real native-validated sentinel and compare the complete
scene-store directory inventory plus file hashes before/after rejection, then
delete only the sentinel. Resource rejection uses the existing production
`SceneStorageLimits(maxSourceBytes = 65536)` constructor; it copies nonzero bytes
before rejecting and never fills device storage. An existing published scene or
retained user job/input is never deleted by this suite.
