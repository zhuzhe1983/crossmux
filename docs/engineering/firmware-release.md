# Firmware Release Architecture

CrossMux has two release channels, `stable` and `nightly`, managed by one
channel-aware pipeline. Hardware identity is not a release channel. Both channels
contain the same nine targets: the shared X3/X4 image and eight independent S3
images. X4 Classic and LilyGo remain development-only.

## Canonical targets

[`scripts/nightly_targets.py`](../../scripts/nightly_targets.py) is the release
source of truth despite its compatibility filename. Each target defines its
runtime models, artifact slug, embedded board tag, per-channel PlatformIO
environments, chip, model display information, partition/Flash parameters, USB
filters, install strategy, optional factory sources, and supported channels. The workflow,
packager, index builder, and tests import this table rather than copy it.

The X3/X4 target accepts `xteink_x3` and `xteink_x4` and produces one ESP32-C3
image. Stable uses `gh_release`; Nightly uses `gh_release_rc`. Sticky Stable uses
`sticky-gh_release`. Sticky, X4 Pro,
Paper Mono, EEGO A4, Murphy M4, Waveshare ePaper 3.97, Metalio E-Ink 4, and MindReset Read Pico each produce their own
ESP32-S3 image per channel. Each image is aliased by the compatibility `global` and
`zh-CN` pointers.

Read Pico uses `readpico-gh_release` and `readpico_nightly`, with artifact slug and board tag `readpico`.
The model list accepts both `readpico` and the SDK runtime name `read_pico`.
Its full-install package uses the existing 16 MB S3 partition
profile. The Web flasher displays it as **MindReset Read Pico**. First installation replaces the factory partition layout
and requires a complete Flash backup. See [Read Pico](read-pico.md) for hardware
evidence and the outstanding physical acceptance checks.

### Firmware-write progress rendering

OTA and SD firmware updates draw an initial progress frame, refresh after at
least another 10 percentage points (jumping directly to the latest value), and
finish the 100% frame before continuing. The progress callback updates byte
snapshots under `RenderLock`, releases the lock, then calls
`requestUpdateAndWait()`. Flash erase/write and the subsequent boot-partition
switch must wait for that frame: overlapping Flash operations can suspend
Read Pico's display feeder tasks during a panel refresh. Render methods always
submit a complete frame; they do not throttle after clearing the framebuffer.

OTA rendering reads only the locked snapshots, not the updater's live counters.
Progress bars clamp completed bytes to the reported total; an unknown/zero
total displays 0%. Download progress reaching 100% does not mean installation
succeeded: the completion page is shown only after firmware verification and
boot-partition selection succeed. No additional heap buffer is allocated.

Run `python3 scripts/tests/test_firmware_update_refresh.py` for the production
callback, Flash-operation ordering, failure-path and theme-render regressions,
alongside `test_font_preload_refresh.py` and `test_download_memory_lifecycle.py`.
Device acceptance must record the installed firmware hash and serial log,
repeat Read Pico OTA through the completion page and expected reboot, and check
interrupted-download recovery plus the SD update path. Builds and host tests do
not establish physical display or reboot acceptance.

## Publishing

Each target job builds once and packages the same four roles in both channels:
bootloader, partitions, boot_app0 and firmware. Packaging checks the ESP image
chip ID, required board tag, complete embedded version, partition layout,
app-slot size, and SHA-256 before emitting two compatibility manifests.
`crossmux-<version>-<deviceSlug>.zip` contains those files and their checksum list.
The regional index includes the ZIP name, size, SHA-256 and immutable download URL.

Before either region publishes, one shared validation job generates both regional
indexes and resolves every manifest, binary and ZIP against the downloaded build
artifacts. It uses the same verifier as published assets, including HTTPS/source
restrictions and checksum checks. A missing local artifact fails validation;
the verifier never falls back to a remote download in local mode.

The global and China publish jobs run independently. Each writes in this order:

1. immutable binaries and checksum files;
2. immutable target manifests;
3. rolling regional indexes.

Every target selected for a channel must build successfully before either region
publishes. Nightly also requires its previous rolling index because that index
protects the immediately preceding build during cleanup. Once both regions
publish, CI resolves every manifest and verifies each distinct asset's size and
SHA-256. Cleanup then runs for Nightly only; Stable builds are retained.

The global index is the `release-index.json` asset of the rolling `stable` or
`nightly` GitHub Release. Binaries and compatibility manifests live in an
immutable `<channel>-build-<sha>-<run>-<attempt>` GitHub Release. China indexes
are `/firmware/releases/<channel>/index.json`; target assets live under
`/firmware/builds/<channel>-build-<sha>-<run>-<attempt>/<target>/` in COS. Region chooses the storage
provider; both variant manifests reference the same neutral binary names and
differing hashes fail publication. Human-facing rolling and versioned Releases
contain only the per-target ZIPs and the total index. Raw binaries and manifests
remain in dedicated immutable build Releases for OTA and Web installation.
New versioned Releases do not duplicate legacy `firmware.bin`, `firmware-cn.bin`,
`bootloader.bin` or `partitions.bin`. Historical version Releases and their links
are retained unchanged.

