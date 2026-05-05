#!/bin/bash
echo "TORK API 通道设置"
echo "=================="
echo ""
echo "输入你的 DeepSeek API Key（输入后按回车）："
read -s api_key
echo ""
export DEEPSEEK_API_KEY="$api_key"
if ! grep -q "DEEPSEEK_API_KEY" ~/.bashrc; then
    echo "export DEEPSEEK_API_KEY='$api_key'" >> ~/.bashrc
fi
echo "API Key 已设置。"
echo "现在可以运行 TORK 桌面程序：python3 /home/lg/0EGG/tork_desktop.py"
