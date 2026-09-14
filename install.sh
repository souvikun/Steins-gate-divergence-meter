#!/bin/bash

echo -e "\033[1;36m>> Compiling FG-204 Divergence Meter...\033[0m"

# Check if g++ is installed
if ! command -v g++ &> /dev/null; then
    echo -e "\033[31mError: g++ is not installed. Please install it first.\033[0m"
    echo "On Debian/Ubuntu/Termux run: apt install g++"
    echo "On Fedora/Bazzite run: rpm-ostree install gcc-c++"
    exit 1
fi

# Compile with optimization
g++ main.cpp -o divergence -O3

if [ $? -ne 0 ]; then
    echo -e "\033[31mCompilation failed!\033[0m"
    exit 1
fi

# Create local bin directory and move the executable
mkdir -p "$HOME/.local/bin"
mv divergence "$HOME/.local/bin/"

# Detect the user's default shell config
SHELL_RC=""
if echo "$SHELL" | grep -q "zsh"; then
    SHELL_RC="$HOME/.zshrc"
elif echo "$SHELL" | grep -q "bash"; then
    SHELL_RC="$HOME/.bashrc"
elif [ -f "$HOME/.bashrc" ]; then
    SHELL_RC="$HOME/.bashrc"
else
    SHELL_RC="$HOME/.profile"
fi

# Ensure ~/.local/bin is in PATH
if ! grep -q 'export PATH="$HOME/.local/bin:$PATH"' "$SHELL_RC"; then
    echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$SHELL_RC"
fi

# Add the auto-start hook using the fast-boot (-f) argument
if ! grep -q "divergence -f" "$SHELL_RC"; then
    echo "" >> "$SHELL_RC"
    echo "# Future Gadget Lab: Divergence Meter Boot" >> "$SHELL_RC"
    echo "divergence -f" >> "$SHELL_RC"
    echo -e "\033[32m>> Added startup hook to $SHELL_RC\033[0m"
else
    echo -e "\033[33m>> Startup hook already exists in $SHELL_RC\033[0m"
fi

echo -e "\033[1;32m>> Installation complete! El Psy Kongroo.\033[0m"
echo -e "Restart your terminal, or run: \033[1m source $SHELL_RC \033[0m"
