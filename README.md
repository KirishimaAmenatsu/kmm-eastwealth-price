# KMyMoney 东方财富证券报价插件

面向 **KMyMoney 5.2.2 / Qt6（≥6.6）/ KF6 / C++17** 的原生插件，不是普通网页 online quote 脚本。首版只处理用户显式选择的**内地 CNY 股票、主要指数、场内 ETF/LOF**，不进行交易、换汇或自动保存。插件及证券选择默认关闭，可与参考的币安插件同时安装。

**非官方公开接口，可能变化、限流、封禁或停止服务；不保证实时性、可成交性、历史收盘价或数据准确性。** 请先备份账本并用副本验证，确认服务条款和行情使用许可。没有账号、API key、下单或反爬绕过功能。

## 构建与验证

依赖：CMake ≥3.22、C++17 编译器、Qt6 Core/Network/Widgets（测试另需 Test）、KF6 CoreAddons/Config/XmlGui、KMyMoney 5.2.2 开发头与 `libkmm_plugin` / `libkmm_mymoney`、Qt6 Alkimia、GMP/GMPXX。不自动安装系统依赖，不支持 Qt5/KF5 插件 ABI。宿主/Qt/KF/Alkimia 升级后应重新构建。

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

产物为 `build/kmm_eastmoney_price.so`。离线测试不访问网络或真实账本，使用 fake 网络与真实的进程内临时账本引擎。覆盖严格输入、精确缩放/日期/身份、请求去重/间隔/超时/取消/重入/大小/限流、全批失败、事务中途补偿、拒绝外层事务、保存通知、插件发现/XMLGUI、关闭重开与卸载、Stock 和 MutualFund 默认全不选。**fakehost 测试不等于真实宿主 UI/真实账本已经验证。**

Arch 的 5.2.2 开发文件没有完整 CMake package，使用头/库查找；Alkimia 优先 CMake target，缺失时查找 Qt6 头/库。`compat/` 仅补漏装头，系统已安装头优先；`tests/upstream/` 仅用于假宿主注册原接口，不改变生产 ABI。

### 无账本联网 CLI

```sh
./build/quote-smoke 1.600519 1.000001 0.159915 0.920047
```

使用与插件相同的 Qt 异步客户端，只输出成功整批的 secid、CNY **精确有理数**、日期及来源；失败打印 `FAILED:` 并非零退出。没有账本读写。网络/地区限制或防护失败应停止，不应换域名、关 TLS 校验或循环重试。

## GitHub Actions 与原生包

已按用户后续要求参照 `kmm-bn-price` 提供 GitHub Actions 工作流定义与原生打包实现，包含七个目标：**Arch Linux amd64**，以及 **Debian sid / openSUSE Tumbleweed 各 amd64、aarch64、riscv64**；分别生成原生 Arch 包、DEB、RPM，不表示兼容所有 Debian/Ubuntu 或 RPM 发行版。

工作流定义在 PR / `main` push 构建三个 amd64 包，tag push / 手动触发构建全部七包，**仅 tag push 发布 Release**。具体触发条件、构建约束、包安装与验证说明见 [CI 与打包文档](docs/ci.md)。本地检查与验收证据见 [验证记录](docs/verification.md)；**hosted GitHub Actions 尚未运行验证**，七目标容器中的真实原生包构建/安装与真实 Release API 尚未验证，不代表已发布包或创建远端 Release。

本地打包逻辑与 shell 语法检查（不执行容器构建或安装依赖）：

```sh
python3 -m unittest discover -s packaging/tests -v
for script in packaging/build-project.sh packaging/ci/*.sh; do bash -n "$script" || exit; done
```

## 暂存及用户安装

以下安装命令由用户决定执行；先退出 KMyMoney，保留备份。无需 root。

```sh
# 固定相对安装路径，避免发行版默认 lib64 差异
cmake -S . -B build -DKMM_PLUGIN_INSTALL_DIR=lib/qt6/plugins/kmymoney_plugins
cmake --build build -j 4
# 先暂存检查；不写系统目录
cmake --install build --prefix "$PWD/stage"
QT_PLUGIN_PATH="$PWD/stage/lib/qt6/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}" kmymoney
```

确认后用户级安装：

```sh
cmake --install build --prefix "$HOME/.local"
QT_PLUGIN_PATH="$HOME/.local/lib/qt6/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}" kmymoney
```

`QT_PLUGIN_PATH` 是**包含 `kmymoney_plugins/` 的目录**，不是 `.so` 所在目录或文件。只对这次启动设置，勿混入 Qt5 路径。若设过绝对 `KMM_PLUGIN_INSTALL_DIR`，`--prefix` 不会改变绝对目的地，须先改回上述相对路径。KMyMoney 5.2.2 使用 `KPluginMetaData::findPlugins("kmymoney_plugins")`；`plugin_discovery` 会在临时 DESTDIR 内按实际安装规则验证此 namespace，不写用户/系统插件目录。

