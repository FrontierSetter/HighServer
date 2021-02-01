// g++ ./mine_simulator.cpp -O0 -o mine_simulator.exe -lpthread -std=c++11 
#include <unistd.h>  
#include <stdio.h>  
// #include <delay.h>
#include <stdlib.h>
// #include <sys/mman.h>
#include <sys/time.h>
#include <string.h>
#include <pthread.h>

#include <vector>
#include <deque>
#include <unordered_map>

#define MAX_ORDER 11
#define THREAD_NUM 59

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
    //* 表示分配状态，0在buddy中空闲，1在pcp中空闲，2被分配，3在pcp保留区域中 
    int allocated;
};

pthread_t threads[THREAD_NUM];
struct cpu_para cpu_info[THREAD_NUM];
// deque<int> pcp_lists[THREAD_NUM];
vector<deque<int>*> pcp_buddys[THREAD_NUM];
// TODO:应该加入老化机制防止有些保留区浪费，不过现在的fake工作集下肯定会充分利用
// *以前一个frame为索引，后面的块全部被拆为order-0
unordered_map<int, deque<int> > pcp_reserves[THREAD_NUM];
// struct thread_state thread_info[THREAD_NUM];


int init_buddy_state[MAX_ORDER] = {216+179, 221+257, 160+146, 78+67, 85+60, 83+41, 22+16, 8+10, 4+12, 2+9, 7187+7965};
// int init_buddy_state[MAX_ORDER] = {216, 221, 160, 78, 85, 83, 22, 8, 4, 2, 7187};
int total_frame_num = 0;
int total_cpu_num = 16;

vector<deque<int>*> buddy_system;
vector<frame_info> physical_pages;

pthread_mutex_t buddy_lock;

void* virtual_cpu(void* cpu_para);

int get_MSB_idx(int target){
    int result = -1;
    while(target){
        target >>= 1;
        ++result;
    }

    return result;
}

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

        for(int j = 0; j < MAX_ORDER; ++j){
            deque<int>* cur_free_area = new deque<int>();
            pcp_buddys[i].push_back(cur_free_area);
        }

    }
}

