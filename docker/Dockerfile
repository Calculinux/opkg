FROM debian:12

RUN apt-get clean && \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
    cmake \
    git \
    libacl1-dev \
    libarchive-dev \
    libbrotli-dev \
    libbz2-dev \
    libcurl4-openssl-dev \
    libkrb5-dev \
    libgpgme11-dev \
    libldap-dev \
    liblzma-dev \
    liblz4-dev \
    libnghttp2-dev \
    libpsl-dev \
    librtmp-dev \
    libssh-dev \
    libssh2-1-dev \
    libssl-dev \
    libtool-bin \
    libzstd-dev \
    make \
    pkg-config \
    python3 \
    vim

RUN git clone https://github.com/openSUSE/libsolv.git /usr/local/src/libsolv && \
    cd /usr/local/src/libsolv && \
    mkdir build && cd build && \
    cmake \
       -DCMAKE_INSTALL_PREFIX="/usr/local/" \
       -DENABLE_COMPLEX_DEPS=on \
       -DMULTI_SEMANTICS=on \
       .. && \
    make install && \
    ldconfig

RUN mkdir /usr/local/src/opkg
WORKDIR /usr/local/src/opkg

CMD ["/bin/bash"]
