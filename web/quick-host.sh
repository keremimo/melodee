#!/usr/bin/env bash

PYTHON="${PYTHON:-python3}"
VERSION="${VERSION:-1.1.0}"
SITE_DIR="${SITE_DIR:-/tmp/melodee-site}"
HOST_PORT="${HOST_PORT:-8000}"

[ -d ${SITE_DIR} ] && rm -rf ${SITE_DIR}

${PYTHON} web/make_site.py build/release-${VERSION}/melodee-${VERSION}.fwsc build/release-${VERSION}-1core/melodee-${VERSION}-1core.fwsc ${VERSION} ${SITE_DIR}

${PYTHON} -m http.server ${HOST_PORT} --directory ${SITE_DIR}