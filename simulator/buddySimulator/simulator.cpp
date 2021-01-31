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

using namespace std;

struct cpu_info{
   int cpu_id;
   int high;
   int batch;
};


int init_buddy_state[MAX_ORDER] = {216+179, 221+257, 160+146, 78+67, 85+60, 83+41, 22+16, 8+10, 4+12, 2+9, 7187+7965};
// int init_buddy_state[11] = [216, 221, 160, 78, 85, 83, 22, 8, 4, 2, 7187];

vector<deque<int>*> buddy_system;

pthread_mutex_t buddy_lock;

void init_buddy(){
    int frame_idx = 0;

    for(int i = 0; i <= 10; ++i){
        deque<int>* cur_free_area = new deque<int>();
        for(int j = 0; j < init_buddy_state[i]; ++j){
            cur_free_area->push_back(frame_idx);
            frame_idx += (1 << i);
        }
        buddy_system.push_back(cur_free_area);
    }

    printf("frame_idx: %d\n", frame_idx);
}

void expand(int frame_idx, int low, int high){
    int size = 1 << high;

    while(high > low){
        high--;
        size >>= 1;

        buddy_system[high]->push_front(frame_idx+size);
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

            expand(result, order, cur_order);

            return result;
        }
    }

    return -1;
}

void allocate_bulk_buddysystem(int batch, cpu_info *cur_info, deque<int>& pcp_list){

    pthread_mutex_lock(&buddy_lock);
    for(int i = 0; i < batch; ++i){
        int cur_page = -1;
        cur_page = allocate_one_buddysystem();

        if(cur_page == -1){
            --i;
            printf("allocate_bulk_buddysystem: buddy no page\n");
            continue;
        }

        pcp_list.push_back(cur_page);
    }
    pthread_mutex_unlock(&buddy_lock);
}

int allocate_one_pcplist(cpu_info *cur_info, deque<int>& pcp_list){
    int result = -1;

    if(pcp_list.empty()){
        allocate_bulk_buddysystem(cur_info->batch, cur_info, pcp_list);
        if(pcp_list.empty()){
            printf("allocate_one_pcplist: pcp no page after retry\n");
            return -1;
        }
    }

    result = pcp_list.front();
    pcp_list.pop_front();

    return result;

}

void free_bulk_buddysystem(int batch, cpu_info *cur_info, deque<int>& pcp_list){
    deque<int> frame_to_free;

    for(int i = 0; i < batch; ++i){
        frame_to_free.push_back(pcp_list.back());
        pcp_list.pop_back();
    }
}

void free_one_pcplist(int frame_idx, cpu_info *cur_info, deque<int>& pcp_list){
    pcp_list.push_front(frame_idx);

    if(pcp_list.size() >= cur_info->high){

    }
}

void* virtual_cpu(void* cpu_info){
    struct cpu_info *cur_info =  (struct cpu_info *) cpu_info;

    deque<int> pcp_list;

    while(true){

    }
}




int main (int argc,char *argv[])   
{   

    init_buddy();
    pthread_mutex_init(&buddy_lock, NULL);

    for(int i = 0; i < buddy_system.size(); ++i){
        printf("Order: %d, cnt: %d\n", i, buddy_system[i]->size());
    }

    return 0;  
}  