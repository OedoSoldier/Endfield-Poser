@echo off
setlocal EnableExtensions
title Endfield Poser 安全管理向导 (安装 / 卸载)

echo ======================================================================
echo             Endfield Poser 安全管理向导 (安装 / 卸载)
echo 发布页请访问【 https://github.com/honxi1/Endfield-Poser/releases 】
echo 本向导将引导您安全地安装或卸载 Endfield Poser 摆姿插件。
echo 运行过程中会自动核验游戏目录特征，并确保原版 DLL 的备份与还原安全。
echo.

:prompt_input
echo ----------------------------------------------------------------------
echo 请输入游戏根目录路径（可以直接将游戏根目录文件夹拖入此窗口）：
echo 示例：D:\APPS\Hypergryph Launcher\games\Endfield Game
echo ----------------------------------------------------------------------
set "TARGET_DIR="
set /p "TARGET_DIR=> "

REM 检查是否输入为空
if not defined TARGET_DIR (
    echo.
    echo [错误] 输入不能为空，请重新输入！
    echo.
    goto prompt_input
)

REM 去除首尾双引号
set "TARGET_DIR=%TARGET_DIR:"=%"

REM 循环去除末尾空格
:trim_space
if "%TARGET_DIR:~-1%#"==" #" (
    set "TARGET_DIR=%TARGET_DIR:~0,-1%"
    goto trim_space
)

REM 循环去除末尾反斜杠
:trim_slash
if "%TARGET_DIR:~-1%#"=="\#" (
    set "TARGET_DIR=%TARGET_DIR:~0,-1%"
    goto trim_slash
)

if "%TARGET_DIR%"=="" (
    echo.
    echo [错误] 输入路径无效，请重新输入！
    echo.
    goto prompt_input
)

echo.
echo 正在检查目标目录：%TARGET_DIR%
echo ----------------------------------------------------------------------

REM 1. 检查目录是否存在
if not exist "%TARGET_DIR%" (
    echo [错误] 指定的文件夹不存在，请检查路径是否输入正确。
    goto error_exit
)

REM 2. 检查游戏目录特征 (Endfield.exe 与 plugins 文件夹)
if not exist "%TARGET_DIR%\Endfield.exe" (
    echo [错误] 在该目录下未找到 Endfield.exe！
    echo        请确保选择的是包含游戏主程序的根目录，而不是启动器目录。
    goto error_exit
)

if not exist "%TARGET_DIR%\plugins" (
    echo [错误] 在该目录下未找到游戏自带的 plugins 文件夹！
    echo        请确认这是否是完整的游戏安装目录。
    goto error_exit
)

REM 3. 检查是否已经安装过本工具 (特征为存在 plugin 文件夹或旧备份)
set "ALREADY_INSTALLED=0"
if exist "%TARGET_DIR%\plugin" set "ALREADY_INSTALLED=1"
if exist "%TARGET_DIR%\d3dcompiler_47.dll.backup" set "ALREADY_INSTALLED=1"
if exist "%TARGET_DIR%\vulkan-1.dll.backup" set "ALREADY_INSTALLED=1"

if "%ALREADY_INSTALLED%"=="1" (
    echo.
    echo ======================================================================
    echo 【检测到已安装】目标游戏目录中已存在 Endfield Poser 插件或原版备份！
    echo ★ 关于【更新插件】：
    echo   如果您想要更新此插件，需要先【卸载旧版】；
    echo   卸载完成后，【重新双击运行此脚本】即可完成新版本的全新安装。
    echo.
    echo 是否现在卸载已有的 Endfield Poser 并将游戏还原为原版？
    echo.
    goto ask_uninstall
)

REM ===================== 以下为全新安装流程 =====================

echo [1/3] 目标目录特征检查通过：
echo        - 确认存在 Endfield.exe
echo        - 确认存在 plugins 文件夹
echo        - 确认当前未安装任何旧版插件
echo.