在 **设置 → 配置 KMyMoney → 插件** 启用 **EastMoney Securities Prices / 东方财富证券报价**（默认禁用）。工具菜单增加“配置东方财富证券报价…”、“更新东方财富证券报价”、“东方财富报价状态…”。仍必须逐只勾选证券，加载插件本身不会请求行情。

卸载：先禁用插件、退出宿主，仅删除自己安装的 `~/.local/lib/qt6/plugins/kmymoney_plugins/kmm_eastmoney_price.so`。配置和已写入价格不会自动删除；不影响币安插件。

## 证券配置

1. 在 KMyMoney 建立或编辑**股票 Stock / 基金 MutualFund 类型**证券。指数也须作为证券，而不是货币。插件不自动创建证券、货币、账户或交易。
2. 证券交易货币必须是账本已有的 **CNY**；本位币可为其他币，但插件不作换汇。不接受 CNH、USD、HKD 等，不把未确认品种当作人民币价格。
3. 打开“配置东方财富证券报价…”，默认全部不选、代码输入为空。勾选需要的证券，并输入显式市场代码。不会读取普通 online quote 设置，也不从裸交易符号猜市场。

| 品种示例 | canonical secid | 也可输入 |
| --- | --- | --- |
| 沪市贵州茅台 | `1.600519` | `SH600519` / `600519.SH` |
| 上证指数 | `1.000001` | `SH000001` |
| 深市平安银行 | `0.000001` | `SZ000001` |
| 深市创业板 ETF | `0.159915` | `SZ159915` |
| 沪市沪深300 ETF | `1.510300` | `SH510300` |
| 北交所现行代码 | `0.920047` | `BJ920047` |

**`1.000001` 与 `0.000001` 是不同品种。** 不接受裸 `000001` / `600519`。支持 SH/SZ/BJ 大小写前后缀（可带点/冒号），保存为 canonical。BJ 映射到东方财富市场 `0`，但 `SZ920047` 被拒绝；旧 BJ `430047` 等不兼容，不会自动迁移。

保守代码白名单（不是品种存在性保证）：沪市 `600/601/603/605/688` A股、`000xxx` 主要指数、`51xxxx/56xxxx/58xxxx` ETF；深市 `000/001/002/003/300/301` A股、`399xxx` 指数、`159xxx` ETF、`16xxxx` 场内 LOF；北交所 `920xxx`。仅在返回身份一致、正价格及时间有效时才使用。**不支持 B股、港美股、债券本身、场外净值基金、期货、旧北交所代码或白名单之外的新代码。** ETF/LOF 的报价是场内交易价格而非基金净值（含债券类 ETF 也不是债券直接报价）。同码不同份额/复权/单位需用户核实，本插件不进行份额换算或复权。

4. 可分别关闭“打开已配置账本时及每15分钟自动更新”和“保存配置后立即更新”。默认自动选项为开，但没有勾选证券时绝不请求；关闭自动后可手动更新。
5. 状态显示最后结果和每只证券来源。自动失败不弹窗，手动失败显示原因；两者均不使用缓存旧行情兜底。
6. 成功后查看 KMyMoney 价格列表：**证券 ID → CNY，日期取各证券行情更新时间**。同证券/货币/日期已有价格会被替换，即使来自其他来源；不是日内时间序列。用户主动保存账本后才持久化。

配置独立位于 `$XDG_CONFIG_HOME/kmm-eastmoney-pricerc`（通常 `~/.config/kmm-eastmoney-pricerc`），`Ledger-<storage UUID>` 分组保存 Enabled、Automatic 和 Secid。账本副本若保留 UUID 会共享选择，但关闭重开的异步结果仍由独立 generation 隔离。新账本无稳定 UUID 时先保存、关闭再重开；插件不会为加载而创建 UUID 或污染账本。

## 精度、身份与日期

固定请求：

```text
https://push2.eastmoney.com/api/qt/stock/get?secid=1.600519&fields=f43,f57,f58,f59,f86,f107
```

校验 `rc == 0`、完整 data、`f57` 代码和 `f107` 市场都与请求一致。`f43` 必须是正整数、`f59` 必须为 0..8 的整数，价格为 **`f43 / 10^f59`**。从原始 JSON 数字词法构建 MyMoneyMoney/GMP 有理数，不以 double 运算；例如 2403 / 1000 = 2.403，不按法币两位小数截断。空值、`"-"`、数字字符串、零、负数、小数/指数型整数、异常精度、缺项或坏 JSON 均拒绝。`f58` 仅是可读名称，不用于猜币种或身份。

