# Build larch-usher from source

FROM ubuntu:22.04

RUN apt -y update \
  && apt -y upgrade

# OS Programs
RUN apt -y install --no-install-recommends \
  wget \
  ssh \
  git \
  vim \
  nano \
  perl \
  black \
  clang-format \
  clang-tidy \
  help2man \
  less

# Build Programs
RUN apt -y install --no-install-recommends \
  cmake \
  protobuf-compiler \
  automake \
  autoconf \
  libtool \
  nasm \
  yasm

# Ognian Programs
RUN apt -y install --no-install-recommends \
  wget \
  git \
  ca-certificates \
  make \
  g++ \
  mpi-default-dev \
  libboost-dev \
  libboost-program-options-dev \
  libboost-filesystem-dev \
  libboost-date-time-dev \
  libboost-iostreams-dev

WORKDIR /larch
COPY . /larch

# WORKDIR /larch/build
# RUN cmake -DCMAKE_BUILD_TYPE=Debug ..
# RUN make -j16

WORKDIR /data

