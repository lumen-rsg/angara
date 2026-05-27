# Security Policy

## Reporting a Vulnerability

We take security issues seriously. If you discover a vulnerability in Angara, please report it responsibly.

**Do not open a public GitHub issue.** Instead, use one of the following:

- **GitHub Security Advisories**: [Report a vulnerability](https://github.com/lumen-rsg/angara/security/advisories/new) (preferred)
- **Email**: Send details to the maintainers privately

Please include as much of the following as possible:

- A description of the vulnerability and its impact
- Steps to reproduce or a proof-of-concept
- Affected versions
- Any suggested fixes

We aim to acknowledge reports within **72 hours** and provide a substantive response within **7 days**.

## Disclosure Policy

- Vulnerabilities are disclosed after a fix is available and released.
- We coordinate with reporters on disclosure timing.
- Credit is given to reporters unless they request otherwise.

## Scope

The following are considered in scope for security reports:

| Component | Description |
|-----------|-------------|
| Compiler (`angc/`) | Miscompilations that silently produce incorrect or unsafe code |
| Crypto modules | `crypto/hash`, `crypto/jwt`, `crypto/uuid` |
| Network modules | `net/http`, `net/websocket`, `net/amqp`, `net/mqtt` |
| Runtime | Memory safety issues in the language runtime or FFI layer |
| Build system | Supply-chain concerns in the Makefile or CI pipeline |

### Out of Scope

- Vulnerabilities in third-party dependencies (report upstream)
- Denial of service via intentionally pathological input to the compiler
- Issues in unreleased or experimental features not on a tagged version

## Supported Versions

| Version | Supported |
|---------|-----------|
| 3.x (stable) | Yes |
| < 3.0 | No |

Security fixes are applied to the current stable branch and included in the next release.

## Security-Relevant Dependencies

Angara links against several security-sensitive libraries. Vulnerabilities in these are tracked upstream:

- **OpenSSL** — used by `net/websocket` for TLS
- **libcurl** — used by `net/http`
- **SQLite** — used by `sqlite` module

We update vendored and linked dependencies as patches become available.