void start_cpu(){
    for(int i = 0; i < THREAD_NUM; ++i){
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

void allocate_bulk_buddysystem(int order, int batch, cpu_para *cur_cpu_para, vector<deque<int>*>& pcp_buddy){

    pthread_mutex_lock(&buddy_lock);
    for(int i = 0; i < batch; ++i){
        int cur_page = -1;
        cur_page = allocate_one_buddysystem(order);

        if(cur_page == -1){
            // --i;
            printf("allocate_bulk_buddysystem: buddy no page order %d\n", order);
            break;
        }

        pcp_buddy[order]->push_back(cur_page);
    }
    pthread_mutex_unlock(&buddy_lock);
}

//* 有些时候只是为了获取reserve_area，允许失败
void try_allocate_bulk_buddysystem(int order, int batch, cpu_para *cur_cpu_para, vector<deque<int>*>& pcp_buddy){

    pthread_mutex_lock(&buddy_lock);
    for(int i = 0; i < batch; ++i){
        int cur_page = -1;
        cur_page = allocate_one_buddysystem(order);

        if(cur_page == -1){
            break;
        }
        pcp_buddy[order]->push_back(cur_page);
    }
    pthread_mutex_unlock(&buddy_lock);
}

//* 返回对应的页框号，如果分配失败则返回-1 
//* reserve_key用于在搜索区寻找，-1表示没有key；potential_size代表包含本次分配在内的潜在大小
int allocate_one_pcplist(int reserve_key, int potential_size, cpu_para *cur_cpu_para, vector<deque<int>*>& pcp_buddy, unordered_map<int, deque<int> >& pcp_reserve){
    int result = -1;
    unordered_map<int, deque<int> >::iterator reserve_area;

    if(reserve_key != -1){
        reserve_area = pcp_reserve.find(reserve_key);
        if(reserve_area != pcp_reserve.end()){
            result = reserve_area->second.front();
            reserve_area->second.pop_front();
            // unordered_map的key不能原地修改，所以插入新值，再删除旧值
            // 应当先插入新值，因为旧值的second部分还需要使用
            if(!reserve_area->second.empty()){
                pcp_reserve.insert(pair<int, deque<int> >(result, reserve_area->second));
            }
            pcp_reserve.erase(reserve_area);
            
            physical_pages[result].allocated = 2;
            return result;
        }
    }

    //* 没有key或者key不在保留区域中，产生对应的保留区区域
    //* 保留区只会由本地缓存的页面组成，不然反而增加了buddy的中心化争用 
    int reserve_size_order = get_MSB_idx(potential_size);

    if(reserve_size_order > 0){
nodegradation_retry:
        //TODO: 保留区的下限是order为2，即四个页面，两个页面感觉没有保留的价值 
        for(int cur_order = reserve_size_order; cur_order > 0; --cur_order){
            if(!pcp_buddy[cur_order]->empty()){
                int target_reserve_frame = pcp_buddy[cur_order]->front();
                pcp_buddy[cur_order]->pop_front();

                if(physical_pages[target_reserve_frame].order != cur_order || physical_pages[target_reserve_frame].allocated != 1){
                    printf("allocate_one_pcplist: pcp_buddy state wrong, cur_order: %d, target_order: %d, target_state: %d\n", cur_order, physical_pages[target_reserve_frame].order, physical_pages[target_reserve_frame].allocated);
                }

                int target_reserve_key = target_reserve_frame;
                deque<int> target_reserve_area;
                
                for(int i = 0; i < (1 << cur_order); ++i){
                    //* 这里用frame而不是key作为起点，因为虽然两者具有相同的值，但是逻辑含义不同 
                    target_reserve_area.push_back(target_reserve_frame+i);
                    physical_pages[target_reserve_frame+1].order = 0;
                    physical_pages[target_reserve_frame+1].allocated = 3; //* allocated=3代表在pcp保留区中
                }

                if(target_reserve_area.front() != target_reserve_key){
                    printf("allocate_one_pcplist: pcp_reserve state wrong, head != key\n");
                }
                target_reserve_area.pop_front();

                pcp_reserve.insert(pair<int, deque<int> >(target_reserve_key, target_reserve_area));

                result = target_reserve_key;
                physical_pages[result].allocated = 2;
                return result;
            }
        }
    }

    //* 运行到这里，要么是reserve_order为0，要么是本地没有可以被reserve的空间
    if(reserve_size_order == 0){
degradation_order_0:
        if(pcp_buddy[0]->empty()){
            allocate_bulk_buddysystem(0, cur_cpu_para->batch, cur_cpu_para, pcp_buddy);
            if(pcp_buddy[0]->empty()){
                printf("allocate_one_pcplist: pcp no page after retry 0\n");
                return -1;
            }
        }
        result = pcp_buddy[0]->front();
        pcp_buddy[0]->pop_front();

        physical_pages[result].allocated = 2;
        return result;
    }else{
        if(pcp_buddy[reserve_size_order]->empty()){
            //* 分配的页面总数量不大于batch
            try_allocate_bulk_buddysystem(reserve_size_order, max(1, cur_cpu_para->batch/(1<<reserve_size_order)), cur_cpu_para, pcp_buddy);
        }
        if(pcp_buddy[reserve_size_order]->empty()){
            //* buddy里这个order的分配失败，退化到order为0的情况
            goto degradation_order_0;
        }else{
            goto nodegradation_retry;
        }
    }
}

// TODO: 为了方便起见（防止内存泄漏）vector使用值传递，之后应该改成指针
vector<int> allocate_pages(int number, cpu_para *cur_cpu_para){
    vector<int> result;
    int prev_frame = -1;
    for(int i = 0; i < number; ++i){
        int cur_alloc_result = allocate_one_pcplist(prev_frame, number-i, cur_cpu_para, pcp_buddys[cur_cpu_para->cpu_id], pcp_reserves[cur_cpu_para->cpu_id]);
        if(cur_alloc_result == -1){
            printf("allocate_pages: allocate fail in %d\n", i);
        }
        prev_frame = cur_alloc_result;
        result.push_back(cur_alloc_result);
    }

    return result;
}

int find_buddy_frame(int frame_idx, int order){
    return frame_idx ^ (1 << order);
}

bool find_del_frame_from_buddy(int frame_idx, int order){
    for(deque<int>::iterator i = buddy_system[order]->begin(); i < buddy_system[order]->end(); ++i){
        if(*i == frame_idx){
            buddy_system[order]->erase(i);
            return true;
        }
    }
    
    //TODO: 会进入错误状态，肯定代码里有问题
    // printf("del_frame_from_buddy, not found frame %d in targeted order %d\n", frame_idx, order);
    return false;
}

void free_one_buddysystem(int frame_idx, int order){
    int buddy_frame;

    while(order < MAX_ORDER-1){
        buddy_frame = find_buddy_frame(frame_idx, order);
        if(physical_pages[buddy_frame].allocated == 0 && physical_pages[buddy_frame].order == order
            && find_del_frame_from_buddy(buddy_frame, order)){
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

void free_bulk_buddysystem(cpu_para *cur_cpu_para, vector<deque<int>*>& pcp_buddy){
    deque<int> frame_to_free;

    for(int cur_order = 0; cur_order < MAX_ORDER; ++cur_order){
        while(pcp_buddy[cur_order]->size()*(1<<cur_order) > cur_cpu_para->high){
            frame_to_free.push_back(pcp_buddy[cur_order]->back());
            pcp_buddy[cur_order]->pop_back();
            if(physical_pages[frame_to_free.back()].order != cur_order){
                printf("free_bulk_buddysystem: wrong order, buddy: %d, page: %d\n", cur_order, physical_pages[frame_to_free.back()].order);
            }
        }
    }

    pthread_mutex_lock(&buddy_lock);

    while(!frame_to_free.empty()){
        free_one_buddysystem(frame_to_free.front(), physical_pages[frame_to_free.front()].order);
        frame_to_free.pop_front();
    }

    pthread_mutex_unlock(&buddy_lock);


}

int find_del_frame_from_pcpbuddy(int frame_idx, int order, vector<deque<int>*>& pcp_buddy){
    for(deque<int>::iterator i = pcp_buddy[order]->begin(); i < pcp_buddy[order]->end(); ++i){
        if(*i == frame_idx){
            pcp_buddy[order]->erase(i);
            return 1;
        }
    }
    
    return 0;
}

// TODO: 决定什么时候需要把pcp页面返还给buddy
// TODO: 优先返还的应该是在本地没法组织起来的页面，还给buddy让buddy来重新组织
bool should_exchange_with_buddy(cpu_para *cur_cpu_para, vector<deque<int>*>& pcp_buddy){
    int total_pcp_pages = 0;
    
    for(int cur_order = 0; cur_order < MAX_ORDER; ++cur_order){
        total_pcp_pages += pcp_buddy[cur_order]->size()*(1<<cur_order);
        if(pcp_buddy[cur_order]->size()*(1<<cur_order) > cur_cpu_para->high){
            return true;
        }
    }

    //TODO: 总数检测，有必要吗？
    if(total_pcp_pages > 5*cur_cpu_para->high){
        return true;
    }else{
        return false;
    }
}

//* potential_free_size表示包含本次释放在内的潜在大小 
void free_one_pcplist(int frame_idx, int potential_free_size, cpu_para *cur_cpu_para, vector<deque<int>*>& pcp_buddy){
    int buddy_frame;
    int order = 0;

    while(order < MAX_ORDER-1){
        buddy_frame = find_buddy_frame(frame_idx, order);
        if(physical_pages[buddy_frame].allocated == 1 && physical_pages[buddy_frame].order == order
            && (find_del_frame_from_pcpbuddy(buddy_frame, order, pcp_buddy) == 1)){
            
            frame_idx = (frame_idx & buddy_frame);
            order++;
        }else{
            break;
        }
    }

    physical_pages[frame_idx].order = order;
    physical_pages[frame_idx].allocated = 1;

    pcp_buddy[order]->push_front(frame_idx);

    //* 如果后续还有东西要来，那么这次就不必急着进行与中心伙伴系统的交互
    if(potential_free_size == 1){
        if(should_exchange_with_buddy(cur_cpu_para, pcp_buddy)){
            free_bulk_buddysystem(cur_cpu_para, pcp_buddy);
        }
    }
}

// TODO: 现在的申请结果都用vector<int>值传递，之后应改为指针
void free_pages(vector<int> frame_idx, cpu_para *cur_cpu_para){
    for(int i = 0; i < frame_idx.size(); ++i){
        if(frame_idx[i] == -1){
            printf("free_page: failed allocation at %d\n", i);
        }
        free_one_pcplist(frame_idx[i], frame_idx.size()-i, cur_cpu_para, pcp_buddys[cur_cpu_para->cpu_id]);
    }
}

void* virtual_cpu(void* cur_para){
    struct cpu_para *cur_cpu_para =  (struct cpu_para *) cur_para;
    printf("cpu %d, batch: %d, high %d\n", cur_cpu_para->cpu_id, cur_cpu_para->batch, cur_cpu_para->high);

    // * print_meminfo中需要获取这一部分信息，所以转为全局变量
    // deque<int> pcp_list;

    int page_num = 1*1024*1024/4; //1G
    int remain_page_num = page_num;
    int allocate_base = 0; //0K
    int allocate_addition_max = 4*1024/4; //4M

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
        cur_allocate_pages = allocate_pages(cur_allocate_page_num, cur_cpu_para);

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
            free_pages(allocated_mem[mem_to_release_idx], cur_cpu_para);
            remain_page_num += mem_size[mem_to_release_idx];

            allocated_mem.erase(allocated_mem.begin()+mem_to_release_idx);
            mem_size.erase(mem_size.begin()+mem_to_release_idx);
        }

        usleep(rand() % 2000000);
        // sleep(1);

        while(remain_page_num > 0){
            cur_allocate_page_num = rand() % allocate_addition_max + allocate_base;
            remain_page_num -= cur_allocate_page_num;

            if(remain_page_num < allocate_base){
                cur_allocate_page_num += remain_page_num;
                remain_page_num = 0;
            }

            // cur_allocate_pages = allocate_pages(cur_allocate_page_num, cur_cpu_para, thread_info[cur_cpu_para->cpu_id].pcp_list);
            cur_allocate_pages = allocate_pages(cur_allocate_page_num, cur_cpu_para);

            allocated_mem.push_back(cur_allocate_pages);
            mem_size.push_back(cur_allocate_page_num);
        }
        usleep(rand() % 4000000);
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
    int frame_reserve_number = 0;
    int frame_buddy_number = 0;
    int frame_allocated_number = 0;

    for(int i = 0; i < THREAD_NUM; ++i){
        // int cur_pcp_list_size = thread_info[i].pcp_list.size();
        for(int j = 0; j < MAX_ORDER; ++j){
            frame_pcp_number += pcp_buddys[i][j]->size()*(1 << j);
        }

        for(unordered_map<int, deque<int> >::iterator itr = pcp_reserves[i].begin(); itr != pcp_reserves[i].end(); ++itr){
            frame_reserve_number += itr->second.size();
        }
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
    printf("reserve_mem_size: %dGB = %dMB = %dKB\n", frame_reserve_number*4/1024/1024, frame_reserve_number*4/1024, frame_reserve_number*4);
    
    printf("====================================\n");
}

void print_time(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    printf("time: %d\t%d\n",tv.tv_sec,tv.tv_usec);
    printf("====================================\n");
}


int main (int argc,char *argv[])   
{   
    struct timeval tv;

    for(int i = 0; i < MAX_ORDER; ++i){
        total_frame_num += (init_buddy_state[i] * (1 << i));
    }

    init_buddy(total_frame_num);
    pthread_mutex_init(&buddy_lock, NULL);
    init_cpu();
 
    print_memoryinfo();
    print_buddyinfo();

    start_cpu();

    while(true){
        print_time();
        print_memoryinfo();
        print_buddyinfo();
        sleep(1);
    }


    return 0;  
}  