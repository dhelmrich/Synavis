# Runscript to call Unreal Engine, Signalling Server and Python Script
# Author: Dirk Baker

# for testing purposes: if MPI variables are not set, set them
if [ -z "$PMI_RANK" ]; then
    export PMI_RANK=0
fi
if [ -z "$PMI_SIZE" ]; then
    export PMI_SIZE=1
fi


# Unreal Engine Runscript absolute path
UE4_PATH="/p/project1/visforai/helmrich1/unrealrun.sh"
# Signalling Server Runscript absolute path
SS_PATH="/p/project1/visforai/helmrich1/signallingrun.sh"
# Python Script absolute path
PY_PATH="/p/project1/visforai/helmrich1/Synavis/python/coupling/hard_scaling.py"

# module environment
source /p/project1/visforai/helmrich1/env4syn.sh

# Run Signal Server in background
$SS_PATH &> /p/project1/visforai/helmrich1/signalling.log &
# Run Unreal Engine in background
$UE4_PATH &> /p/project1/visforai/helmrich1/unreal.log &
# Run Python Script
python3 $PY_PATH
