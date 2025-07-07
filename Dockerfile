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

# Python
WORKDIR /odin/jungfrau-detector/python

FROM ghcr.io/odin-detector/odin-data-runtime:1.11.0 AS runtime

COPY --from=build /odin /odin
COPY --from=build /venv /venv
COPY deploy /odin/jungfrau-deploy

RUN rm -rf /odin/jungfrau-detector

ENV PATH=/odin/bin:/odin/venv/bin:$PATH

WORKDIR /odin

CMD ["sh", "-c", "cd /odin/jungfrau-deploy && zellij --layout ./layout.kdl"]
