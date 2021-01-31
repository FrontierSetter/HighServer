my_log="./test_data/" 
echo > ${my_log}"buddy_info.log"

while true;
do
	/bin/sleep 1

	date +%s >> ${my_log}"buddy_info.log"
	cat "/proc/buddyinfo" >> ${my_log}"buddy_info.log"

done
	
