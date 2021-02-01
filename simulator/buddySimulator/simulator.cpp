#include <unistd.h>  
#include <stdio.h>  
// #include <delay.h>
#include <stdlib.h>
// #include <sys/mman.h>
#include <string.h>
#include <pthread.h>

#include <vector>
#include <deque>
 

#define MAX_ORDER 11
#define THREAD_NUM 19

using namespace std;

//* 代表这个cpu的内存管理参数
struct cpu_para{
   int cpu_id;
   int high;
   int batch;
};

//* 代表这个进程的内存分配状态
// struct thread_state{
//     deque<int> pcp_list;
//     vector<vector<int> > allocated_mem;
//     vector<int> mem_size;
// }; 

struct frame_info{
    int order;
    int allocated;
};

pthread_t threads[THREAD_NUM];
struct cpu_para cpu_info[THREAD_NUM];
deque<int> pcp_lists[THREAD_NUM];
// struct thread_state thread_info[THREAD_NUM];


int init_buddy_state[MAX_ORDER] = {216+179, 221+257, 160+146, 78+67, 85+60, 83+41, 22+16, 8+10, 4+12, 2+9, 7187+7965};
// int init_buddy_state[MAX_ORDER] = {216, 221, 160, 78, 85, 83, 22, 8, 4, 2, 7187};
int total_frame_num = 0;
int total_cpu_num = 16;

vector<deque<int>*> buddy_system;
vector<frame_info> physical_pages;

pthread_mutex_t buddy_lock;

void* virtual_cpu(void* cpu_para);

void init_buddy(int total_frames){
    int frame_idx = 0;
    physical_pages.resize(total_frames);

    for(int i = 0; i < MAX_ORDER; ++i){
        deque<int>* cur_free_area = new deque<int>();
        for(int j = 0; j < init_buddy_state[i]; ++j){
            cur_free_area->push_back(frame_idx);
            physical_pages[frame_idx].order = i;
            physical_pages[frame_idx].allocated = 0;
            frame_idx += (1 << i);
        }
        buddy_system.push_back(cur_free_area);
    }

    printf("total_frame: %d, frame_idx: %d\n",total_frames, frame_idx);
}

void init_cpu(){
    for(int i = 0; i < THREAD_NUM; ++i){
        cpu_info[i].cpu_id = i;
        cpu_info[i].batch = 63;
        cpu_info[i].high = 378;

        if (pthread_create(&(threads[i]), NULL, virtual_cpu, &(cpu_info[i])) != 0) {
            printf("init_cpu %d error\n", i);
        }
    }
}

void expand(int frame_idx, int low, int high){
    int size = (1 << high);

    while(high > low){
        high--;
        size >>= 1;

        buddy_system[high]->push_front(frame_idx+size);
        physical_pages[frame_idx+size].order = high;
        physical_pages[frame_idx+size].allocated = 0;
    }
}

int allocate_one_buddysystem(int order){
    int result = -1;

    for(int cur_order = order; cur_order < MAX_ORDER; ++cur_order){
        if(buddy_system[cur_order]->empty()){
            continue;
        }else{
            result = buddy_system[cur_order]->front();
            buddy_system[cur_order]->pop_front();
            
            physical_pages[result].order = order;
            physical_pages[result].allocated = 1;

            expand(result, order, cur_order);

            return result;
        }
    }

    return -1;
}

void allocate_bulk_buddysystem(int batch, cpu_para *cur_cpu_para, deque<int>& pcp_list){

    pthread_mutex_lock(&buddy_lock);
    for(int i = 0; i < batch; ++i){
        int cur_page = -1;
        cur_page = allocate_one_buddysystem(0);

        if(cur_page == -1){
            --i;
            printf("allocate_bulk_buddysystem: buddy no page\n");
            continue;
        }

        pcp_list.push_back(cur_page);
    }
    pthread_mutex_unlock(&buddy_lock);
}

//* 返回对应的页框号，如果分配失败则返回-1 
int allocate_one_pcplist(cpu_para *cur_cpu_para, deque<int>& pcp_list){
    int result = -1;

    if(pcp_list.empty()){
        allocate_bulk_buddysystem(cur_cpu_para->batch, cur_cpu_para, pcp_list);
        if(pcp_list.empty()){
            printf("allocate_one_pcplist: pcp no page after retry\n");
            return -1;
        }
    }

    result = pcp_list.front();
    pcp_list.pop_front();
    physical_pages[result].allocated = 2;

    return result;
}

