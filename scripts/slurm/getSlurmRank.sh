#! /bin/bash
HOSTS=$(${slurm}/getSlurmHostlist.sh)
HOST="$HOSTNAME"
ID=$(echo $HOSTS | awk -v workers_per_node="$SLURM_CPU_CORES_PER_NODE" -v procid="$SLURM_PROCID" 'BEGIN { FS=" " } { for(i = 1; i <= NF; i++) { if($i == "'$HOST'") { printf "%i", (((i - i) * workers_per_node) + procid) } } }')

if [ -z $ID ]; then
  echo "0"
else 
  echo "$ID"
fi

