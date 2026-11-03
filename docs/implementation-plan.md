# 实现计划与验收

1. 保留 GPL 许可，参考 `../kmm-bn-price` 的 d7392c5；独立命名原生 Qt6/KF6 插件。
2. 实现显式市场代码验证、CNY 白名单、精确整数缩放及上海行情日期验证。
3. 固定 HTTPS 异步串行客户端：去重、限速、大小/时间限制、冷却与取消；全批成功才产生报价。
4. 复用并更名会话防护、独立原子账本写入、XMLGUI 与每账本 UUID 配置；支持 Stock/MutualFund，默认全不选。
5. 提供中文文档及无账本 CLI smoke。
6. 用户后续新增阶段：由另一个独立实现代理参照 `kmm-bn-price` 提供 GitHub Actions 工作流定义与原生包实现。矩阵为 Arch amd64、Debian sid/openSUSE Tumbleweed 各 amd64/aarch64/riscv64 共七目标；PR/main 三个 amd64，tag/手动七包，仅 tag push 发布。细节见 [ci.md](ci.md)。

验收：CMake build、离线 ctest（报价、网络、事务、原生发现/XMLGUI/fakehost 生命周期），仅内存引擎；原实现者一次联网 smoke 和主会话后续重复一次均如实记录。不得改参考库、真实账本、安装依赖、推送或自动保存。独立审查已无阻塞；本次文档修订交同一审查者复审，不声明已最终提交。

新增打包阶段已完成本地 Python unittest、逐脚本 `bash -n`、YAML 结构与 CI helper 构建/测试/暂存/发现验收，分阶段证据见 [verification.md](verification.md)。不得以本地检查代替七目标容器真实原生包构建/安装或 hosted Actions 验证；这些执行及真实 Release API 均未验证。
