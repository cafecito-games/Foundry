# Publishing the Foundry Android library to Maven Central

The Foundry Android library is published to Maven Central under group
`games.cafecito.foundry` with two runtime artifacts:

| Artifact | Coordinate | Contents |
| --- | --- | --- |
| Template (release) | `games.cafecito.foundry:foundry` | Runtime template AAR |
| Template (debug) | `games.cafecito.foundry:foundry-debug` | Debug template AAR |

Publishing is wired into `.github/workflows/release.yml` (the `build-android` job) and runs
automatically on a tagged, non-draft release. This document covers the one-time account setup, the
required secrets, and how to cut a release.

## One-time setup

### 1. Register and verify the namespace on the Central Portal

Maven Central is managed through the **Central Portal** (<https://central.sonatype.com>); the build
targets the Portal's OSSRH-compatibility endpoint via the `gradle-nexus.publish-plugin`.

1. Sign in to the Central Portal.
2. Add the namespace `games.cafecito` (the root of `games.cafecito.foundry`), which maps to the
   domain `cafecito.games`.
3. Verify domain ownership by adding the DNS `TXT` record the Portal provides to `cafecito.games`,
   then click **Verify**. Publishing returns `401/403` until the namespace is verified.

### 2. Generate a Central Portal user token

Portal → **Account → Generate User Token**. This returns a username/password pair (a token, not your
login). These become the `OSSRH_USERNAME` / `OSSRH_PASSWORD` secrets.

### 3. Create the PGP signing key

Central requires signed artifacts. On macOS: `brew install gnupg`, then:

```bash
gpg --gen-key                                   # name + a cafecito-games email (any address; not verified)
gpg --list-secret-keys --keyid-format=long      # note the fingerprint
gpg --keyserver keyserver.ubuntu.com --send-keys <FINGERPRINT>   # publish the public key
gpg --armor --export-secret-keys <FINGERPRINT>  # the private key -> SIGNING_KEY secret
```

The key's email is **not** verified by Central — it only signs the artifacts; the public key on the
keyserver is what validates the signature.

- `SIGNING_KEY_ID` = the **last 8 hex characters** of the fingerprint.
- `SIGNING_KEY` = the full armored private key block (`-----BEGIN PGP PRIVATE KEY BLOCK-----` … `-----END-----`).
- `SIGNING_PASSWORD` = the passphrase set at key generation.

### 4. Configure GitHub Actions secrets

Repo → **Settings → Secrets and variables → Actions**:

| Secret | Value |
| --- | --- |
| `OSSRH_USERNAME` | Portal user-token username |
| `OSSRH_PASSWORD` | Portal user-token password |
| `SONATYPE_STAGING_PROFILE_ID` | Leave unset; add only if a publish errors asking for it |
| `SIGNING_KEY_ID` | Last 8 chars of the key fingerprint |
| `SIGNING_KEY` | Full armored private key |
| `SIGNING_PASSWORD` | Key passphrase |

`OSSRH_GROUP_ID` is **not** a secret — the workflow's publish step hard-codes
`games.cafecito.foundry`.

## Versioning

The published version is derived from `version.py` and the release tag:

- The release workflow's `resolve` job derives a channel `status` from the tag and exports it as
  `FOUNDRY_VERSION_STATUS`.
- `getFoundryPublishVersion` (`app/config.gradle`) uses `FOUNDRY_VERSION_STATUS` when present
  (so the tag drives the version), falling back to `version.py` for local/untagged builds.
- **Stable** → clean `major.minor.patch` (e.g. `0.1.0`), published as a real Central release.
- **Any other channel** (`dev`, `alpha`, `beta`, `rc`) → `major.minor.patch-<status>-SNAPSHOT`,
  published to the snapshot repository.

`version.py` (`major.minor.patch`) must still match the tag's numeric version — the `resolve` job
sanity-checks this and fails the release on mismatch.

## Cutting a release

The publish step is gated `if: needs.resolve.outputs.draft == 'false'`, so it only runs on tagged
(non-draft) releases. A manual `workflow_dispatch` produces a draft and does **not** publish.

### Dry run first (recommended)

Push a prerelease tag to validate credentials + signing without promoting anything:

```bash
git tag v0.1.0-alpha.1
git push origin v0.1.0-alpha.1
```

This publishes a **snapshot** (`0.1.0-alpha1-SNAPSHOT`) to the snapshot repository. Confirm the run
succeeds and the artifacts appear before doing a real release.

### Stable release

```bash
git tag v0.1.0
git push origin v0.1.0
```

The `build-android` job builds all three artifacts, signs them, and runs
`closeAndReleaseSonatypeStagingRepository`. Artifacts are searchable on Central after ~10–30 minutes
of sync.

> Note: each artifact publishes from its own matrix leg, so a stable release
> opens/closes two separate staging repositories rather than one atomic bundle.
> This is functional (two independent artifacts) but not transactional.

### Local validation

To validate coordinates/POM without the network (no signing required):

```bash
cd platform/android/java
OSSRH_GROUP_ID=games.cafecito.foundry ./gradlew :lib:publishToMavenLocal
ls -R ~/.m2/repository/games/cafecito/foundry/
```

## Consuming the library

```gradle
repositories {
    mavenCentral()
    // Snapshots only:
    // maven { url "https://central.sonatype.com/repository/maven-snapshots/" }
}

dependencies {
    implementation "games.cafecito.foundry:foundry:0.1.0"
    // debugImplementation "games.cafecito.foundry:foundry-debug:0.1.0"
}
```
