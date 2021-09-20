echo $1
echo $2
echo "TEST"
read -p "Press enter to continue"

sleep 5
export PATH=$1;$PATH

$2 --target-os=win64 --arch=x86_64 --toolchain=msvc
