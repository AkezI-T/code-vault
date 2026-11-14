# Communication protocols

[简体中文](README.md) | English

Cross-chip, cross-language communication protocol contracts and portable codecs with no specific-peripheral dependency.

## Modules

| Module | Language | Responsibility |
| --- | --- | --- |
| [uart-frame-protocol](uart-frame-protocol/README.en.md) | C + Python | Generic UART frame transport: fixed header + variable payload + CRC8, with an encoder and a byte-fed receive state machine |

See [docs/architecture.md](../docs/architecture.md) for file placement and [docs/module-template.md](../docs/module-template.md) for the module documentation template.
