#!/bin/bash

if [ "$1" = "-interactive" ]; then
  # Launch shell for interactivity
  /bin/bash
else
  ./benchmark.sh "$@"
  if [ -d \results ]; then
    cp build/output.csv /results/
  fi 
fi
