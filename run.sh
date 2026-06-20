#!/bin/bash
set -e

cd build
./empi_http -c ../config/agent_config.toml -p 8080 "$@"
