# Code Vault

简体中文 | [English](README.en.md)

个人模块化代码仓库，用于积累可复用的代码模块、可运行的示例及配套文档。

## 文件存放位置

| 位置 | 内容 |
| --- | --- |
| [modules/](modules/README.md) | 按功能领域组织的可复用模块 |
| [modules/microcontrollers/](modules/microcontrollers/README.md) | 单片机固件模块、驱动和协议 |
| [tools/](tools/README.md) | 按功能领域组织的开发工具 |
| [tools/microcontrollers/](tools/microcontrollers/README.md) | 电脑端使用的单片机开发工具 |
| [examples/](examples/README.md) | 演示模块用法的小型程序 |
| [docs/architecture.md](docs/architecture.md) | 文件归属、模块边界与依赖规则 |
| [docs/module-template.md](docs/module-template.md) | 新模块 README 的编写起点 |
| [scripts/](scripts/README.md) | 仓库维护脚本 |

## 添加模块

1. 选择功能领域，并使用小写字母和连字符命名模块，例如 `modules/networking/http-client/`。
2. 将实现代码放入 `src/`，通过所用语言的常规入口公开受支持的接口。
3. 参考[模块说明模板](docs/module-template.md)编写模块 README，并添加该语言所需的依赖清单。
4. 按需将模块专用的测试和小型运行资源放在模块内部。
5. 如需演示用法，在 `examples/<domain>/<module-name>/` 中添加可运行的示例。
6. 将模块登记到[模块目录](modules/README.md)。

每个模块自行管理依赖和工具配置。除模块说明另有约定外，命令均在对应的模块或示例目录中执行。

## 日常 Git 操作

```bash
git status
git add <specific-paths>
git diff --cached
git commit -m "Add module description"
git push origin main
```

将 `<specific-paths>` 替换为本次需要提交的具体路径。凭证存放在环境变量或本地凭证存储中；需要配置示例时，提交使用占位值的示例文件。

## 访问与使用权限

本仓库为私有仓库。复用与再分发需要明确的许可证或仓库所有者的许可。
