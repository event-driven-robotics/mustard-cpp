FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    # Core build tools
    build-essential \
    cmake \
    ninja-build \
    git \
    pkg-config \
    curl \
    # OpenGL — Mesa software renderer (no GPU required)
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    # X11 dependencies required by GLFW when built from source via FetchContent
    libx11-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxi-dev \
    libxext-dev \
    # Wayland dependencies (optional GLFW backend)
    libwayland-dev \
    libxkbcommon-dev \
    wayland-protocols \
    # Virtual framebuffer for headless testing (CI / no display)
    xvfb \
    && rm -rf /var/lib/apt/lists/*

# Use Mesa software rasterizer by default — no GPU passthrough required.
# Override with LIBGL_ALWAYS_SOFTWARE=0 and --device /dev/dri to use the host GPU.
ENV LIBGL_ALWAYS_SOFTWARE=1

WORKDIR /workspace
