FROM ghcr.io/odin-detector/odin-data-build:1.11.0 AS developer

FROM developer AS build

# Root of jungfrau-detector
COPY . /odin/jungfrau-detector

# C++
WORKDIR /odin/jungfrau-detector
RUN mkdir -p build && cd build && \
    cmake -DCMAKE_INSTALL_PREFIX=/odin -DODINDATA_ROOT_DIR=/odin ../cpp && \
    make -j8 VERBOSE=1 && \
    make install

# slsDetectorPackage

# Set environment variables
ENV PROGRAM=slsDetectorPackage
ENV VERSION=9.2.0
ENV BUILD_DIR="/tmp/build"
ENV SRC="${BUILD_DIR}/src/${PROGRAM}-${VERSION}"
ENV TARGET_PREFIX="/slsDetector"

RUN mkdir ${BUILD_DIR}
WORKDIR ${BUILD_DIR}

# Create directories
RUN mkdir -p tar-files ${SRC} ${TARGET_PREFIX}

# Download and extract the release
RUN curl -L https://github.com/slsdetectorgroup/${PROGRAM}/archive/refs/tags/${VERSION}.tar.gz -o tar-files/${VERSION}.tar.gz && \
    tar -xzf tar-files/${VERSION}.tar.gz -C ${BUILD_DIR}/src && \
    rm tar-files/${VERSION}.tar.gz

# Patch cmk.sh to allow CMAKE override
RUN sed -i 's|CMAKE="cmake3"|CMAKE="${CMAKE:-cmake}"|' ${SRC}/cmk.sh

# Set compiler and cmake environment variables
ENV CC=gcc
ENV CXX=g++
ENV CMAKE=cmake

# Build using the SLS cmake script
WORKDIR ${SRC}
RUN bash ${SRC}/cmk.sh -bsj$(nproc) -l${TARGET_PREFIX}

FROM ghcr.io/odin-detector/odin-data-runtime:1.11.0 AS runtime

COPY --from=build /odin /odin
COPY --from=build /venv /venv
COPY --from=build /slsDetector /slsDetector
#COPY deploy /odin/jungfrau-deploy

RUN rm -rf /odin/jungfrau-detector

ENV PATH=/odin/bin:/odin/venv/bin:$PATH

WORKDIR /odin

