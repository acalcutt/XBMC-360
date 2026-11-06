# Releases

360 Riffle's version is `APP_VERSION` in [xbmc360/CompileInfo.h](../xbmc360/CompileInfo.h), and
[CHANGELOG.md](../CHANGELOG.md) says what each version brings. GitLab CI ([.gitlab-ci.yml](../.gitlab-ci.yml)) builds
every merge request and push to main, and makes a release of each new version.

## What a release has

- `360Riffle-v<version>.zip`: the bundle as a `360riffle` folder. Unzip it at the top of a USB drive, plug that into
  the console, and choose **System info > Update from USB**. For a new install with Peer Pressure, the folder goes on
  the stick as `BadUpdatePayload\SoftmodExtras\360riffle` instead ([usb-stick.md](usb-stick.md)).
- `SHA256SUMS`, to check the zip against.
- The version's notes, from its section of CHANGELOG.md.

Each build of a merge request or of main also keeps its zip for 7 days, as the build job's artifact:
`360Riffle-v<version>-ci<pipeline>-<commit>.zip`.

MilkDrop's shaders and presets (`data`, `visualisations`) aren't in this repository. The zip has them when the runner
has a stock XBMC-360 bundle (`XBMC360_STOCK`, below). Without them it still updates an installed 360 Riffle, which keeps
its own, but a new install has no visualisations.

## Making a release

1. As changes are made, add them under `## master` in CHANGELOG.md.
2. **Build > Pipelines > Run pipeline** on main, with **BUMP_VERSION** set: `patch`, `minor` or `major`; or for a
   pre-release `prepatch`, `preminor` or `premajor` (`0.2.0-rc.1`, named by **BUMP_PREID**) and then `prerelease` for
   the next one (`0.2.0-rc.2`). It sets the new APP_VERSION, makes the changelog's master section that version's,
   pushes the branch `release-v<version>` and opens a merge request.
3. Check the changelog in the merge request and merge it. The pipeline on main builds it and creates the release
   `v<version>`.

Any push to main whose APP_VERSION has no release yet makes one, so the first pipeline on main releases 0.1.0.

## Setting it up

- **The runner**: the build needs a Windows runner with Visual Studio 2010 and the Xbox 360 XDK, tagged `windows` and
  `xdk`: gitlab-runner-install's `Install-WindowsRunner.ps1`, then `Install-Xbox360Xdk.ps1`, registered as a runner of
  its own so that the tags are only its. The other jobs run on any runner tagged `windows`, with Python 3 for the bump.
- **CI_PUSH_TOKEN**, for the bump: in **Settings > Access tokens**, a project access token with the Maintainer role and
  the `api` and `write_repository` scopes. Add it in **Settings > CI/CD > Variables** as `CI_PUSH_TOKEN`, Protected
  and Masked.
- **XBMC360_STOCK**, optional: copy a stock XBMC-360 bundle (the folder with `data` and `visualisations` in it, as the
  stick has at `BadUpdatePayload\SoftmodExtras\xbmc360`) to the XDK runner, such as to `C:\GitLab-Runner\xbmc360-stock`,
  and add that path as the variable `XBMC360_STOCK`.
- The first build compiles every library, about half an hour. Build output stays between pipelines, so later builds
  compile only what changed; to build everything again, run a pipeline with `GIT_CLEAN_FLAGS` set to `-ffdx`.
