#!/usr/bin/env python3
"""修正 CMake 生成的 SES 工程中的本机绝对路径，使其可随仓库提交并在其他机器使用。

由 CMake 在 configure 阶段自动调用（见 CMakeLists.txt）：
  1. 调试命令改为工程内相对路径（tools/openocd + hpm_sdk 配置）
  2. 路径分隔符统一为 '/'

SES 工程保留在官方生成位置 build/<build_type>/segger_embedded_studio/。
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
BUILD = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build" / "hpm5100evk_flash_xip"
GEN = BUILD / "segger_embedded_studio"

GDB_LINE = (
    'gdb_server_command_line="&quot;$(ProjectDir)/../../../tools/openocd/openocd.exe&quot;'
    ' -s &quot;$(ProjectDir)/../../../tools/openocd/tcl&quot;'
    ' -f &quot;$(ProjectDir)/../../../hpm_sdk/boards/openocd/probes/cmsis_dap.cfg&quot;'
    ' -f &quot;$(ProjectDir)/../../../hpm_sdk/boards/openocd/soc/hpm5100.cfg&quot;'
    ' -f &quot;$(ProjectDir)/../../../hpm_sdk/boards/openocd/boards/hpm5100evk.cfg&quot;"'
)


def main() -> None:
    src_file = GEN / "SuperCapacitor.emProject"
    if not src_file.exists():
        sys.exit(f"missing {src_file}, run 'cmake --preset flash_xip' first")

    text = src_file.read_text(encoding="utf-8")
    text = re.sub(r'gdb_server_command_line="[^"]*"', GDB_LINE, text)
    text = text.replace(chr(92), "/")
    src_file.write_text(text, encoding="utf-8")
    print(f"fixed {src_file} (relative openocd/config paths)")


if __name__ == "__main__":
    main()
