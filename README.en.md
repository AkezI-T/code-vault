# Code Vault

[简体中文](README.md) | English

A personal collection of reusable code modules, runnable examples, and supporting documentation.

## Find a place for a file

| Location | Contents |
| --- | --- |
| [modules/](modules/README.md) | Reusable modules, grouped by functional domain |
| [Microcontroller modules](modules/microcontrollers/README.md) | Reusable firmware modules, drivers, and protocols |
| [tools/](tools/README.md) | Development tools, grouped by functional domain |
| [Microcontroller tools](tools/microcontrollers/README.md) | Host-side tools for microcontroller development |
| [examples/](examples/README.md) | Small programs that demonstrate module usage |
| [docs/architecture.md](docs/architecture.md) | File placement, boundaries, and dependency rules |
| [docs/module-template.md](docs/module-template.md) | Starting point for a module README |
| [scripts/](scripts/README.md) | Repository maintenance scripts |

## Add a module

1. Choose a domain and a descriptive kebab-case name, such as `modules/networking/http-client/`.
2. Put implementation files in `src/` and expose the supported API through the language's normal entry point.
3. Add a module README using the [template](docs/module-template.md), along with the dependency manifest required by that language.
4. Keep module-specific tests and small runtime assets inside the module when needed.
5. Add a runnable example under `examples/<domain>/<module-name>/` when it helps demonstrate usage.
6. Add the module to the [module catalog](modules/README.md).

Each module owns its dependencies and tool configuration. Commands run from the relevant module or example directory unless its README says otherwise.

## Everyday Git workflow

```bash
git status
git add <specific-paths>
git diff --cached
git commit -m "Add module description"
git push origin main
```

Store credentials in the environment or a local secret store. Commit sample configuration with placeholder values when configuration is needed.

## Availability

This is a private repository. Reuse and redistribution rights require an explicit license or permission from the owner.