REM 4. 检查当前安装包文件是否齐全
if not exist "%~dp0d3dcompiler_47.dll" (
    echo [错误] 安装包内缺失 d3dcompiler_47.dll，请检查安装包完整性！
    goto error_exit
)
if not exist "%~dp0plugin" (
    echo [错误] 安装包内缺失 plugin 文件夹，请检查安装包完整性！
    goto error_exit
)

REM 5. 备份原版文件
echo [2/3] 正在安全备份原版 DLL 文件...

if exist "%TARGET_DIR%\d3dcompiler_47.dll" (
    ren "%TARGET_DIR%\d3dcompiler_47.dll" "d3dcompiler_47.dll.backup"
    if errorlevel 1 (
        echo [错误] 备份 d3dcompiler_47.dll 失败！请检查文件是否被占用（请先退出游戏和启动器）。
        goto error_exit
    )
    echo  - 成功：已将原版 d3dcompiler_47.dll 重命名备份为 d3dcompiler_47.dll.backup
) else (
    echo  - 提示：原游戏目录下未检测到 d3dcompiler_47.dll，跳过备份。
)

if exist "%TARGET_DIR%\vulkan-1.dll" (
    ren "%TARGET_DIR%\vulkan-1.dll" "vulkan-1.dll.backup"
    if errorlevel 1 (
        echo [错误] 备份 vulkan-1.dll 失败！请检查文件是否被占用（请先退出游戏和启动器）。
        goto error_exit
    )
    echo  - 成功：已将原版 vulkan-1.dll 重命名备份为 vulkan-1.dll.backup
) else (
    echo  - 提示：原游戏目录下未检测到 vulkan-1.dll（原版可能无此文件），跳过备份。
)
echo.

REM 6. 复制要安装的文件
echo [3/3] 正在复制插件文件到游戏目录...

copy /Y "%~dp0d3dcompiler_47.dll" "%TARGET_DIR%\" >nul
if errorlevel 1 (
    echo [错误] 复制 d3dcompiler_47.dll 失败！
    goto error_exit
)
echo  - 已复制 d3dcompiler_47.dll

if exist "%~dp0vulkan-1.dll" (
    copy /Y "%~dp0vulkan-1.dll" "%TARGET_DIR%\" >nul
    if errorlevel 1 (
        echo [错误] 复制 vulkan-1.dll 失败！
        goto error_exit
    )
    echo  - 已复制 vulkan-1.dll
)

xcopy "%~dp0plugin" "%TARGET_DIR%\plugin\" /E /I /Y >nul
if errorlevel 1 (
    echo [错误] 复制 plugin 文件夹失败！
    goto error_exit
)
echo  - 已复制 plugin 文件夹及插件配置

echo.
echo ======================================================================
echo                         安装完成！
echo Endfield Poser 已成功安全安装至游戏目录：
echo %TARGET_DIR%
echo.

echo                  【关键使用说明】
echo ======================================================================
echo.
echo 1.【如何启动游戏】：
echo    ★ 必须通过【官方启动器（Hypergryph Launcher）】启动游戏！
echo    ★ 不可直接双击运行 Endfield.exe，否则会在 Unity 初始化阶段崩溃！
echo.
echo 2.【重要快捷键】：
echo    ★ F12 ：呼出 / 隐藏摆姿面板（GUI 控制窗口）
echo    ★ F11 ：冻结 / 解冻当前正在操作的角色
echo    ★ 按住 Alt 键 ：将鼠标指针控制权临时交给插件面板进行点击和调节数值；
echo      松开 Alt 后鼠标与键盘立即完全交还给游戏操作。
echo.
echo 3.【GUI 界面与数据在哪里】：
echo    ★ GUI 界面为透明悬浮窗口，进入游戏并加载角色后按 F12 即可看到。
echo    ★ 姿势预设保存路径：%TARGET_DIR%\plugin\poses\
echo    ★ 运行排查日志文件：%TARGET_DIR%\plugin\poser_log.txt
echo.
echo 4.【后续更新或卸载】：
echo    如需更新或卸载，重新双击运行本脚本即可一键卸载旧版，并还原官方文件。
echo    新版发布页：【 https://github.com/honxi1/Endfield-Poser/releases 】
echo.
echo 请确认已仔细阅读以上使用说明。按任意键确认已读并退出向导...
pause >nul
exit /b 0

