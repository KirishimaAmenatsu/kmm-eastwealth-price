# 原生包 CI 与发布

本项目是东方财富（EastMoney）大陆 CNY 股票、指数、ETF、LOF 报价插件，
不提供外汇报价。CMake project / 插件为 `kmm_eastmoney_price`，安装文件为
`kmm_eastmoney_price.so`，发行包名为 `kmm-eastmoney-price`。

包元数据暂用 `https://github.com/KirishimaAmenatsu/kmm-eastwealth-price`；
这只是预期项目地址，不表示已配置 remote、已创建远端或已发布。
本实现没有创建远端、push、打 tag 或发布 Release。

## 触发与七包矩阵

`.github/workflows/packages.yml` 默认只有 `contents: read`，只有 release job
有 `contents: write`。所有外部 Actions 固定 40 位提交 SHA，checkout 禁止保留凭据，
不使用 `pull_request_target`，不把事件输入直接插入 shell 源码。

| 事件 | 编译 / 测试 / 打包 | 发布 |
| --- | --- | --- |
| main push、所有 PR | 下列三个发行版的 amd64，共三包 | 不发布 |
| `v*` tag push | 先严格校验版本，再构建七包 | 全部通过后发布 |
| 手动 workflow_dispatch | 七包（选择 tag 时同样校验版本） | **不会发布** |

| 发行版容器 | 架构 | 包格式 | 执行环境 |
| --- | --- | --- | --- |
| Arch Linux `base-devel` | amd64 / x86_64 | `.pkg.tar.zst` | ubuntu-24.04 原生 |
| Debian sid | amd64 | `.deb` | ubuntu-24.04 原生 |
| Debian sid | aarch64 / arm64 | `.deb` | ubuntu-24.04-arm 原生 |
| Debian sid | riscv64 | `.deb` | ubuntu-24.04 + QEMU |
| openSUSE Tumbleweed | amd64 / x86_64 | `.rpm` | ubuntu-24.04 原生 |
| openSUSE Tumbleweed | aarch64 | `.rpm` | ubuntu-24.04-arm 原生 |
| openSUSE Tumbleweed | riscv64 | `.rpm` | ubuntu-24.04 + QEMU |

源码只读挂载后复制进临时容器工作区；每个发行版使用自己的编译器、头文件、运行库、
原生打包工具（makepkg / dpkg-buildpackage / rpmbuild），不是给同一二进制修改后缀。
ARM 使用原生 runner；riscv64 在目标发行版用户空间中经 QEMU 编译测试，不是交叉编译。
Arch 仅支持 amd64；RPM 仅面向 Tumbleweed，不保证 Fedora/RHEL 兼容。
普通检查允许取消旧运行；tag 发布运行不自动取消，同一 ref 的发布串行执行。

## ABI、SDK 与包验证

当前支持 KMyMoney **5.2.2**：Arch、Debian 的包版本须为 `5.2.2-*`，
openSUSE 的上游版本须为 `5.2.2`；漂移到其他上游版本会直接失败。
每个成品包还依赖构建时实际安装的**精确发行版 KMyMoney 包版本**，包括发行版修订号：
Arch `kmymoney=...`、Debian `kmymoney (= ...)`、RPM `kmymoney = ...`。
Debian 使用 dpkg-shlibdeps，RPM 保留自动动态库依赖生成，不捆绑宿主库。

Debian 没有可直接使用的完整 KMyMoney 开发包，因此从已安装包的
`${source:Version}` 获取**匹配版本**的 source 和 build-dep，配置该源码，
只提取 `cmake_install.cmake` 明确列出的公共安装头（含配置生成的 export 头）。
未生成且不被插件使用的 `kmymoneysettings.h` 不复制、不伪造；SDK 内两个 `.so`
开发链接只指向 dpkg 列出的真实已装 `libkmm_plugin.so.5`、`libkmm_mymoney.so.5`，
不编译替代 KMyMoney 库，不把私有 SDK 打进成品包。公共头布局改变时拒绝继续。

`qtpaths` 查询实际 Qt6 `QT_INSTALL_PLUGINS`，再追加 `kmymoney_plugins`，
支持 Debian multiarch 和 RPM lib64 路径，不假定统一的 `/usr/lib` 安装目录。
各构建执行 CTest，再安装原生包，并检查：

