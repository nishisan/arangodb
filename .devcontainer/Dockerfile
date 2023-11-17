# [Choice] Debian / Ubuntu version (use Debian 12, Debian 11, Ubuntu 22.04 on local arm64/Apple Silicon): debian-12, debian-11, debian-10, ubuntu-22.04, ubuntu-20.04
ARG VARIANT=ubuntu-22.04
FROM mcr.microsoft.com/devcontainers/base:${VARIANT}
USER root

ARG ARCH "x86_64" 
ENV COMPILER_VERSION 12
ENV CLANG_VERSION 16
RUN apt-get update --fix-missing && \
    apt-get install -y software-properties-common && \
    apt-get update && \
    apt-get upgrade -y
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update --fix-missing && apt-get install -y build-essential gcc-${COMPILER_VERSION} g++-${COMPILER_VERSION} cmake make bison flex python3 ccache git libjemalloc-dev vim exuberant-ctags gdb fish psmisc sudo debhelper debconf jq wget libdb-dev curl gnupg2 gcovr prometheus bc tcpdump liburing-dev && apt-get clean

# Baixar o arquivo
# RUN curl -L -o /tmp/sccache.tar.gz https://github.com/mozilla/sccache/releases/download/v0.3.1/sccache-v0.3.1-${ARCH}-unknown-linux-musl.tar.gz
RUN wget -O /tmp/sccache.tar.gz https://github.com/mozilla/sccache/releases/download/v0.3.1/sccache-v0.3.1-x86_64-unknown-linux-musl.tar.gz
 
# Descompactar o arquivo
RUN tar xvzf /tmp/sccache.tar.gz -C /tmp

# Mover o executável sccache para /usr/bin e torná-lo executável
RUN mv /tmp/sccache-v0.3.1-x86_64-unknown-linux-musl/sccache /usr/bin/sccache \
    && chmod +x /usr/bin/sccache

# Limpar arquivos desnecessários
RUN rm -rf /tmp/sccache.tar.gz /tmp/sccache-v0.3.1-x86_64-unknown-linux-musl
RUN chmod +xw /tools/*.sh
COPY ./tools/* /tools/

RUN [ "/tools/install.sh", "3.1", "3" ]
COPY ./scripts /scripts
RUN export LC_ALL="C" && wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add - && add-apt-repository -s "deb http://apt.llvm.org/jammy/ llvm-toolchain-jammy-${CLANG_VERSION} main" && apt install -y clang-${CLANG_VERSION} lldb-${CLANG_VERSION} lld-${CLANG_VERSION} libc++-${CLANG_VERSION}-dev libc++abi-${CLANG_VERSION}-dev libclang-common-${CLANG_VERSION}-dev libclang-rt-${CLANG_VERSION}-dev
RUN update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-${CLANG_VERSION} 100 && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-${CLANG_VERSION} 100
RUN LC_ALL="C" update-ccache-symlinks
RUN git config --global --add safe.directory '*'
RUN apt-get install -y python3-pip && pip3 install py7zr psutil


# Setup ENV vars for vcpkg
ENV VCPKG_ROOT=/usr/local/vcpkg \
    VCPKG_DOWNLOADS=/usr/local/vcpkg-downloads
ENV PATH="${PATH}:${VCPKG_ROOT}"

ARG USERNAME=vscode

# Install vcpkg itself: https://github.com/microsoft/vcpkg/blob/master/README.md#quick-start-unix
COPY base-scripts/install-vcpkg.sh /tmp/
RUN chmod +x /tmp/install-vcpkg.sh \
    && ./tmp/install-vcpkg.sh ${USERNAME} \
    && rm -f /tmp/install-vcpkg.sh

# [Optional] Uncomment this section to install additional OS packages.
# RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
#     && apt-get -y install --no-install-recommends <your-package-list-here>