At steady state, GitHub and COS retain the current build and the build or builds
referenced by the previous index. Normally this means two immutable builds; the
rollback window depends on release frequency. Failed builds do not publish or
clean up anything. The first complete run can temporarily retain more than two
build names when the preceding index contains target-level fallbacks; the next
complete run converges to exactly the current and previous build.

Cleanup validates a nonempty historical target map and every target's identity,
compatibility variants, and regional manifest URL. Historical targets need not
match today's canonical target table: adding or retiring a device must still
protect every build referenced by the previous index. New published indexes
continue to require the complete canonical target set. A cleanup failure after
verified publication leaves the new firmware available and retains extra old
builds until a subsequent successful cleanup.

COS publishing runs only on the H2O self-hosted runner and does not fall back to
a GitHub-hosted runner. It uses a version-pinned, SHA-256-verified COSCLI binary
from the runner's temporary directory. The workflow verifies COSCLI before
writing any immutable COS object and passes credentials directly to each COS
command instead of persisting a CLI config file. Required repository secrets
are `COS_SECRET_ID`, `COS_SECRET_KEY`, `COS_BUCKET`, and `COS_REGION`;
`COS_SESSION_TOKEN` is optional. Gitee is not a firmware release destination.
The COS identity also needs `cos:GetBucket` for the current-object listing,
`cos:GetBucketVersioning` to detect the bucket state,
`cos:GetBucketObjectVersions` for versioned cleanup, `cos:DeleteObject`, and
`cos:DeleteMultipleObjects`. Cleanup detects versioned buckets and removes every
version of an obsolete build prefix rather than leaving hidden historical objects.

### Branch rehearsal

Manual dispatch accepts `dry_run` (boolean, default `false`). Enable it to build
all nine Nightly targets on a branch and run the same packaging and two-region
index validation without any GitHub Release or COS writes or cleanup. Build notes
record the actual branch. Rehearsals have a separate concurrency group, so they
do not serialize with production publishing. Firmware packages and both indexes
remain available as Actions artifacts for 14 days.

```bash
gh workflow run nightly.yml --ref codex/unified-build-release \
  -f dry_run=true \
  -f notes_en=$'Build and package validation\nNo production firmware update' \
  -f notes_zh=$'构建及打包验证\n不更新线上固件'
```

To repeat the index check locally, use `verify_nightly_release.py --channel nightly
--expected-sha <full-sha> --index-url <regional-published-index-url> --index-file
<local-index.json> --artifacts <downloaded-artifacts-directory>`. `--index-url`
still defines the trusted regional URL policy; `--index-file` and `--artifacts`
must be supplied together. Normal manual dispatch and version-tag publishing
retain their existing behavior when `dry_run` is false or absent.

## Index contract and failure behavior

The schema-v1 index contains `channel`, `updatedAt`, `buildId`, and a `targets`
map, plus optional regional `releaseNotes`. Each target repeats its identity,
public display/install metadata, optional factory sources and ZIP descriptor,
and contains `global` and `zh-CN` pointers with version,
CrossMux SHA, SDK SHA, publish time, and immutable manifest URL. Stable requires
both release-note locales.

Every target advances together only when both compatibility manifests are valid
and have the same CrossMux revision, SDK revision, version, and assets. A
missing or malformed manifest prevents the whole channel from publishing.
Build objects are never overwritten; cleanup runs only after the new rolling
indexes and their assets pass verification.

## Consumers and safety

The Web flasher and download dialog share the regional catalog. The home page
loads both channels and prefetches install manifests and factory metadata with
at most four concurrent requests. Successful metadata is cached for five minutes
with in-flight request sharing; failures and missing manifests can be retried.
Binary downloads begin only for an explicit flash/download.

The Web validates generic targets and dispatches existing install strategies,
rather than maintaining another platform list. Platforms reusing a supported
chip, partition and install strategy need only firmware-side metadata; a new
hardware write strategy still requires implementation and validation. Factory
options describe the fixed Xteink update provider or an immutable pinned image.
Unknown strategies, wrong-board images and invalid URLs remain rejected.

Device OTA keeps the GitHub-like response with one
`firmware.bin` asset and selects by exact model, variant, and channel. A target
that does not support a requested channel in a historical index returns
`ota_status: unsupported_channel` rather than falling back to another channel or board.

Deploy the Web compatibility layer before enabling the new CI indexes. Verify
both regional Nightly indexes, ZIPs and direct assets before switching consumers;
Stable adopts the format on the next version tag. Legacy v1 manifests and API
responses remain readable during this transition.

