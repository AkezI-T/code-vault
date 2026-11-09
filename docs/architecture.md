# Repository architecture

## Organizing principle

This repository uses one Git history for independently understandable modules. Functional domains group related capabilities; language and build choices belong to each module.

```text
code-vault/
  modules/
    <domain>/
      <module-name>/
        README.md
        <language-specific dependency manifest>
        src/
        tests/       (when needed)
        assets/      (small runtime resources, when needed)
  examples/
    <domain>/
      <module-name>/
        README.md
        <runnable example files>
  docs/
    architecture.md
    module-template.md
  tools/
    microcontrollers/
      README.md
  scripts/
  README.md
  .gitignore
```

The domain and module directories above describe the layout for added code. The initial repository contains the navigation and design documents.

## File placement

| File or capability | Location |
| --- | --- |
| Reusable implementation | `modules/<domain>/<module-name>/src/` |
| Public API and usage instructions | The owning module's `README.md` |
| Dependency manifest and lockfile | The owning module's root, according to its package manager |
| Module-specific tests and fixtures | The owning module's `tests/`, or its language's established test layout |
| Small runtime resources | The owning module's `assets/` |
| Runnable integration or usage example | `examples/<domain>/<module-name>/` |
| Guidance covering multiple modules | `docs/` |
| Development tools | `tools/<domain>/<tool-name>/` |
| Repository maintenance command | `scripts/` |
| Module-specific helper script | The owning module's `scripts/` |
| Generated output, caches, credentials | Local storage excluded by Git |
| Large datasets, installers, release binaries | External storage or release assets with a documented retrieval location |

Use English kebab-case directory names. Microcontroller firmware modules live in `modules/microcontrollers/`; host-side microcontroller development tools live in `tools/microcontrollers/`. Create additional domains as needed.

## Module boundaries

A module has one documented responsibility, a supported public entry point, and explicit dependencies. Its README states the runtime or toolchain, installation steps, API usage, configuration, and any platform requirements.

A consumer imports the supported entry point. Modules depend on other modules through their public APIs and declare the dependency using the language's package system. Dependencies form an acyclic graph. Shared code lives in a module named after its responsibility when actual consumers need it.

Examples depend on modules. Modules remain usable without examples, repository maintenance scripts, or documentation tooling. A cross-language dependency requires an explicit protocol or binding documented by the owning modules.

## Configuration and resources

Pass application-specific addresses, paths, and options into the public API. Keep real tokens, passwords, private keys, and device-specific credentials outside version control. An example configuration contains placeholders only.

Keep source assets needed for normal operation beside the owning module. Document the origin and usage rights of third-party resources. Download large optional resources into an ignored local directory and document retrieval in the owning README.

## Dependencies and versions

Each module owns its build commands, dependency declarations, and applicable lockfiles. Root documentation links to those commands. Choose a shared workspace tool when multiple modules in the same ecosystem need coordinated installation or builds.

The default branch is `main`. A release identifies its included modules and API changes. Introduce version manifests and release automation when a module needs an installable distribution.

## Adding a module

1. Select a functional domain and module name.
2. Create the module's source entry point and required manifest.
3. Write its README from the [module template](module-template.md).
4. Add any necessary tests and a focused usage example.
5. List it in the [module catalog](../modules/README.md).

When a module needs a separate permission boundary or a release process that conflicts with this repository, give it its own repository and update consumers to use its published interface.
