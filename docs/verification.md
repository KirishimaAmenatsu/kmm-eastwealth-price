# 实现验证记录（2026-11-03）

本记录按执行角色与阶段区分证据。原实现者记录来自本角色实际执行；主会话验收与独立审查结果由主会话提供，不冒充本角色重新执行。未操作真实账本或用户插件目录；下述 install 均仅暂存。独立审查已完成且无阻塞，本次文档修订待同一审查者复审；本文不声明已最终提交。

## 原实现者：Debug 核心插件验证

构建命令：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

离线目标：quotes、network、plugin_metadata、plugin_discovery、ledger。
本角色核心验证构建退出码0；CTest **5/5通过，0失败，1.75秒**。以下记录仅覆盖核心插件，不包含新增打包阶段。
plugin_discovery 在临时 DESTDIR 执行真实 install 并用宿主同样的 namespace 查找插件。fakehost 使用内存引擎，验证 Stock/MutualFund 默认全不选、Bond 被过滤、基金显式配置与 canonical 持久化、普通报价配置不变、生命周期与卸载。

另执行本地暂存（非用户安装）并验证发现：

```sh
cmake --install build --prefix /home/Amenatsu/Documents/code/kmm-eastwealth-price/stage
QT_QPA_PLATFORM=offscreen ./build/test_plugin --discover-root /home/Amenatsu/Documents/code/kmm-eastwealth-price/stage/lib/qt6/plugins
```

两者均退出码0，暂存产物为 `stage/lib/qt6/plugins/kmymoney_plugins/kmm_eastmoney_price.so`。

原实现者在实现阶段运行一次联网 smoke（退出码0），调用的是同一 Qt 客户端，没有读取账本。主会话后来另行重复一次，见后文；并非全项目只请求过一次：

```sh
./build/quote-smoke 1.600519 1.000001 0.159915 0.920047
```

原始输出：

```text
1.600519 CNY 1480/1 date=2026-11-03 source=EastMoney [1.600519]; CNY; quote update date Asia/Shanghai
1.000001 CNY 36081/10 date=2026-11-03 source=EastMoney [1.000001]; CNY; quote update date Asia/Shanghai
0.159915 CNY 2403/1000 date=2026-11-03 source=EastMoney [0.159915]; CNY; quote update date Asia/Shanghai
0.920047 CNY 304/25 date=2026-11-03 source=EastMoney [0.920047]; CNY; quote update date Asia/Shanghai
```

## 主会话：RelWithDebInfo 核心验收

主会话实际执行下列命令，全部退出码0；CTest **5/5通过，0失败，1.87秒**：

```sh
cmake -S . -B build-acceptance -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-acceptance -j4
ctest --test-dir build-acceptance --output-on-failure
```

主会话另行重复一次无账本联网 smoke，退出码0，四标的输出与上文原始记录完全相同：

```sh
./build-acceptance/quote-smoke 1.600519 1.000001 0.159915 0.920047
```

主会话核对参考仓库 `kmm-bn-price` 的 `git status --short` 仍为空，参考源码未被修改。两次 smoke 成功仅证明各次访问成功，不保证持续可用。

## 主会话：用户新增 CI/打包阶段验收

`.github/`、`packaging/`、`docs/ci.md` 属于用户后续明确要求、主会话授权的另一独立实现代理，现已提供工作流定义、原生打包实现及 [CI 文档](ci.md)。本角色本次只整合文档，不修改该阶段文件。

主会话实际执行以下本地检查：

```sh
python3 -W error::ResourceWarning -m unittest discover -s packaging/tests -v
for script in packaging/build-project.sh packaging/ci/*.sh; do bash -n "$script" || exit; done
```

Python **23/23通过**，逐脚本 `bash -n` 全部退出码0；另用 PyYAML `BaseLoader` 核对工作流 trigger/permission 结构，通过。注意 `bash -n file1 file2 file3` 只解析第一个文件，不能代替上述循环；主会话及本角色均复现 `bash -n /dev/null /nonexistent-second-script` 仍退出0。

主会话实际通过 CI helper 完成配置、构建、测试、暂存及发现验收，以下命令全部退出码0，其中 CTest **5/5通过，0失败，1.87秒**：

```sh
KMM_PLUGIN_DIR=/usr/lib/qt6/plugins/kmymoney_plugins bash packaging/build-project.sh configure . build-ci-check
BUILD_JOBS=4 bash packaging/build-project.sh build . build-ci-check
bash packaging/build-project.sh test . build-ci-check
bash packaging/build-project.sh install . build-ci-check /home/Amenatsu/Documents/code/kmm-eastwealth-price/stage-ci-check
env QT_QPA_PLATFORM=offscreen ./build-ci-check/test_plugin --discover-root /home/Amenatsu/Documents/code/kmm-eastwealth-price/stage-ci-check/usr/lib/qt6/plugins
```

install 仅写入 `stage-ci-check/`，不是安装到系统 `/usr`。主会话对暂存 `kmm_eastmoney_price.so` 执行 `readelf -d`，未发现 RPATH/RUNPATH；`ldd` 无 `not found`，链接真实的 `kmm_plugin` 与 `kmm_mymoney` 两库。这是本机 helper 与暂存验证，不是七目标容器原生包构建/安装验证。

## 独立审查

主会话提供的独立审查结果：审查者实际运行 build、**5/5 CTest**、**23/23 Python 测试**、shell 与 YAML 检查，均通过；实际代码与变更审查无阻塞问题。低优先级文档问题为 README 多文件 `bash -n` 命令遗漏后续脚本，本次已改为逐个循环；本次 docs 变更仍交同一独立审查者复审。

## 未验证边界与非阻塞覆盖建议

- **hosted GitHub Actions 尚未运行验证**；工作流定义与本地检查不代表 hosted job、七目标容器中的真实原生包构建/安装、真实 Release API 或远端发布已验证。
- 未验证真实 KMyMoney 宿主 UI/真实账本更新、长期定时运行、其他发行版/架构的实际执行、真实服务器限流/TLS故障与服务未来稳定性；限流/TLS故障仅由假网络注入覆盖。
- 非阻塞覆盖建议：`verify-package` 单测当前只覆盖 Arch，Debian/RPM 路径尚未覆盖；本次不扩大实现。
- 非阻塞覆盖建议：fakehost 插件测试不启动报价，尚无完整插件接收延迟报价结果并关闭/重开账本的端到端用例。现有模块级 cancel、SessionGuard、ledger 测试不能冒充该 E2E 覆盖；本次不扩大实现。
