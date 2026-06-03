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
    # FFmpeg — video decode for RGBVideoPanel (MP4 / H.264 playback)
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    libswresample-dev \
    # FFmpeg runtime codec/muxer dependencies (pulled in transitively at runtime)
    libvpx-dev \
    libx264-dev \
    libx265-dev \
    libmp3lame-dev \
    libopus-dev \
    libvorbis-dev \
    libogg-dev \
    libtheora-dev \
    libspeex-dev \
    libgsm1-dev \
    libtwolame-dev \
    libxvidcore-dev \
    libshine-dev \
    libcodec2-dev \
    libdav1d-dev \
    librav1e-dev \
    libsvtav1-dev \
    libopenjp2-7-dev \
    libjxl-dev \
    libsnappy-dev \
    libwebp-dev \
    libzvbi-dev \
    # FFmpeg network/protocol libraries
    libchromaprint-dev \
    libbluray-dev \
    librabbitmq-dev \
    librist-dev \
    libsrt-openssl-dev \
    libssh-dev \
    libzmq3-dev \
    # FFmpeg hardware acceleration / utility dependencies
    libva-dev \
    libvdpau-dev \
    libvpl-dev \
    libgme-dev \
    libopenmpt-dev \
    libnuma-dev \
    libsoxr-dev \
    libsodium-dev \
    # Cairo / Pango / GDK-Pixbuf (pulled in by rsvg codec support)
    libcairo2-dev \
    librsvg2-dev \
    libgdk-pixbuf-2.0-dev \
    libpango1.0-dev \
    && rm -rf /var/lib/apt/lists/*

# Use Mesa software rasterizer by default — no GPU passthrough required.
# Override with LIBGL_ALWAYS_SOFTWARE=0 and --device /dev/dri to use the host GPU.
ENV LIBGL_ALWAYS_SOFTWARE=1

WORKDIR /workspace
