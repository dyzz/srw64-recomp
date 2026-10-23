Nintendo Switch（docs/design/switch-port.md）

  tools/release/switch/build.sh smoke    在 Docker 里（devkitpro/devkita64，固定摘要）编工具链检查程序，
                                         产出 build/recomp/switch-smoke/srw64-smoke.nro
  tools/release/switch/build.sh game     游戏本体：srw64-gl-host（SRW64_GL_ONLY）交叉编译成
                                         build/recomp/switch-build/Marchwind64.nro；加 -k 遇错继续
  tools/release/switch/build_icu.sh      ICU 78.3 静态库（devkitPro 没有）与数据子集的 C 源码，
                                         build.sh game 缺了会自动跑；到 build/recomp/switch-deps

  先在仓库根目录 make（生成 CPU 代码、拉固定版本上游、RSP 音频）。worktree 里 build/ 的内容软链到
  主工作区（cpu-bound、runtime-lifecycle、audio-probe、frontend-adapter 留本地），build.sh 会把
  主工作区一起挂进容器。

文件
  Dockerfile        镜像：官方 devkitA64，已带 libnx、switch-sdl2、switch-mesa、switch-glad、
                    FreeType、HarfBuzz、switch-tools
  build.sh          宿主侧入口：建镜像、把仓库按同一绝对路径挂进容器再编
  build_icu.sh      ICU 交叉编译（配置照 mh-linux）；数据换成 tools/release/icu_data.py 的子集
  smoke/            检查程序：打印小程序类型、掌机/底座、进程内存、GLES 2.0 与 GL 2.1 兼容上下文
                    各自的建立耗时与驱动字符串，然后清屏变色 20 秒（按 + 提前退出）

推到实机（nxlink 必须在 Mac 本机跑，不能在容器里，原因见 switch-port.md「开发循环」）
  Mac 上的 nxlink 从 switch-tools v1.13.1 源码编：
    git clone --depth 1 --branch v1.13.1 https://github.com/switchbrew/switch-tools.git ~/.local/share/switch-tools
    cc -O2 -o ~/.local/bin/nxlink ~/.local/share/switch-tools/src/nxlink.c -lz
  Switch 上按住 R 启动任意游戏进 hbmenu（应用模式），按 Y 进 NetLoader，屏幕上会显示 IP。
    cd build/recomp/switch-smoke
    nxlink -a <Switch IP> -s srw64-smoke.nro
  文件落到 sdmc:/switch/srw64-smoke.nro。-p 是相对 sdmc:/switch 的路径，hbmenu 不建目录，
  目录不存在时屏幕报 "open: err=2"。
  不带 -a 时 nxlink 发 255.255.255.255 广播，macOS 上报 "sendto: No route to host"，所以总是带 -a。
  NetLoader 只接受一次连接：别用 nc 探端口，空连接会让它报 "error getting name length err=11"。
  本机的 nxlink 加了一行：Switch 回未知错误码时打印出来（原版直接静默退出 1）。
  按住 R 启动游戏进的 hbmenu 才是应用模式；从相簿进只有约 370 MiB 内存。
