FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
      bubblewrap gcc libc6-dev iproute2 util-linux ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY probe.c /probe.c
COPY probe.sh /probe.sh
RUN gcc -O2 -o /probe /probe.c && chmod +x /probe.sh
CMD ["/bin/sh", "/probe.sh"]
