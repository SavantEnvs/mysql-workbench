# syntax=docker/dockerfile:1.7
#
# Commit-image Dockerfile (SMALL form, PREFERRED) — builds ON TOP of the org C/C++ base
# image (ghcr.io/savantenvs/base). The base already provides debian + clang/llvm +
# libFuzzer/afl++ + the identities + the build-contract ENV, so this only adds the
# per-commit build (built on top of the prebuilt base, rather than building the toolchain in-image).
#
# Build (from the repo root):
#   DOCKER_BUILDKIT=1 docker build -f mayhem/Dockerfile \
#       --build-arg COMMIT=$(git rev-parse HEAD) \
#       [--build-arg SOURCE_DATE_EPOCH=$(date +%s)] \
#       -t mytag .

# The org base, pinned to a fixed DATE-tagged base (YYYYMMDD; shared by all integrations
# minted against it). Use the current dated tag; pin by digest in production: ...base@sha256:<digest>.
FROM ghcr.io/savantenvs/base:20261023

ARG COMMIT
ARG REPO_URL
# SOURCE_DATE_EPOCH stays an ARG (available to build.sh); do NOT bake it as an ENV — an
# empty value breaks clang ("must be a non-negative decimal integer") both here and for
# any in-image rebuild. build.sh unsets it when empty.
ARG SOURCE_DATE_EPOCH

# Sanitizer override: the base exports SANITIZER_FLAGS (ASan+UBSan, halting) as the default.
# To change it for a build (e.g. ASan-only, or add MSan): --build-arg SANITIZER_FLAGS="...".
# Declare it WITHOUT a default (no '='): an undefaulted ARG lets the base ENV flow through to
# build.sh when not passed, and an explicit --build-arg overrides it. (An empty default — `=` —
# would instead shadow the base ENV and silently wipe the sanitizers.)
ARG SANITIZER_FLAGS
# Coverage (optional, off by default): instrument the TEST build to measure how much of the project
# the suite exercises — --build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping".
# Empty by default (no effect on the normal oracle build); build.sh appends it to the test build only.
ARG COVERAGE_FLAGS=
# Build parallelism (optional). build.sh falls back to nproc when unset: --build-arg MAYHEM_JOBS=8
ARG MAYHEM_JOBS=

# Extra build dependencies: apt -dev packages for the headers the compiled library slices include, plus a
# JRE (the MySQL grammar is turned into C++ at build time by the ANTLR tool) and cmake/curl for the
# one-off runtime build below. Re-pinned to SOURCE_DATE_EPOCH first for reproducibility.
USER root
RUN [ -n "$SOURCE_DATE_EPOCH" ] && repro-sources-list.sh || true ; \
    apt-get update && apt-get install -yq --no-install-recommends \
        ca-certificates curl cmake make pkg-config default-jre-headless \
        libglib2.0-dev libxml2-dev python3-dev libboost-dev uuid-dev rapidjson-dev \
 && rm -rf /var/lib/apt/lists/*

# MySQL Workbench's grammar needs ANTLR 4.13.2 (Debian ships 4.9): the tool jar and the C++ runtime are
# fetched ONCE here, sha256-pinned, and built into a static uninstrumented prefix (SPEC §6.2 item 19).
# build.sh then generates the lexer/parser from the in-tree grammar with this jar, fully offline.
ARG ANTLR_VER=4.13.2
ARG ANTLR_JAR_SHA256=eae2dfa119a64327444672aff63e9ec35a20180dc5b8090b7a6ab85125df4d76
ARG ANTLR_SRC_SHA256=9f18272a9b32b622835a3365f850dd1063d60f5045fb1e12ce475ae6e18a35bb
RUN d=/opt/toolchains/deps/antlr4-${ANTLR_VER} && mkdir -p "$d/src" "$d/lib" /tmp/dep-build \
 && curl -fsSL -o /tmp/antlr.jar https://repo1.maven.org/maven2/org/antlr/antlr4/${ANTLR_VER}/antlr4-${ANTLR_VER}-complete.jar \
 && echo "${ANTLR_JAR_SHA256}  /tmp/antlr.jar" | sha256sum -c - \
 && install -m 0644 /tmp/antlr.jar "$d/lib/antlr-${ANTLR_VER}-complete.jar" \
 && curl -fsSL -o /tmp/antlr-src.tgz https://github.com/antlr/antlr4/archive/refs/tags/${ANTLR_VER}.tar.gz \
 && echo "${ANTLR_SRC_SHA256}  /tmp/antlr-src.tgz" | sha256sum -c - \
 && tar -xzf /tmp/antlr-src.tgz --strip-components=1 -C "$d/src" \
 && cmake -S "$d/src/runtime/Cpp" -B /tmp/dep-build -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS="-O2 -g -gdwarf-3 -fPIC" \
        -DANTLR_BUILD_SHARED=OFF -DANTLR_BUILD_STATIC=ON -DANTLR_BUILD_CPP_TESTS=OFF -DWITH_DEMO=False \
        -DCMAKE_INSTALL_PREFIX="$d" \
 && cmake --build /tmp/dep-build -j"$(nproc)" && cmake --install /tmp/dep-build \
 && rm -rf /tmp/dep-build /tmp/antlr.jar /tmp/antlr-src.tgz && chmod -R a+rX "$d"
USER mayhem

# Source enters from the build CONTEXT (the CI checkout): the FULL repo (incl. .git
# history) is copied to /mayhem as `mayhem`; build.sh builds in place.
USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

# Anything a Dockerfile step materializes into the tree (downloaded fixtures, generated headers,
# vendored deps) MUST be committed or (re)created by build.sh: rlenv's patch grader runs
# `git clean -ffdX` before every graded build, so a gitignored, untracked file is gone by then and
# every honest patch scores zero (SPEC §6.2 item 16; verify-repo.sh checks it).
#
# OPTIONAL compile-cache pre-fill (SPEC §6.2 item 18). Bases minted with /opt/mayhem/ccache/prefill
# run the build under rlenv's pinned ccache, so the patch grader's first (otherwise cold) rebuild is
# warm — this is what keeps large C/C++ targets inside the build time limit. Older bases fall back to
# the plain build; a missing or stale pre-fill only costs a cold cache. Never enable ccache in
# build.sh or via ENV/PATH, and keep the compiler resolving by name (CC=clang) or nothing is cached.
RUN if [ -x /opt/mayhem/ccache/prefill ]; then /opt/mayhem/ccache/prefill bash mayhem/build.sh; \
    else bash mayhem/build.sh; fi

# Run the project's functional tests at build time IF the repo ships them (mayhem/test.sh is
# optional — omit it only when the repo has no tests meaningful for RL). A failing suite fails the
# image build, so the commit image is guaranteed to build AND pass its own tests.
RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh — repo ships no functional tests"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem
