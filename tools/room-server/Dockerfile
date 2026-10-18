# NS HUEHUE: servidor de salas do Eden (eden-room) para o "Jogar online" do emulador.
# Compila o servidor a partir deste repositorio e roda em UDP 24872.
#
#   docker build -f tools/room-server/Dockerfile -t ns-huehue-room .
#   docker run -d --name ns-huehue-room --restart unless-stopped -p 24872:24872/udp ns-huehue-room \
#       --room-name "NS HUEHUE" --port 24872 --max-members 8 --password "troque-esta-senha"
#
# O contexto do build e a raiz do repositorio (o ponto no fim do docker build).
FROM ubuntu:24.04 AS build
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential ninja-build git python3-pip pkg-config ca-certificates libssl-dev libboost-dev \
    && pip install --break-system-packages "cmake>=3.31,<4" \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja -DYUZU_STATIC_ROOM=ON -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target yuzu_room_standalone \
    && mkdir -p /out && cp "$(find build -type f -name eden-room | head -n 1)" /out/eden-room

FROM debian:stable-slim
COPY --from=build /out/eden-room /usr/local/bin/eden-room
EXPOSE 24872/udp
ENTRYPOINT ["/usr/local/bin/eden-room"]
CMD ["--room-name", "NS HUEHUE", "--port", "24872", "--max-members", "8"]
