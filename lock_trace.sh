echo 40000 > buffer_size_kb &&
echo irq sched_switch sched_wakeup sched_waking cpu_frequency cpu_idle lock > set_event &&
cat set_event &&
echo > trace  &&
echo 1 > tracing_on &&
sleep 10 &&
echo 0 > tracing_on &&
cat trace > /data/trace.txt   
