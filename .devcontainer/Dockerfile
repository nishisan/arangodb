# [Choice] Ubuntu version (use ubuntu-22.04 or ubuntu-18.04 on local arm64/Apple Silicon): ubuntu-22.04, ubuntu-20.04, ubuntu-18.04
ARG VARIANT=ubuntu-20.04
FROM mcr.microsoft.com/vscode/devcontainers/base:0-${VARIANT}

# Remove listas de repositórios antigas
RUN rm -rf /etc/apt/sources.list.d

# Adiciona novas listas de repositórios
RUN echo "deb     http://http.debian.net/debian testing main contrib"  > /etc/apt/sources.list
RUN echo "deb-src http://http.debian.net/debian testing main contrib" >> /etc/apt/sources.list

# Adiciona a chave GPG
RUN apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 0E98404D386FA1D9 6ED0E7B82643E131

# Atualiza e instala pacotes
RUN apt update && apt install -y \
    git-core \
    build-essential \
    libssl-dev \
    libjemalloc-dev \
    cmake
    
# [Optional] Uncomment this section to install additional OS packages.
# RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
#     && apt-get -y install --no-install-recommends <your-package-list-here>
