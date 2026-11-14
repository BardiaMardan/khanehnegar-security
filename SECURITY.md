# Security policy

khanehnegar is a security system, so flaws in it matter more than in most hobby projects.

## Reporting a vulnerability

Please **do not open a public issue** for a vulnerability.

Use GitHub's private reporting instead: **Security → Report a vulnerability** on this repository.
Include:

- which part is affected (hub firmware, a node, SecMesh, or the dashboard)
- how to reproduce it, and what hardware you used
- what an attacker gains

You should get a reply within 7 days. Once a fix is ready, you will be credited in the
changelog unless you would rather not be.

## Supported versions

Only the latest commit on `main` gets fixes.

## Known, documented limitations

These are deliberate trade-offs, not undisclosed bugs. They are explained in
[docs/security.md](docs/security.md):

- ESP8266 nodes talk to the hub in plaintext (counter-protected and MAC-filtered, but readable off the air).
- The ESP-NOW keys are shared by every board on a site.
- The hub talks to the dashboard over plain HTTP unless you put the dashboard behind HTTPS.
- There is no battery backup or cellular fallback in the reference build.

A report that shows one of these is *worse* than documented is very welcome.
