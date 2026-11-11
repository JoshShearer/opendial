# Security policy

## Reporting

Please report vulnerabilities privately through the repository's GitHub
security advisories ("Report a vulnerability"). Do not open a public issue.
Include the version, distribution and desktop, and steps to reproduce. Expect
a first reply within 7 days.

## Scope

In scope:
- The service and its D-Bus API
- The HTTP API
- The udev, systemd and D-Bus files shipped in the packages
- Profile import
- The plugins in this repository

The threat model is in [docs/security-threat-model.md](docs/security-threat-model.md).

## Supported versions

Only the latest release gets security fixes before 1.0.