// TODO: 为了方便起见（防止内存泄漏）vector使用值传递，之后应该改成指针
vector<int> allocate_pages(int number, cpu_para *cur_cpu_para, deque<int>& pcp_list){
    vector<int> result;
    for(int i = 0; i < number; ++i){
        int cur_alloc_result = allocate_one_pcplist(cur_cpu_para, pcp_list);
        if(cur_alloc_result == -1){
            printf("allocate_pages: allocate fail in %d\n", i);
        }
        result.push_back(cur_alloc_result);
    }

    return result;
}

int find_buddy_frame(int frame_idx, int order){
    return frame_idx ^ (1 << order);
}

void del_frame_from_buddy(int frame_idx, int order){
    for(deque<int>::iterator i = buddy_system[order]->begin(); i < buddy_system[order]->end(); ++i){
        if(*i == frame_idx){
            buddy_system[order]->erase(i);
            return;
        }
    }
    
    printf("del_frame_from_buddy, not found frame %d in targeted order %d\n", frame_idx, order);
}

void free_one_buddysystem(int frame_idx, int order, cpu_para *cur_cpu_para){
    int buddy_frame;

    while(order < MAX_ORDER-1){
        buddy_frame = find_buddy_frame(frame_idx, order);
        if(physical_pages[buddy_frame].allocated == 0 && physical_pages[buddy_frame].order == order){
            del_frame_from_buddy(buddy_frame, order);
            frame_idx = (frame_idx & buddy_frame);
            order++;
        }else{
            break;
        }
    }

    physical_pages[frame_idx].order = order;
    physical_pages[frame_idx].allocated = 0;

    buddy_system[order]->push_front(frame_idx);
}

void free_bulk_buddysystem(int batch, cpu_para *cur_cpu_para, deque<int>& pcp_list){
    deque<int> frame_to_free;

    for(int i = 0; i < batch; ++i){
        frame_to_free.push_back(pcp_list.back());
        pcp_list.pop_back();
    }

    pthread_mutex_lock(&buddy_lock);

    for(int i = 0; i < frame_to_free.size(); ++i){
        free_one_buddysystem(frame_to_free[i], 0, cur_cpu_para);
    }

    pthread_mutex_unlock(&buddy_lock);


}

void free_one_pcplist(int frame_idx, cpu_para *cur_cpu_para, deque<int>& pcp_list){
    pcp_list.push_front(frame_idx);

    if(pcp_list.size() >= cur_cpu_para->high){
        free_bulk_buddysystem(cur_cpu_para->batch, cur_cpu_para, pcp_list);
    }
}

// TODO: 现在的申请结果都用vector<int>值传递，之后应改为指针
void free_pages(vector<int> frame_idx, cpu_para *cur_cpu_para, deque<int>& pcp_list){
    for(int i = 0; i < frame_idx.size(); ++i){
        if(frame_idx[i] == -1){
            printf("free_page: failed allocation at %d\n", i);
        }
        free_one_pcplist(frame_idx[i], cur_cpu_para, pcp_list);
    }
}

