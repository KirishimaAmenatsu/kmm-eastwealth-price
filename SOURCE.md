# 来源与许可

本项目以 GPL-2.0-or-later 发布，完整许可见 LICENSE。

参考项目：`/home/Amenatsu/Documents/code/kmm-bn-price`，参考 commit `d7392c5`。
复用/改编其 CMake 配置、插件生命周期与配置 UI、SessionGuard、exactJson/decimal、ledgerwriter、compat 头文件及测试 fakehost/插件发现/账本事务测试。保留原文件 SPDX 与上游版权声明；新增东方财富报价、网络处理和测试同样使用 GPL-2.0-or-later。

`tests/upstream/interfaceloader.h` 来自 KDE KMyMoney，版权和许可保留于文件中。compat 是参考项目用于 Arch KMyMoney 5.2.2 缺失安装头的窄兼容层，不是完整 SDK。

东方财富是行情服务提供方，本插件非官方、无关联、不代表获得服务授权。用户须自行确认使用条款及行情许可。