Official packages must contain the board tag. The OTA stream aborts a tagged
image for another board before selecting the new partition. Untagged historical
or third-party images remain compatible, so chip and board checks in the
official packaging path are mandatory.

CI, indexes, and checksum checks do not replace real-device acceptance. Before
making a Nightly pipeline production-critical, test OTA and reboot on every S3
target, one X3/X4, both content profiles, and one wrong-board negative case.

## Personal CrossStore three-device prereleases

The separate workflow
[`fork-store-release.yml`](../../.github/workflows/fork-store-release.yml), named
**CrossStore Three-Device Release**, targets `zhuzhe1983/crossmux`. It builds on
GitHub-hosted Ubuntu runners and needs no COS or self-hosted runner secrets.
It does not modify the official target table, supported Stable devices,
`stable`/`nightly` tags, rolling OTA indexes, or retention policy above.

| Device | PlatformIO environment | Embedded board tag | Artifact prefix |
| --- | --- | --- | --- |
| M5Stack PaperMono | `papermono_store` | `papermono` | `papermono-` |
| M5Stack PaperS3 | `papers3_store` | `papers3` | `papers3-` |
| Waveshare ESP32-S3-ePaper-3.97 | `waveshare_epaper_397_store` | `waveshare_epaper_397` | `waveshare-epaper-397-` |

All three are ESP32-S3 / 16 MiB Store builds with the current `partitions.csv`
layout. PaperS3's SDK runtime model is `m5paper_s3`; it has its own binary,
not the PaperMono image. This matrix is independent of the official Nightly
matrix because its environments include `CROSSMUX_STORE=1` and the optional
services selected by each board's Store profile. PaperS3's configured upload
mode is QIO; Mono and Waveshare use DIO. The manifest records that difference;
the packager preserves each built bootloader header and does not rewrite it.

