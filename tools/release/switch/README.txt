Nintendo Switch（docs/design/switch-port.md）

  tools/release/switch/build.sh smoke    在 Docker 里（devkitpro/devkita64，固定摘要）编工具链检查程序，
                                         产出 build/recomp/switch-smoke/srw64-smoke.nro

文件
  Dockerfile        镜像：官方 devkitA64，已带 libnx、switch-sdl2、switch-mesa、switch-glad、
                    FreeType、HarfBuzz、switch-tools
  build.sh          宿主侧入口：建镜像、把仓库按同一绝对路径挂进容器再编
  smoke/            检查程序：打印小程序类型、掌机/底座、进程内存、GLES 2.0 与 GL 2.1 兼容上下文
                    各自的建立耗时与驱动字符串，然后清屏变色 20 秒（按 + 提前退出）

推到实机（nxlink 必须在 Mac 本机跑，不能在容器里，原因见 switch-port.md「开发循环」）
  Mac 上的 nxlink 从 switch-tools v1.13.1 源码编：
    git clone --depth 1 --branch v1.13.1 https://github.com/switchbrew/switch-tools.git ~/.local/share/switch-tools
    cc -O2 -o ~/.local/bin/nxlink ~/.local/share/switch-tools/src/nxlink.c -lz
  Switch 上按住 R 启动任意游戏进 hbmenu（应用模式），按 Y 进 NetLoader，屏幕上会显示 IP。
    cd build/recomp/switch-smoke
    nxlink -a <Switch IP> -p switch/Marchwind64/srw64-smoke.nro -s srw64-smoke.nro
  不带 -a 时 nxlink 发 255.255.255.255 广播，macOS 上报 "sendto: No route to host"，所以总是带 -a。
