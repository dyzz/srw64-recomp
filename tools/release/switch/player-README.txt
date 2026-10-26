Marchwind64（超级机器人大战 64 原生重编译版）· Nintendo Switch 版（实验性）
Marchwind64, Nintendo Switch edition (experimental). English below.

本程序只供你自己使用，不含 ROM。游戏文字与图片在第一次启动时从你自己的 ROM 导入。请勿转发。

需要
  已安装自制系统（Atmosphère）的 Switch，以及 hbmenu。
  只支持应用模式：按住 R 启动任意一个游戏（实体卡带最省事），进入 hbmenu 后再选 Marchwind64。
  从相簿进入的 hbmenu 内存不够，游戏会提示你改用上面的方式。

安装
  1. 把这个 zip 解压到 SD 卡根目录：得到 /switch/Marchwind64/Marchwind64.nro 和旁边的几个文件夹。
  2. 把 ROM（日版 Rev 0，任意字节序）命名为 rom.z64，放进 /switch/Marchwind64/。
  3. 可选：把 Switch 版 HD 包（Marchwind64-HD-<版本>-switch.zip）也解压到 SD 卡根目录，
     得到 /switch/Marchwind64/hd/。HD 包是单独下载的，桌面版的 HD 包在 Switch 上不能用。
  4. 按住 R 启动任意游戏进入 hbmenu，选 Marchwind64。

第一次启动
  会先从 ROM 导入游戏数据，约半分钟，屏幕上有进度。导入的数据放在 /switch/Marchwind64/game/，
  之后每次启动只要几秒。换新版本时会自动重新导入；那时 rom.z64 可以不在，程序会用 game/ 里的那份。

存档与设置
  都在 /switch/Marchwind64/ 里（saves/、presentation.json 等）。更新时只替换 Marchwind64.nro
  和 fonts/、dialogue/、stages/、splash/ 这几个文件夹，存档不受影响。

问题反馈：https://github.com/dyzz/srw64-recomp/issues

------------------------------------------------------------------------------------------

This program is for your own use and contains no ROM. The game's text and pictures are
imported from your own ROM on the first launch. Please do not redistribute it.

Requirements
  A Switch running custom firmware (Atmosphère), with hbmenu.
  Application mode only: hold R while starting any game (a game card is easiest), then pick
  Marchwind64 in hbmenu. hbmenu opened from the Album has too little memory; the game tells you
  to start it the other way.

Installing
  1. Extract this zip to the root of the SD card: /switch/Marchwind64/Marchwind64.nro and the
     folders beside it.
  2. Name the ROM (Japan, Rev 0, any byte order) rom.z64 and put it in /switch/Marchwind64/.
  3. Optional: extract the Switch HD pack (Marchwind64-HD-<version>-switch.zip) to the SD
     card's root too, giving /switch/Marchwind64/hd/. It is a download of its own: the desktop
     HD pack does not work on the Switch.
  4. Hold R while starting any game to open hbmenu, and pick Marchwind64.

The first launch
  The game data is imported from the ROM first, in about half a minute, with its progress on
  screen. It goes into /switch/Marchwind64/game/, and later launches take a few seconds. A new
  version imports again by itself; rom.z64 need not be there then, the copy in game/ serves.

Saves and settings
  All in /switch/Marchwind64/ (saves/, presentation.json and so on). To update, replace
  Marchwind64.nro and the fonts/, dialogue/, stages/ and splash/ folders; the saves stay.

Feedback: https://github.com/dyzz/srw64-recomp/issues
