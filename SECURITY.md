# Security Policy

## Reporting a Vulnerability

If you discover a potential security vulnerability in NightBuild, please report it privately through GitHub's private vulnerability reporting feature, if enabled for this repository.

If private vulnerability reporting is unavailable, please contact the repository maintainer privately through an appropriate GitHub communication channel to arrange a confidential report.

**Please do not publicly disclose exploitable vulnerability details before a fix or mitigation is available.**

When possible, include the following information in your report:

- A description of the vulnerability.
- The affected NightBuild version or commit.
- Steps to reproduce the issue.
- The potential security impact.
- Any relevant logs, error messages, or proof-of-concept examples that help demonstrate the issue. Please avoid including secrets or unrelated personal information.

## Scope

Security reports may include vulnerabilities involving:

- Unsafe command execution.
- Build database or state-file corruption that affects security or build integrity.
- Path traversal or unsafe filesystem operations.
- Improper handling of untrusted build configuration.
- Incorrect dependency tracking or incremental-build behavior that could compromise build integrity.
- Other issues that could compromise the security of the host system.

This list is not exhaustive. If you're unsure whether an issue has security implications, please report it privately.

## Response

Reports will be reviewed as time permits.

Please wait for confirmation before assuming a report has been received or that a fix is underway. Response and remediation times may vary depending on the issue's severity, complexity, and the maintainer's availability.

Please avoid publicly sharing additional vulnerability details while a report is being investigated.

## Disclosure

Coordinated disclosure helps protect NightBuild users and downstream consumers.

When a vulnerability is confirmed, the maintainer will work toward an appropriate fix or mitigation and determine when public disclosure is appropriate. Not every report will necessarily result in a code change; some reports may require further investigation or may not represent a security vulnerability.