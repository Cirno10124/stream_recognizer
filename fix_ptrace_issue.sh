#!/bin/bash

# 解决ptrace_scope问题的脚本
# 这个问题通常出现在Linux系统中，当调试器尝试附加到进程时

echo "检查当前ptrace_scope设置..."
current_value=$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo "文件不存在")
echo "当前ptrace_scope值: $current_value"

echo ""
echo "ptrace_scope值的含义："
echo "0 - 经典ptrace权限：进程可以PTRACE_ATTACH到任何其他进程"
echo "1 - 受限ptrace：进程只能ptrace其子进程或具有CAP_SYS_PTRACE能力的进程"
echo "2 - 仅管理员ptrace：只有具有CAP_SYS_PTRACE能力的进程可以使用ptrace"
echo "3 - 禁用ptrace：没有进程可以使用ptrace"

echo ""
echo "解决方案："
echo "1. 临时解决（重启后失效）："
echo "   sudo sysctl kernel.yama.ptrace_scope=0"
echo ""
echo "2. 永久解决："
echo "   echo 'kernel.yama.ptrace_scope = 0' | sudo tee -a /etc/sysctl.conf"
echo "   sudo sysctl -p"
echo ""
echo "3. 仅对当前用户临时解决："
echo "   echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope"

echo ""
echo "注意：修改ptrace_scope可能会降低系统安全性。"
echo "建议仅在开发环境中使用，生产环境请谨慎考虑。"

echo ""
read -p "是否要临时设置ptrace_scope为0？(y/N): " answer
if [[ $answer == [Yy]* ]]; then
    echo "正在设置ptrace_scope为0..."
    if sudo sysctl kernel.yama.ptrace_scope=0; then
        echo "设置成功！"
        echo "新的ptrace_scope值: $(cat /proc/sys/kernel/yama/ptrace_scope)"
    else
        echo "设置失败，请检查权限。"
    fi
else
    echo "未进行任何更改。"
fi

echo ""
echo "如果问题仍然存在，请检查："
echo "1. 是否有其他安全模块（如SELinux、AppArmor）在阻止操作"
echo "2. 是否需要以root权限运行程序"
echo "3. 程序是否在容器或虚拟环境中运行" 