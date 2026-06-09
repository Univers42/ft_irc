# ─────────────────────────────────────────────────────────────────────────────
#  ircserv — the C++98 IRC server (full tier).
#
#  Multi-stage: a build image with the toolchain, a slim runtime image with
#  just the binary. The vendored libcpp/googletest submodules must be checked
#  out in the build context (they are part of the repo working tree).
#
#  Targets:
#    (default)  -> runtime image, ENTRYPOINT ./ircserv
#    --target test  -> runs the 138-assertion Google Test suite at build time
# ─────────────────────────────────────────────────────────────────────────────

# ---- build ----------------------------------------------------------------
FROM debian:bookworm-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ make ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
# Full tier (default `make`): bonus + platform extras, extras runtime-gated.
RUN make re

# ---- test (optional: docker build --target test) --------------------------
FROM build AS test
RUN make test

# ---- runtime --------------------------------------------------------------
FROM debian:bookworm-slim AS runtime
RUN apt-get update \
 && apt-get install -y --no-install-recommends libstdc++6 \
 && rm -rf /var/lib/apt/lists/* \
 && useradd -r -u 10001 ircd
WORKDIR /app
COPY --from=build /src/ircserv /app/ircserv
USER ircd
EXPOSE 6667
# Override the password at run time:  docker run ... ircserv 6667 <password>
ENTRYPOINT ["/app/ircserv"]
CMD ["6667", "changeme"]
