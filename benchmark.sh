#!/bin/bash

simulate_large_server=0
benchmark_size=SMALL
output_file=build/output.csv

function print_usage()
{
  printf "Usage: %s: [-s] [-b value] args\n\n" $0
  printf "Runs the benchmark and saves the result in build/output.csv. Defaults to the small benchmark.\n\n"
  printf "Options:\n"
  printf "  -s                          Run with a simulated large server to reduce memory requirements.\n"
  printf "  -b SMALL / LARGE / FULL     Run the small / large / full benchmark. Small: ~5 mins. Large: ~1 hrs. Full: ~2 hrs. \n"
  printf "  -h                          Display this help message and exit.\n"
}

while getopts sb:h option
do 
    case "${option}" in
        s) 
          simulate_large_server=1;;
        b) 
          benchmark_size=${OPTARG};;
        ? | h)
          print_usage
          exit 2;;
    esac
done

function run_one_server ()
{
  if [[ "$simulate_large_server" -eq 1 ]]; then
    build/s3pir_simlargeserver --one-server $1 $2 $3 
  else 
    build/s3pir --one-server $1 $2 $3
  fi
}

function run_two_server ()
{
  if [[ "$simulate_large_server" -eq 1 ]]; then
    build/s3pir_simlargeserver --two-server $1 $2 $3 
  else 
    build/s3pir --two-server $1 $2 $3
  fi
}

function make_exec()
{
  if [[ "$simulate_large_server" -eq 1 ]]; then
    echo "Simulating large server"
  fi
  mkdir -p build
  find build -name output.csv -delete
  make
}

function small_params()
{
  run_one_server 20 32 "$output_file" 
  run_one_server 24 32 "$output_file" 
  run_two_server 20 32 "$output_file" 
  run_two_server 24 32 "$output_file" 
}

function large_params()
{
  run_one_server 20 32 "$output_file" 
  run_one_server 24 32 "$output_file" 
  run_one_server 28 8 "$output_file" 
  run_one_server 28 32 "$output_file" 
  run_two_server 20 32 "$output_file" 
  run_two_server 24 32 "$output_file" 
  run_two_server 28 8 "$output_file" 
  run_two_server 28 32 "$output_file" 
}

function full_params()
{
  run_one_server 20 32 "$output_file" 
  run_one_server 24 32 "$output_file" 
  run_one_server 28 8 "$output_file" 
  run_one_server 28 32 "$output_file" 
  run_one_server 28 256 "$output_file" 
  run_two_server 20 32 "$output_file" 
  run_two_server 24 32 "$output_file" 
  run_two_server 28 8 "$output_file" 
  run_two_server 28 32 "$output_file" 
  run_two_server 28 256 "$output_file" 
}


case "${benchmark_size}" in
  SMALL) 
    echo "Running small benchmark.."
    make_exec
    small_params;;
  LARGE)
    echo "Running large benchmark.."
    make_exec
    large_params;;
  FULL)
    echo "Running full benchmark.."
    make_exec
    full_params;;
  *)
    print_usage
    exit 2;;
esac
