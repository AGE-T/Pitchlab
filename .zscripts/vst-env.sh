# Task 32 local build environment (user-prefix xcb/xkbcommon deps +
# pinned CMake 3.31.6 + ninja from the venv)
export PATH="$HOME/.local/cmake-3.31.6-linux-x86_64/bin:$HOME/.venv/bin:$PATH"
export PKG_CONFIG_PATH="$HOME/.local/xcbdeps/lib/x86_64-linux-gnu/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$HOME/.local/xcbdeps/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