REM ===================== 以下为卸载与还原流程 =====================

:ask_uninstall
set "UNINSTALL_CHOICE="
set /p "UNINSTALL_CHOICE=请输入 [Y] 卸载还原  /  [N] 保持现状退出: "
if defined UNINSTALL_CHOICE set "UNINSTALL_CHOICE=%UNINSTALL_CHOICE: =%"

if /i "%UNINSTALL_CHOICE%"=="Y" goto do_uninstall
if /i "%UNINSTALL_CHOICE%"=="N" goto cancel_uninstall

echo.
echo [错误] 输入无效，请输入字母 Y 或 N
echo.
goto ask_uninstall

:cancel_uninstall
echo.
echo [操作取消] 未作任何更改，游戏文件保持原状。
echo 按任意键退出向导...
pause >nul
exit /b 0

:do_uninstall
echo.
echo 正在执行卸载并还原游戏文件...
echo ----------------------------------------------------------------------

REM 1. 还原 d3dcompiler_47.dll
if exist "%TARGET_DIR%\d3dcompiler_47.dll.backup" (
    if exist "%TARGET_DIR%\d3dcompiler_47.dll" (
        del /f /q "%TARGET_DIR%\d3dcompiler_47.dll" >nul
    )
    ren "%TARGET_DIR%\d3dcompiler_47.dll.backup" "d3dcompiler_47.dll"
    if errorlevel 1 (
        echo [错误] 还原 d3dcompiler_47.dll 失败！请检查文件是否被占用（请先退出游戏和启动器）。
        goto error_exit
    )
    echo [1/3] 成功：已将 d3dcompiler_47.dll 还原为原版。
) else (
    echo [1/3] 提示：未发现 d3dcompiler_47.dll.backup 备份文件，跳过该项。
)

REM 2. 还原或清理 vulkan-1.dll
if exist "%TARGET_DIR%\vulkan-1.dll.backup" (
    if exist "%TARGET_DIR%\vulkan-1.dll" (
        del /f /q "%TARGET_DIR%\vulkan-1.dll" >nul
    )
    ren "%TARGET_DIR%\vulkan-1.dll.backup" "vulkan-1.dll"
    if errorlevel 1 (
        echo [错误] 还原 vulkan-1.dll 失败！
        goto error_exit
    )
    echo [2/3] 成功：已将 vulkan-1.dll 还原为原版。
) else (
    if exist "%TARGET_DIR%\vulkan-1.dll" (
        del /f /q "%TARGET_DIR%\vulkan-1.dll" >nul
        echo [2/3] 成功：已清理插件的 vulkan-1.dll（原版无此文件）。
    ) else (
        echo [2/3] 提示：无需处理 vulkan-1.dll。
    )
)

REM 3. 清理 plugin 文件夹
if exist "%TARGET_DIR%\plugin" (
    rd /s /q "%TARGET_DIR%\plugin"
    if errorlevel 1 (
        echo [错误] 删除 plugin 文件夹失败，请检查是否有文件被占用。
        goto error_exit
    )
    echo [3/3] 成功：已删除插件 plugin 文件夹。
) else (
    echo [3/3] 提示：未发现 plugin 文件夹。
)

echo.
echo ======================================================================
echo                         卸载与还原完成！
echo ======================================================================
echo 游戏文件已全部恢复为官方原版状态。
echo.
echo ★【更新提示】 https://github.com/honxi1/Endfield-Poser/releases
echo   若要更新为此安装包对应的新版本，现在只需【重新双击运行本脚本】即可！
echo ======================================================================
echo.
echo 按任意键退出向导...
pause >nul
exit /b 0

REM ===================== 错误退出分支 =====================

:error_exit
echo.
echo ======================================================================
echo [操作中断] 未能完成操作，游戏文件已保持原状或未受损。
echo 请根据上方提示排查问题后重新运行本脚本。
echo ======================================================================
echo.
echo 按任意键退出向导...
pause >nul
exit /b 1
