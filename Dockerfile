FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    make \
    git \
    zlib1g-dev \
    libssl-dev \
    libgtest-dev \
    valgrind \
    binutils \
    linux-tools-common \
    cpplint \
    python3 \
    python3-pip \
    && pip3 install --no-cache-dir flask \
    && rm -rf /var/lib/apt/lists/*

# Build and install gtest from source
RUN cd /usr/src/googletest && cmake . && make && \
    find . -name "*.a" -exec cp {} /usr/lib/ \; || \
    (cd /usr/src/gtest && cmake . && make && find . -name "*.a" -exec cp {} /usr/lib/ \;) || true

WORKDIR /workspace

COPY . /workspace/

RUN mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)

# Web GUI (EX-09): Flask serves the console on port 8080
ENV CBACKUP_BIN=/workspace/build/cbackup
EXPOSE 8080

# Default: launch the Web GUI. Override with `docker run ... <args>` to use CLI.
ENTRYPOINT ["python3", "/workspace/gui/app.py"]
