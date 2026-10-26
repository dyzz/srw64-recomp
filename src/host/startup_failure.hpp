#pragma once
// What a player sees when the game cannot draw at all: no window, or RT64 found neither a
// D3D12 nor a Vulkan device it can use (graphics.cpp). In the saved language (zh-Hans,
// zh-Hant, en or ja; anything else gets Simplified Chinese, as play_lock_message). `gpus` are the
// adapters gpu_report found, `log` the run's console.log.
#include <algorithm>
#include <string>
#include <vector>

namespace srw64::startup_failure {
// Android: an Adreno 6xx whose driver (Android 9 and 10) crashes compiling the game's
// shaders (rt64_android_patches.py). Newer drivers, which come with Android 11, work.
inline std::string old_android_driver(const std::string& locale) {
    if (locale == "ja")
        return "Marchwind64 を起動できません。この端末のグラフィックドライバーは古く、ゲーム画面のシェーダーを作る途中で強制終了してしまいます。\n"
               "Android 11 以降にアップデートしてから、もう一度起動してください。同じ機種でも、アップデート後は動作しています。";
    if (locale == "en")
        return "Unable to start Marchwind64: this phone's graphics driver is too old and crashes while building the game's shaders.\n"
               "Update to Android 11 or later and start the game again. The same phones run it after the update.";
    if (locale == "zh-Hant")
        return "無法啟動 Marchwind64：這台手機的顯示卡驅動程式太舊，在產生遊戲畫面所需的著色器時會當機。\n"
               "請把系統更新到 Android 11 或更新版本，然後重新啟動。同款手機更新系統後可以正常執行。";
    return "无法启动 Marchwind64：这台手机的显卡驱动太旧，在生成游戏画面所需的着色器时会崩溃。\n"
           "请把系统升级到安卓 11 或更高版本，然后重新启动。同款手机升级系统后可以正常运行。";
}

inline std::string message(const std::string& locale, const std::vector<std::string>& gpus,
                           const std::string& log, const std::string& detail) {
    // An adapter can be listed twice (a second output, a virtual display): each name once.
    std::vector<std::string> seen;
    std::string names;
    for (const auto& gpu : gpus) {
        if (std::find(seen.begin(), seen.end(), gpu) != seen.end()) continue;
        seen.push_back(gpu);
        names += (names.empty() ? "" : ", ") + gpu;
    }
    std::string text;
    if (locale == "ja") {
        text = "Marchwind64 を起動できません。このパソコンのグラフィックドライバーではゲーム画面を描画できません。\n"
               "Marchwind64 には DirectX 12 または Vulkan が必要です。GPU メーカー（Intel、AMD、NVIDIA）の公式サイトから"
               "最新のドライバーをインストールして、もう一度起動してください。Windows Update やパソコンメーカーのドライバーには、"
               "これらが含まれていないことがあります。\n";
        if (!names.empty()) text += "\nGPU：" + names;
        if (!detail.empty()) text += "\nエラー：" + detail;
        if (!log.empty()) text += "\n\n報告の際はこのファイルを添付してください：\n" + log;
        return text;
    }
    if (locale == "en") {
        text = "Unable to start Marchwind64: this PC's graphics driver cannot draw the game.\n"
               "Marchwind64 needs DirectX 12 or Vulkan. Install the latest driver from your GPU maker's website "
               "(Intel, AMD or NVIDIA) and start the game again. Drivers from Windows Update or the PC's maker "
               "sometimes leave these parts out.\n";
        if (!names.empty()) text += "\nGPU: " + names;
        if (!detail.empty()) text += "\nError: " + detail;
        if (!log.empty()) text += "\n\nIf you report this, please attach this file:\n" + log;
        return text;
    }
    if (locale == "zh-Hant") {
        text = "無法啟動 Marchwind64：這台電腦的顯示卡驅動程式無法繪製遊戲畫面。\n"
               "Marchwind64 需要 DirectX 12 或 Vulkan。請到顯示卡廠商（Intel、AMD 或 NVIDIA）官網下載並安裝最新驅動程式，"
               "然後重新啟動。Windows Update 或電腦廠商提供的驅動程式有時缺少這些部分。\n";
        if (!names.empty()) text += "\n顯示卡：" + names;
        if (!detail.empty()) text += "\n錯誤：" + detail;
        if (!log.empty()) text += "\n\n回報問題時請附上這個檔案：\n" + log;
        return text;
    }
    text ="无法启动 Marchwind64：这台电脑的显卡驱动无法绘制游戏画面。\n"
           "Marchwind64 需要 DirectX 12 或 Vulkan。请到显卡厂商（Intel、AMD 或 NVIDIA）官网下载并安装最新驱动，"
           "然后重新启动。Windows 更新或电脑厂商提供的驱动有时缺少这些部分。\n";
    if (!names.empty()) text += "\n显卡：" + names;
    if (!detail.empty()) text += "\n错误：" + detail;
    if (!log.empty()) text += "\n\n反馈问题时请附上这个文件：\n" + log;
    return text;
}
}
