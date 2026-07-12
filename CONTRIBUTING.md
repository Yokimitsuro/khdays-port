# Contributing to khdays-port

Thank you for helping with the project.

## Keep the port and decompilation separate

Submit work to the repository that matches its purpose:

- Byte-matching Nintendo DS decompilation work belongs in `khdays-decomp`.
- Native platform code, compatibility layers, tools, adapted game code, and PC-specific behavior belong in `khdays-port`.

A port pull request does not need to produce matching Nintendo DS assembly, but it must preserve understood behavior and explain intentional differences.

## Prohibited content

Do not commit or attach:

- ROMs or links intended to obtain copyrighted ROMs;
- extracted game assets or data packs;
- original game binaries or assembly dumps;
- proprietary NitroSDK, CodeWarrior, or Nintendo development files;
- secrets, access tokens, or private keys;
- code copied from a project whose license is incompatible with this repository.

Test data should be synthetic, freely licensed, or generated locally from a user-provided game copy.

## Reverse-engineering provenance

When porting a game function or subsystem, include enough information for another contributor to review it:

- source module, overlay, symbol, or address;
- source `khdays-decomp` commit;
- known inputs, outputs, global state, and side effects;
- assumptions that are not yet verified;
- tests or comparison method used;
- intentional behavioral changes.

Avoid presenting guessed names, types, structures, or behavior as confirmed facts.

## AI-assisted contributions

AI-assisted work is allowed only when the contributor has reviewed, understood, and tested the submitted code. The pull request author remains responsible for correctness, provenance, licensing, and maintainability.

Do not submit large generated rewrites that cannot be explained or reviewed.

## Coding guidelines

- Prefer small, reviewable pull requests.
- Keep platform-neutral game code separate from platform implementations.
- Avoid direct hardware addresses in portable code.
- Avoid adding abstractions before there is a concrete use for them.
- Treat warnings as defects in newly added code.
- Add error messages that explain how the user can fix the problem.
- Never silently download game data or proprietary tools.

## Commit messages

Use concise conventional-style prefixes where practical:

```text
build: add initial CMake project
docs: define legal and contribution policy
tools: add ROM hash verification
platform: add window bootstrap
renderer: decode first texture format
game: port mission state helper
fix: reject unsupported ROM revisions
```

## Pull request checklist

- [ ] No copyrighted or proprietary content is included.
- [ ] Third-party code and licenses are documented.
- [ ] The change builds on each platform claimed by the pull request.
- [ ] New behavior has a test or a documented manual verification procedure.
- [ ] Reverse-engineering claims distinguish verified facts from assumptions.
- [ ] Port-specific changes have not been pushed into the matching decompilation without a matching reason.
