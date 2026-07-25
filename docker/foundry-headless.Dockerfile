FROM ubuntu:24.04

ARG FOUNDRY_VERSION
ARG FOUNDRY_REVISION

LABEL org.opencontainers.image.title="Foundry" \
      org.opencontainers.image.description="Foundry editor CLI for headless CI and Foundry Script tooling" \
      org.opencontainers.image.source="https://github.com/cafecito-games/Foundry" \
      org.opencontainers.image.documentation="https://github.com/cafecito-games/Foundry#headless-container" \
      org.opencontainers.image.licenses="MIT" \
      org.opencontainers.image.version="${FOUNDRY_VERSION}" \
      org.opencontainers.image.revision="${FOUNDRY_REVISION}"

RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates \
      libfontconfig1 \
      libfreetype6 \
      libgl1 \
      libpulse0 \
      libxcursor1 \
      libxi6 \
      libxinerama1 \
      libxrandr2 && \
    rm -rf /var/lib/apt/lists/* && \
    groupadd --gid 10001 foundry && \
    useradd --uid 10001 --gid 10001 --create-home --home-dir /home/foundry foundry && \
    mkdir -p /home/foundry/.config /home/foundry/.cache /home/foundry/.local/share /workspace && \
    chown -R 10001:10001 /home/foundry /workspace

COPY --chown=10001:10001 --chmod=0755 foundry.linuxbsd.editor.x86_64 /usr/local/bin/foundry

ENV HOME=/home/foundry \
    XDG_CONFIG_HOME=/home/foundry/.config \
    XDG_CACHE_HOME=/home/foundry/.cache \
    XDG_DATA_HOME=/home/foundry/.local/share

WORKDIR /workspace
USER 10001:10001

ENTRYPOINT ["/usr/local/bin/foundry", "--headless"]
CMD ["--help"]
