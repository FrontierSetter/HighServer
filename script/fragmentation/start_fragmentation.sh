name="fragmentation"
echo "" > "fragmentation.log"

for ((i=0;i<$1;i++))
do
    # str = "$name$i"
    echo "$name$i"
	date +%s >> "fragmentation.log"

    ./fragmentation.exe 786432 &

done