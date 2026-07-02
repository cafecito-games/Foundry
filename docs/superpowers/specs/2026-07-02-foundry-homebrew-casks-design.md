# Foundry Homebrew Casks Design

## Context

Foundry already publishes release assets from `.github/workflows/release.yml`. The macOS release build produces a signed and notarized `Foundry.app`, packaged as `Foundry_v<version>_macos.universal.zip`, and the Linux editor is packaged as `Foundry_v<version>_linux.x86_64.zip`. The publish job uploads both to the GitHub Release.

Cafecito Games already owns the `cafecito-games/homebrew-tap` repository, which contains Homebrew casks under `Casks/`. The organization also has a `HOMEBREW_TAP_TOKEN` secret available for release automation.

## Goals

- Allow Homebrew users to install Foundry from the Cafecito Games tap.
- Keep stable users on stable releases by default.
- Provide prerelease channels for alpha, beta, and release-candidate users.
- Install `Foundry.app` on macOS.
- Link a `foundry` command into Homebrew's `bin` directory that runs `Foundry.app/Contents/MacOS/Foundry`.
- Install the Linux x86_64 editor binary through the same channel casks on Linux.
- Update the relevant tap cask automatically when a GitHub Release is published.

## Non-Goals

- Do not publish Foundry to the upstream `homebrew/cask` repository in this change.
- Do not create version-pinned archival casks such as `foundry@0.1.0-alpha.1`.
- Do not update Homebrew casks for draft releases.
- Do not change Foundry's build output names unless release packaging requires it.

## User-Facing Install Commands

Stable users install the default cask:

```sh
brew install --cask cafecito-games/tap/foundry
```

Prerelease users install channel casks:

```sh
brew install --cask cafecito-games/tap/foundry@alpha
brew install --cask cafecito-games/tap/foundry@beta
brew install --cask cafecito-games/tap/foundry@rc
```

After installation, all channels expose:

```sh
foundry --version
```

## Channel Model

The tap will maintain four cask files:

- `Casks/foundry.rb`: latest stable release only.
- `Casks/foundry@alpha.rb`: latest alpha release only.
- `Casks/foundry@beta.rb`: latest beta release only.
- `Casks/foundry@rc.rb`: latest release-candidate release only.

Each published release updates exactly one cask:

- `status == stable` updates `foundry`.
- `status` beginning with `alpha` updates `foundry@alpha`.
- `status` beginning with `beta` updates `foundry@beta`.
- `status` beginning with `rc` updates `foundry@rc`.

All casks install the same application bundle name and binary link, so they must declare conflicts with the other Foundry channel casks.

## Cask Shape

Each cask should support macOS and Linux x86_64 with platform-specific URLs and checksums:

```ruby
cask "foundry" do
  version "0.1.0"

  on_macos do
    sha256 "<computed macOS sha256>"
    url "https://github.com/cafecito-games/Foundry/releases/download/v#{version}/Foundry_v#{version}_macos.universal.zip"

    app "Foundry.app"
    binary "#{appdir}/Foundry.app/Contents/MacOS/Foundry", target: "foundry"
  end

  on_linux do
    sha256 "<computed Linux sha256>"
    url "https://github.com/cafecito-games/Foundry/releases/download/v#{version}/Foundry_v#{version}_linux.x86_64.zip"

    binary "foundry.linuxbsd.editor.x86_64", target: "foundry"
  end
end
```

The cask should include standard metadata:

```ruby
name "Foundry"
desc "Game engine and editor with Foundry Script"
homepage "https://github.com/cafecito-games/Foundry"
```

## Release Workflow

The Foundry release workflow should add a tap update step after `Publish GitHub Release`, guarded so it only runs when `needs.resolve.outputs.draft == 'false'`.

The step should:

1. Determine the target cask token from the resolved release status.
2. Compute SHA-256 values for `dist/Foundry_v<version>_macos.universal.zip` and `dist/Foundry_v<version>_linux.x86_64.zip`.
3. Generate the target cask file from a small template or script.
4. Clone `cafecito-games/homebrew-tap` using `HOMEBREW_TAP_TOKEN`.
5. Write the updated cask to `Casks/<token>.rb`.
6. Run `ruby -c Casks/<token>.rb` against the generated cask, and run `brew audit --cask <token>` when Homebrew is available in the publishing environment.
7. Commit as `Brew cask update for <token> version v<version>`.
8. Push to the tap repository.

If the tap update fails after the GitHub Release is already published, the release job should fail visibly so the tap can be fixed or rerun.

## Error Handling

The tap publication script should fail fast if:

- The resolved release status does not map to a known channel.
- The macOS release asset is missing from `dist/`.
- The Linux x86_64 release asset is missing from `dist/`.
- The SHA-256 cannot be computed.
- The generated cask does not pass `ruby -c`.
- The tap checkout or push fails.

The script should be deterministic: given a cask token, version, platform SHA-256 values, and repository URL, it should emit the same cask file every time.

## Testing

Repository tests should cover the cask-generation script without requiring a real tap push:

- Stable status maps to `foundry`.
- Alpha, beta, and rc statuses map to their channel casks.
- Unknown statuses fail.
- Generated Ruby contains the expected version, platform URLs, platform SHA-256 values, app stanza, binary stanzas, and conflicts.

Release workflow validation should include script unit tests and `ruby -c` on generated fixture casks. A full end-to-end Homebrew install can be verified after the first public release asset exists.

## Future Work

Linux support is initially x86_64 only because the release workflow currently publishes only `Foundry_v<version>_linux.x86_64.zip`. Linux arm64 can be added later by extending the release build matrix and rendering the appropriate Homebrew architecture branch.
