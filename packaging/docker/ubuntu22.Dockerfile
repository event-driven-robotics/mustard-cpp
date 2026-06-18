FROM ubuntu:22.04

ARG OPENEB_VERSION=5.2.0

ENV DEBIAN_FRONTEND=noninteractive
ENV CMAKE_GENERATOR=Ninja
ENV LIBGL_ALWAYS_SOFTWARE=1

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    apt-utils \
    ca-certificates \
    build-essential \
    cmake \
    ninja-build \
    git \
    git-lfs \
    gdb \
    pkg-config \
    curl \
    wget \
    unzip \
    file \
    software-properties-common \
    desktop-file-utils \
    appstream \
    squashfs-tools \
    libfuse2 \
    libgl1 \
    libglx0 \
    libglx-mesa0 \
    libgl1-mesa-dri \
    mesa-utils \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    libopencv-dev \
    libboost-all-dev \
    libusb-1.0-0-dev \
    libprotobuf-dev \
    protobuf-compiler \
    libhdf5-dev \
    hdf5-tools \
    libglew-dev \
    libglfw3-dev \
    libcanberra-gtk-module \
    libx11-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxi-dev \
    libxext-dev \
    libwayland-dev \
    libxkbcommon-dev \
    wayland-protocols \
    xvfb \
    ffmpeg \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    libswresample-dev \
    librsvg2-dev \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 --branch "${OPENEB_VERSION}" https://github.com/prophesee-ai/openeb.git /tmp/openeb \
    && cmake -S /tmp/openeb -B /tmp/openeb/build \
        -DBUILD_TESTING=OFF \
        -DCOMPILE_PYTHON3_BINDINGS=OFF \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --build /tmp/openeb/build --parallel "$(nproc)" \
    && cmake --build /tmp/openeb/build --target install \
    && ldconfig \
    && rm -rf /tmp/openeb

RUN git lfs install --system

WORKDIR /workspace

CMD ["./scripts/make_appimage.sh"]