void* virtual_cpu(void* cur_para){
    struct cpu_para *cur_cpu_para =  (struct cpu_para *) cur_para;
    printf("cpu %d, batch: %d, high %d\n", cur_cpu_para->cpu_id, cur_cpu_para->batch, cur_cpu_para->high);

    // * print_meminfo中需要获取这一部分信息，所以转为全局变量
    // deque<int> pcp_list;

    int page_num = 3*1024*1024/4; //3G
    int remain_page_num = page_num;
    int allocate_base = 32; //128K
    int allocate_addition_max = 256*1024/4; //256M

    // vector<vector<int> >& allocated_mem = thread_info[cpu_info->cpu_id].allocated_mem;
    // vector<int>& mem_size = thread_info[cpu_info->cpu_id].mem_size;
    vector<vector<int> > allocated_mem;
    vector<int> mem_size;
    int cur_allocate_page_num;
    vector<int> cur_allocate_pages;

    while(remain_page_num > 0){
        cur_allocate_page_num = rand() % allocate_addition_max + allocate_base;
        remain_page_num -= cur_allocate_page_num;

        if(remain_page_num < allocate_base){
            cur_allocate_page_num += remain_page_num;
            remain_page_num = 0;
        }

        // cur_allocate_pages = allocate_pages(cur_allocate_page_num, cur_cpu_para, thread_info[cur_cpu_para->cpu_id].pcp_list);
        cur_allocate_pages = allocate_pages(cur_allocate_page_num, cur_cpu_para, pcp_lists[cur_cpu_para->cpu_id]);

        allocated_mem.push_back(cur_allocate_pages);
        mem_size.push_back(cur_allocate_page_num);
    }

    usleep(rand() % 3000);

    while(true){
        int mem_to_release_num = rand() % (allocated_mem.size() * 1 / 3);
        for(int i = 0; i < mem_to_release_num; ++i){
            // usleep(500000);
            int mem_to_release_idx = rand() % allocated_mem.size();

            if(allocated_mem[mem_to_release_idx].size() != mem_size[mem_to_release_idx]){
                printf("cpu %d, invalid page numbers, vector: %d, size: %d\n", cur_cpu_para->cpu_id, allocated_mem[mem_to_release_idx].size(), mem_size[mem_to_release_idx]);
            }
            
            // free_pages(allocated_mem[mem_to_release_idx], cur_cpu_para, thread_info[cur_cpu_para->cpu_id].pcp_list);
            free_pages(allocated_mem[mem_to_release_idx], cur_cpu_para, pcp_lists[cur_cpu_para->cpu_id]);
            remain_page_num += mem_size[mem_to_release_idx];

            allocated_mem.erase(allocated_mem.begin()+mem_to_release_idx);
            mem_size.erase(mem_size.begin()+mem_to_release_idx);
        }

        usleep(rand() % 2000);
        // sleep(1);

        while(remain_page_num > 0){
            cur_allocate_page_num = rand() % allocate_addition_max + allocate_base;
            remain_page_num -= cur_allocate_page_num;

            if(remain_page_num < allocate_base){
                cur_allocate_page_num += remain_page_num;
                remain_page_num = 0;
            }

            // cur_allocate_pages = allocate_pages(cur_allocate_page_num, cur_cpu_para, thread_info[cur_cpu_para->cpu_id].pcp_list);
            cur_allocate_pages = allocate_pages(cur_allocate_page_num, cur_cpu_para, pcp_lists[cur_cpu_para->cpu_id]);

            allocated_mem.push_back(cur_allocate_pages);
            mem_size.push_back(cur_allocate_page_num);
        }
        usleep(rand() % 4000);
        // sleep(2);

    }
}

void print_buddyinfo(){
    // for(int i = 0; i < buddy_system.size(); ++i){
    //     printf("%d\t", i);
    // }
    // printf("\n");
    for(int i = 0; i < buddy_system.size(); ++i){
        printf("%d\t", buddy_system[i]->size());
    }
    printf("\n");
    
    printf("====================================\n");
}

void print_memoryinfo(){
    int frame_pcp_number = 0;
    int frame_buddy_number = 0;
    int frame_allocated_number = 0;

    for(int i = 0; i < THREAD_NUM; ++i){
        // int cur_pcp_list_size = thread_info[i].pcp_list.size();
        int cur_pcp_list_size = pcp_lists[cpu_info[i].cpu_id].size();
        printf("cpu %d; batch: %d; high: %d; count: %d;\n", cpu_info[i].cpu_id, cpu_info[i].batch, cpu_info[i].high, cur_pcp_list_size);
        frame_pcp_number += cur_pcp_list_size;
    }

    for(int i = 0; i < MAX_ORDER; ++i){
        frame_buddy_number += (buddy_system[i]->size() * (1 << i));
    }

    frame_allocated_number = total_frame_num-frame_buddy_number-frame_pcp_number;
    printf("total_frame_num: %d; buddy_frame_num: %d; pcp_frame_num: %d; allocated_frame_num: %d\n", total_frame_num, frame_buddy_number, frame_pcp_number, frame_allocated_number);
    printf("total_mem_size: %dGB = %dMB = %dKB; ", total_frame_num*4/1024/1024, total_frame_num*4/1024, total_frame_num*4);
    printf("allocated_mem_size: %dGB = %dMB = %dKB\n", frame_allocated_number*4/1024/1024, frame_allocated_number*4/1024, frame_allocated_number*4);
    printf("buddy_mem_size: %dGB = %dMB = %dKB; ", frame_buddy_number*4/1024/1024, frame_buddy_number*4/1024, frame_buddy_number*4);
    printf("pcp_mem_size: %dGB = %dMB = %dKB\n", frame_pcp_number*4/1024/1024, frame_pcp_number*4/1024, frame_pcp_number*4);
    
    printf("====================================\n");
}


int main (int argc,char *argv[])   
{   
    for(int i = 0; i < MAX_ORDER; ++i){
        total_frame_num += (init_buddy_state[i] * (1 << i));
    }

    init_buddy(total_frame_num);
    pthread_mutex_init(&buddy_lock, NULL);

    print_memoryinfo();
    print_buddyinfo();

    init_cpu();

    while(true){
        sleep(1);
        print_memoryinfo();
        print_buddyinfo();
    }


    return 0;  
}  