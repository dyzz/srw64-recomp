超级机器人大战 64 原生重编译版 · Steam Deck 版（实验性）
SRW64 Recompiled, Steam Deck edition (experimental). English below.

本程序只供你自己使用，不含 ROM。游戏文字与头像在第一次启动时从你自己的 ROM 导入。请勿转发。

安装（在桌面模式里做一次）
  1. 把这个文件夹解压到任意位置，例如 ~/Games/SRW64。
  2. 把 ROM（日版 Rev 0，.z64 字节序）复制成
       ~/.local/share/srw64-recomp/rom.z64
     也可以放在 srw64.sh 旁边，命名为 rom.z64。
  3. 打开 Steam →「游戏」→「添加非 Steam 游戏到我的库」→「浏览」，选中 srw64.sh 添加。
  之后回到游戏模式，就能像普通游戏一样从库里启动。不需要改 Steam 输入设置。

按键（Steam Deck 默认手柄模板）
  A            确认 / 下一页
  B（或 X）    取消 / 返回
  菜单键 ☰     START
  十字键 / 左摇杆  移动光标
  L1 / R1      L / R（地图上切换我方机体；对白中 L1 打开回看）
  L2 或 Y      Z 扳机
  右摇杆       C 键（对白中上下调字号）
  视图键 ⧉     打开 / 关闭设置：语言、原版／HD 画面、规则修正、各界面切换
               设置里用十字键移动、A 选择、B 关闭
  对白：R1 + A 按住快进，R1 + 菜单键 跳过当前段落
  输入姓名时会弹出 Steam 屏幕键盘；也可以直接用默认名字。
  界面底部的操作提示会随你最后用的设备显示手柄按键或键盘按键。

存档与设置
  ~/.local/share/srw64-recomp（sessions/ 里保留每次游玩的存档）。
  从 Steam 菜单选「退出游戏」会正常保存退出。

这一版的限制
  - 只有原版画面：HD 图层（整张头像、背景、剧情文字图、3D 标记）还在移植。
  - 同一程序也能在其他 x86-64 Linux 上运行（glibc 2.35 以上，需要 Vulkan 驱动）。

------------------------------------------------------------------------------
English

For your own use only; no ROM is included. Text and portraits are imported from
your ROM on first launch. Do not redistribute.

Install (once, in Desktop Mode): extract this folder, copy your ROM (Japan, Rev 0,
.z64) to ~/.local/share/srw64-recomp/rom.z64 (or next to srw64.sh as rom.z64), then
in Steam choose Games > Add a Non-Steam Game to My Library and add srw64.sh. It then
starts from Game Mode; no Steam Input changes are needed.

Controls: A confirm, B/X back, Menu = START, D-pad/left stick move, L1/R1 = L/R
(L1 opens the dialogue history), L2 or Y = Z, right stick = C buttons, View opens
the settings (language, Original/HD, rules). Dialogue: hold R1 + A to fast-forward,
R1 + Menu skips. Names open the Steam on-screen keyboard. Hints follow the last
device you used.

Saves: ~/.local/share/srw64-recomp. Quitting from the Steam menu saves normally.
This build shows Original images only; the HD layers are still being ported.
Runs on other x86-64 Linux too (glibc 2.35+, a Vulkan driver).
