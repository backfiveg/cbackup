FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    make \
    git \
    zlib1g-dev \
    libgtest-dev \
    valgrind \
    binutils \
    linux-tools-common \
    cpplint \
    && rm -rf /var/lib/apt/lists/*

# Build and install gtest from source
RUN cd /usr/src/googletest && cmake . && make && \
    find . -name "*.a" -exec cp {} /usr/lib/ \; || \
    (cd /usr/src/gtest && cmake . && make && find . -name "*.a" -exec cp {} /usr/lib/ \;) || true

WORKDIR /workspace

COPY . /workspace/

RUN mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)

ENTRYPOINT ["/workspace/build/cbackup"]
CMD ["--help"]
