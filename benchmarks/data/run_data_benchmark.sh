# runs the bench_data_file_read_write benchmark with different types and
# different data types

githash=$(eval "git rev-parse --short HEAD")
timestamp=$(date)
echo ${timestamp}
for benchmark in bench_data_file_read_write bench_data_channel_read_write bench_data_channel_a_to_b
do
  for type in int string pair triple
  do
    for size in 1K 100K 1M 100M 2G
    do
      eval ./${benchmark} -b ${size} $1 ${type}| grep 'single run; ti' | sed -e "s/^/${githash}; ${benchmark}; ${timestamp}; ${size}; ${type}; /"
    done
  done
done
