PS Vita（V0 可行性探针，docs/design/psv-port.md）

这里还不是发布脚本，是把重编译的游戏代码交叉编译到 32 位 ARM 并测量的工具：

  tools/release/vita/build.sh            在 Docker 里（vitasdk/vitasdk 官方镜像 + arm-linux-gnueabihf
                                         + qemu-user）编 srw64_cpu / srw64_audio / librecomp /
                                         ultramodern 的 ARMv7 静态库，-O2 与 -Os 各一次
  tools/release/vita/build.sh --qemu     再用 arm-linux-gnueabihf 编无图形的探针宿主，在 qemu-arm 下跑
                                         开场 N 帧并保存 RDRAM 快照
  python3 tools/release/vita/build_probe.py --native   本机（macOS/Linux）同一探针宿主的参照快照
  结果：build/recomp/vita/report.json（.text 大小、编译时间、告警、快照比对）

文件
  Dockerfile                 镜像：VitaSDK + gcc-arm-linux-gnueabihf + qemu-user
  build.sh                   宿主侧入口：先在本机打运行库补丁（prepare_vita_runtime.py），再进容器
  build_probe.py             容器内/本机的构建、运行与报告
  CMakeLists.txt             探针工程：生成代码、音频微码、运行库（Vita 替身）与探针宿主
  probe_host.cpp             无图形宿主：全部原生钩子调原版，第 N 帧在游戏线程上写 rdram.bin
  toolchain-gnueabihf.cmake  qemu 用的 Linux ARMv7 工具链

先在仓库根目录 `make`（生成 CPU 代码、拉取固定版本上游）。worktree 里按 memory 的做法把
build/ 软链到主工作区即可，build.sh 会把主工作区一起挂进容器。