Run the workflow on the intended committed branch or tag. GitHub dispatch
resolves that ref to `GITHUB_SHA`; every checkout explicitly uses that immutable
SHA. `publish_release=false` builds and retains Actions artifacts for 14 days
without creating a GitHub Release. The default `true` publishes only after all
three matrix jobs succeed. The workflow must first be available on the fork's
default branch for GitHub's normal manual-dispatch entry point; choosing another
ref must select a revision containing this workflow and its scripts.
See [GitHub's dispatch contract](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#workflow_dispatch).

The first release does not require merging this work into the fork's `main`.
After publishing the complete feature-branch commit and its SDK dependency,
push one new lightweight tag named `crossstore-build-<full-40-character-sha>`
at that exact commit. The tag's `push` event runs the workflow from the tagged
commit, including when it has never existed on the default branch. Ordinary
branch pushes do not trigger this workflow. For example, from the prepared
feature branch (commands below publish the build request):

```bash
release_sha=$(git rev-parse HEAD)
python3 scripts/fork_store_release.py preflight --sha "$release_sha"
git tag "crossstore-build-$release_sha" "$release_sha"
git push origin "refs/tags/crossstore-build-$release_sha"
```

The prepare job requires an exact SHA match and a new, non-forced, non-deleted
tag event. Do not move or force-push a build tag; use Actions' **Re-run all jobs**
for the same commit, or create a new source-bound tag for a new commit. Artifact
names include the run attempt, so rerunning only failed jobs cannot combine
earlier successful build assets with the new attempt. A build-tag run
always requests publication, but still requires all three successful builds
and both local and downloaded-asset verification. Output release tags use
`crossstore-...`, which does not match the `crossstore-build-*` input pattern.
This also keeps them outside the official workflow's stable version tags.
The first tag push must use the maintainer's normal authenticated Git push;
events made with a workflow's `GITHUB_TOKEN` normally do not start another
workflow. See [GitHub's push-event contract](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#push)
and [workflow-trigger token rules](https://docs.github.com/en/actions/how-tos/writing-workflows/choosing-when-your-workflow-runs/triggering-a-workflow).

### Reproducible source readiness

The parent repository and SDK must be committed and publicly fetchable by the
workflow. Publishing the parent alone does not publish uncommitted submodule
changes. Commit/publish the required SDK changes to an accessible repository,
set the parent's SDK gitlink to that exact commit, and commit the parent changes.
If the SDK commit lives in a fork, `.gitmodules` must point to that reachable
repository. A local successful build is insufficient evidence that a fresh
Actions checkout can build it.

For the SDK, publish the required changes on a feature branch of an accessible
fork first (for example `zhuzhe1983/freeink-sdk`), update `.gitmodules` to that
repository and the parent gitlink to its exact commit, then publish the parent
feature branch. Neither repository's `main` needs to change. Test a fresh
recursive checkout of that parent revision before creating the build tag;
verify SDK `HEAD` equals `git ls-tree HEAD freeink-sdk`. An existing local SDK
remote or base commit does not prove uncommitted SDK changes are available to
Actions. A public SDK fork avoids requiring extra private-repository tokens.

Fresh-checkout inputs include the vendored `lib/CrossStoreLua` and
`lib/RadioDecoder` sources and licenses, the CrossStore runtime/services/HAL,
`assets/app-icons/appstore/icon.csi`, and the six preprocessed semantic-icon
`icons.h` files used by `SemanticAppIcons.cpp`. These are repository inputs,
not files retrieved from a developer's `~/Workspace/CrossStore` checkout.
PIO hooks regenerate the ignored Store icon, woodfish PCM, HTML, user-guide and
i18n headers. Those hooks need only the Python standard library; Pillow is
needed when authoring new icon assets, not for this release build. Tox and cmp
are downloaded by `scripts/configure_chat.py` from fixed revisions with checked
SHA-256 values. A previously populated `.cache/chat-deps` is not required.

`scripts/fork_store_release.py preflight --sha <full-commit-sha>` rejects a
different checkout SHA, dirty/untracked source in either repository, a gitlink
that differs from the SDK checkout, `platformio.local.ini`, a missing Store flag,
a missing build mode, explicit version override or unsafe generated version,
and a different chip/flash/partition
configuration. CI starts with fresh build outputs and isolated PlatformIO
toolchains; no cached firmware is reused. Build dependencies follow the pinned
platform and existing checksum-verified chat download scripts. The firmware
version uses the same `custom_build_mode`, Git branch/HEAD and `version_for()`
function as the build middleware, and must appear in the image. Store profiles
inherit development mode; a detached CI checkout produces
`<base>-dev-detached-<7sha>`. Slashes in local branch names are allowed only in
version metadata; asset filenames remain fixed per device. Board identity is
checked separately from the common version string; Arduino's `esp_app_desc.version` is a framework build ID and is not used
as the CrossMux version.

### Artifacts and publication gates

For each target the packager emits seven files: `*-firmware.bin`,
`*-bootloader.bin`, `*-partitions.bin`, `*-boot_app0.bin`,
`*-full-install.bin`, `*-install-manifest.json`, and `*-SHA256SUMS`.
The manifest records exact CrossMux/SDK SHAs, board/model/environment, configured
version, flash profile, and each image's role, offset, length and SHA-256.
The combined `crossstore-release.json` is an inventory of these three manifests;
it is deliberately not an official OTA index or an ESP Web Tools manifest.

`firmware.bin` is the application image for the corresponding device's existing
compatible CrossMux installation. Full installation uses segments at `0x0`,
`0x8000`, `0xE000`, and `0x10000`, or the merged `full-install.bin` at `0x0`.
The merged image pads gaps with `0xFF`, including the NVS region, so it replaces
bootloader/partition/NVS/OTA state. Back up Flash first and use the application
image for routine updates. The images do not include TF-card contents or Lua
application data. Partition changes require a separate migration decision.

The packager reuses the official chip/board-tag and CSV layout checks, then
also verifies the emitted partition binary (including its MD5, types, offsets,
overlaps and bounds), S3 bootloader, app-slot limit, and byte-for-byte merged
segments. Aggregation requires exactly all three targets, the dispatched
CrossMux SHA and one SDK SHA, correct sizes/hashes, and an exact filename
allowlist. Extra files, symlinks, stale output directories and missing targets
fail the job rather than being uploaded.

The publish job alone has `contents: write`. It creates a new lightweight tag
`crossstore-<full-sha>-<run-id>-<attempt>` at the built SHA; an existing tag causes
failure. It uploads a draft prerelease, downloads and validates the uploaded
bytes, rechecks the remote tag's SHA, then makes the prerelease public without
marking it Latest. A failed upload/verification may leave a draft/tag for manual
inspection; it never overwrites an older release or changes an OTA index.
GitHub artifact digest mismatches are only warnings, which is why the explicit
post-download validation is mandatory here. See the
[artifact validation documentation](https://docs.github.com/en/actions/tutorials/store-and-share-data#validating-artifacts)
and [GitHub CLI release options](https://cli.github.com/manual/gh_release_create).
These are checksum and source-provenance checks, not a detached-signature or
secure-boot signing service; the workflow does not use a firmware signing key.

Run `python3 -m unittest discover -s scripts/tests -p test_fork_store_release.py`
for the target mapping, source-bound trigger checks, fresh-checkout build
inputs, clean-source rejection, cross-board/chip rejection,
partition corruption, slot overflow, merged bytes, hash/manifest/source-SHA
consistency and workflow publication gates. These host checks do not establish
cloud build success or hardware acceptance; record each separately.