`f86` 是行情更新时间 Unix 秒，转换为 **Asia/Shanghai（固定 UTC+08:00）日历日**。拒绝未来超过5分钟或日期比上海今天早超过14个日历日的行情（容纳长假）；14日边界仍可接受。它**不保证是最后成交日、交易所正式收盘日或历史收盘价**，停牌、盘前、长假等可能带来旧行情。系统时间错误会影响校验；不是拿抓取当天日期冒充行情日期，也不查询历史行情。

## 网络、隐私与安全边界

- 只有上述固定 HTTPS 主机和路径；无备用域名、自动重试、API key 或用户凭据。不跟随重定向，不忽略 TLS 错误。Qt 可能使用系统代理与系统 CA；须自行信任网络和宿主环境。
- 不发送账本文件、账本 UUID、内部证券 ID、名称、账户、持仓或金额。服务方/代理仍会看到 IP、所请求的**市场与证券代码及时间**，可推断关注品种；网络层行为也受 Qt/系统配置影响。
- 每批最多100只证券，按 canonical secid 去重请求，保留各证券映射；异步串行，响应后至少约100ms间隔。单请求15秒墙钟截止，整批90秒（含间隔），单响应最多1MiB。Qt事件循环不阻塞，不并发重入。
- 429/418 读取 `Retry-After` 秒数/HTTP日期：有效未来时间照常等待，无效或过期冷却5分钟；超大数字在安全日期上界9999年饱和，避免溢出缩短封禁。插件实例存活期间，冷却期内手动/自动均不发新请求；状态提示下次时间，不自动立即重试。不要用卸载/重载规避限流。重启后冷却不持久化。
- 任一请求/身份/日期/格式验证失败，整批不产生可写报价，不将失败转为零或局部写入；所有证券完成后才重验配置、证券类型/CNY、账本 UUID 与 generation。
- 关闭/切换/相同 UUID 重开账本、模型 reset、改配置、卸载都会取消并作废旧请求结果。完成写入前无嵌套事件循环。
- 在一次**独立**账本事务中只写价格；如果已有事务直接拒绝，不加入外层。KMyMoney 5.2 PriceModel 不受普通 undo 完整保护，故中途失败时显式恢复精确旧日期价格（含来源），移除本次新增项，保留反向价格。成功提交后补发必要的 dataChanged 保存通知；失败不发成功通知。崩溃、内存耗尽、引擎补偿本身失败等进程级故障不在可保证原子性范围。
- 不改普通 online quote 配置、证券定义、持仓、账户或交易，不自动保存。人工操作或其他报价源也可能覆盖同日价格，请自行协调。价格写入仅表示接口响应符合约束，不证明其经济真实性。

## 故障排查

- 插件不可见：确认 Qt6/KF6/KMyMoney ABI、`QT_PLUGIN_PATH` 层级，查看终端动态加载错误，升级后重新构建。
- 菜单不可用：先打开有稳定 UUID 的账本；无 UUID 则用户保存后关闭重开。不需通过建立持仓来触发保存按钮。
- 配置被拒绝：检查显式市场、Stock/MutualFund、交易货币确为 CNY、代码白名单和 ≤100 上限。不要将 B股/港股伪装成 CNY 证券。
- 身份/日期/数据错误：远端改接口、无行情、迁码、停牌或系统时间不准均可导致整批拒绝；核对状态，不强行写价。
- TLS/超时/HTTP403/418/429：检查网络和时间、等待冷却；不要关闭 TLS、绕过服务防护或频繁重试。smoke 失败不等于可以安全写入旧价格。
- “账本正在处理其他事务”：等待宿主当前操作结束再手动更新。

## 源码结构与许可

- `src/quotes.*`：市场白名单、精确 JSON/有理数、日期与会话票据。
- `src/quoteclient.*`：固定 HTTPS 异步网络与整批验证。
- `src/ledgerwriter.*`：独立事务、失败恢复和保存通知。
- `src/plugin.cpp`、`src/plugin.json`、`src/eastmoneyprice.rc`：原生插件、逐账本 UI、独立 XMLGUI/配置命名。
- `tests/`、`compat/`、`tools/smoke.cpp`：离线测试/窄兼容头/只读 CLI。
- `docs/implementation-plan.md`：计划与验收；[SOURCE.md](SOURCE.md)：参考项目路径和 commit `d7392c5`，复用范围与上游版权。

GPL-2.0-or-later，见 [LICENSE](LICENSE) 及文件内 SPDX。参考 [KMyMoney 5.2 源码](https://invent.kde.org/office/kmymoney/-/tree/5.2)；东方财富接口没有本项目可依赖的正式稳定性保证。本插件不是投资建议。
