#include <unistd.h>  
#include <stdio.h>  
// #include <delay.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>

#include <vector>
 
using namespace std;



int main (int argc,char *argv[])   
{   

    int page_num = atoi(argv[1]);
    int remain_page_num = page_num;
    int allocate_base = 32; //128K
    int allocate_addition_max = 256*1024/4; //256M

    vector<int*> allocated_mem;
    vector<int> mem_size;
    int cur_allocate_page_num;
    int* cur_allocate_pages;

    while(remain_page_num > 0){
        cur_allocate_page_num = rand() % allocate_addition_max + allocate_base;
        remain_page_num -= cur_allocate_page_num;

        if(remain_page_num < allocate_base){
            cur_allocate_page_num += remain_page_num;
            remain_page_num = 0;
        }

        cur_allocate_pages = (int*)malloc(cur_allocate_page_num*1024*sizeof(int));
        memset(cur_allocate_pages, 128, cur_allocate_page_num*1024*sizeof(int));

        allocated_mem.push_back(cur_allocate_pages);
        mem_size.push_back(cur_allocate_page_num);
    }

    sleep(3);

    while(true){
        int mem_to_release_num = rand() % (allocated_mem.size() * 1 / 3);
        for(int i = 0; i < mem_to_release_num; ++i){
            // usleep(500000);
            int mem_to_release_idx = rand() % allocated_mem.size();
            free(allocated_mem[mem_to_release_idx]);
            remain_page_num += mem_size[mem_to_release_idx];

            allocated_mem.erase(allocated_mem.begin()+mem_to_release_idx);
            mem_size.erase(mem_size.begin()+mem_to_release_idx);
        }

        sleep(1);

        while(remain_page_num > 0){
            // usleep(500000);
            cur_allocate_page_num = rand() % allocate_addition_max + allocate_base;
            remain_page_num -= cur_allocate_page_num;

            if(remain_page_num < allocate_base){
                cur_allocate_page_num += remain_page_num;
                remain_page_num = 0;
            }

            cur_allocate_pages = (int*)malloc(cur_allocate_page_num*1024*sizeof(int));
            memset(cur_allocate_pages, 128, cur_allocate_page_num*1024*sizeof(int));
            allocated_mem.push_back(cur_allocate_pages);
            mem_size.push_back(cur_allocate_page_num);
        }
        sleep(2);
    }

    return 0;  
}  