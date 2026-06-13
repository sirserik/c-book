# Многоступенчатый Dockerfile для chat-server.
# Stage 1: builder на alpine с musl — компилируем статически.
# Stage 2: scratch (пустой образ) + только бинарь.
# Итог: < 5 MB образ, никаких зависимостей.

FROM alpine:3.19 AS builder
RUN apk add --no-cache build-base make
WORKDIR /src
COPY . .
RUN make MODE=release CXX=g++ \
    CXXFLAGS="-std=c++11 -O2 -DNDEBUG -static -static-libstdc++ -static-libgcc -pthread -Iinclude -MMD -MP" \
    LDFLAGS="-static -pthread"

FROM scratch
COPY --from=builder /src/build/rooms_chat_server /chat-server
EXPOSE 9001
ENTRYPOINT ["/chat-server", "9001"]
