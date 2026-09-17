# PLANK dependency maintenance

PLANK consumes `plank/client` and `plank/host`, not this inherited default
branch. The Client branch has no recursive dependencies; the Host branch is
header-only. Neither maintained branch currently has a supported dependency
manifest or an active GitHub Actions workflow to schedule with Dependabot.
Do not enable updates for inherited ENet/nanors inputs on the default branch.

Repository security alerts and security-update PRs are enabled, but that is not
evidence of vulnerability coverage for vendored C code. Review source changes
on the two maintained branches, then qualify their exact gitlinks in the
consuming Host/Client repositories. Their `.gitmodules` must explicitly name
`plank/host` and `plank/client`, respectively.

See the [PLANK dependency policy](https://github.com/instinctual/plank/blob/main/docs/development/dependency-maintenance.md)
for integration gates. No automatic merges or inherited upstream CI jobs.