- 原生元数据中的包名、版本、目标架构和精确 KMyMoney 依赖；
- 包载荷仅含目标 `.so`、原生包元数据、目录和许可 / 文档，不含测试程序、SDK 或宿主库；
- `readelf` 的 ELF machine 与目标架构一致，`ldd` 没有缺库并链接真实 KMyMoney 库；
- 插件无 RPATH / RUNPATH；
- 安装后以 `test_plugin --discover-root <Qt插件根目录>` 做 KMyMoney namespace discovery。

滚动发行版容器和依赖不是快照，也没有声称可复现构建：仓库保留版本、Qt/KF ABI、
源码依赖、ARM runner 可用性和 QEMU 支持都可能漂移；精确包版本依赖不等同于
Qt/KF 的完整 ABI 锁定。升级后应审查并重新构建，而不是放宽失败检查。

## 发布协议与安全边界

仅 `push` 的 `refs/tags/vX.Y.Z` 能发布，必须是无前导零、无预发布后缀的版本号。
tag、CMake `project(... VERSION ...)`、`src/plugin.json` 和 `PKGBUILD` 版本必须一致，
当前为 `0.1.0`。PR 或手动运行即使选择 tag，也没有发布路径。

下载本次运行的 package artifacts 后，只接受七个精确的预期文件名：
`kmm-eastmoney-price_<版本>-1_<发行版>_<架构>.<原生后缀>`。
缺失、多余、空文件、符号链接或目录一律拒绝。然后生成七行 `SHA256SUMS`。
这些包和 checksum **均未签名**：SHA256SUMS 只能检查完整性，不能证明发布者真实性。

脚本分页检查已有 Release（含 draft），并检查 tag endpoint，拒绝覆盖或自动续传已有发布。
解析 lightweight / annotated tag（有深度和环检测上限），确认最终 commit 与 workflow
`GITHUB_SHA` 相同；先创建 draft，再逐个上传七包和 checksum，核对上传状态、大小及
八个资产名，最后发布。每一次发布 PATCH（包括 HTTP 422 后的重试）之前都重新核验
tag，移动或删除时不再尝试发布，保留尚未发布的 draft。

HTTP 422 最多尝试五次、线性退避，不无限重试；其他 HTTP 错误立即失败。
422 后 GET 若发现 Release 已 published，会如实报告且不再 PATCH；再检查 tag，
若已移动 / 无法解析会报告需人工检查，不谎称仍是 draft，也不自动回退公开状态。
GitHub API 不支持把检查 ref 与发布原子化，最后一次 GET 到 PATCH 之间仍有 TOCTOU；
应配置 tag ruleset 禁止发布 tag 的更新和删除，且不要随意绕过规则。

失败后可能留下 draft（也可能在响应不明确时已经公开）：维护者应先检查 Release 状态、
源码 commit、当前 tag 和所有资产。脚本不会自动删除或覆盖已有 Release；
确认后由维护者决定是否删除失败 draft 并重跑，不能靠重复运行偷偷覆盖。

## 本地验证与当前限制

不需要安装依赖的检查：

```sh
python3 -m unittest discover -s packaging/tests -v
for script in packaging/build-project.sh packaging/ci/*.sh; do bash -n "$script"; done
python3 packaging/ci/metadata.py version
```

测试使用 mock 覆盖触发矩阵、版本一致性、精确资产 allowlist、draft 发布、拒绝覆盖、
上传途中及 422 重试期间 tag 漂移 / 删除、bounded retry、已公开状态和 Debian SDK。
可用 PyYAML 6.0.3 的 `BaseLoader` 解析 workflow，避免 YAML 1.1 把 `on` 当布尔值；
这只是语法 / 结构检查，不能替代 actionlint 或真实 Actions 执行。

若宿主已具有全部编译依赖，可用独立 `build-ci-check` 目录调用
`packaging/build-project.sh` 的 configure / build / test / install，install 必须传临时
DESTDIR；configure 须设置 `KMM_PLUGIN_DIR` 为已安装 Qt6 的真实插件路径。
不要在宿主运行 `container-build.sh` / `debian-sdk.sh`：依赖安装仅允许在一次性容器中，
脚本也要求 `CI_CONTAINER=1`，这个标记只是防误用，不是隔离或安全沙箱。

交付时 hosted GitHub Actions **尚未运行**，不能据本地单元测试声称七平台构建已验证。
当前环境缺 actionlint / shellcheck；本机 docker 是 Podman 包装且存储 driver 存在
overlay / btrfs 冲突，因此未执行真实 Docker / 七平台容器构建，也未尝试修改用户容器
存储或安装系统软件。真实 native 包依赖、各发行版可用性、QEMU、上传及发布 API 仍须
由后续实际 CI 验证；本地 CMake 检查（如有）只证明当前宿主环境。
