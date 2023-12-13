#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HASH_SIZE 128
int initialized = 0;
unsigned long int page_size = 0;

typedef struct Page {
        unsigned long int address;
        int ref_count;
} page;
typedef struct thread_local_storage
{
        pthread_t tid;
        unsigned int size;
        unsigned int page_num;
        page **pages;
} TLS;

struct hash_element
{
        pthread_t tid;
        TLS *tls;
        struct hash_element *next;
};

struct hash_element* hash_table[HASH_SIZE];


int tls_create(unsigned int size);
int tls_destroy();
int tls_clone(pthread_t tid);
int tls_read(unsigned int offset, unsigned int length, char* buffer);
int tls_write(unsigned int offset, unsigned int length, char* buffer);
void init();
void tls_handle_page_fault(int sig, siginfo_t *si, void *context);
void tls_protect(page *p);
void tls_unprotect(page *p);


void tls_protect(page *p){
        if(mprotect((void *) (p->address), page_size, 0)) {
                fprintf(stderr, "tls_protect: could not protect page\n");
                exit(1);
        }
}

void tls_unprotect(page *p){
        if(mprotect((void *) (p->address), page_size, (PROT_READ | PROT_WRITE))) {
                fprintf(stderr, "tls_unprotect: could not unprotect page\n");
                exit(1);
        }
}

void tls_handle_page_fault(int sig, siginfo_t *si, void *context){
        unsigned long int p_fault = ((unsigned long int) si->si_addr) & ~(page_size - 1);
        int i;
        int j;
        for(i = 0; i < HASH_SIZE; i++){ //brute force scan thru all regions
                if(hash_table[i] != NULL){
                        struct hash_element* iter = hash_table[i];
                        for(iter = hash_table[i]; iter != NULL; iter = iter->next){
                                for(j = 0; j < iter->tls->page_num; j++){
                                        if(iter->tls->pages[j]->address == p_fault){
                                                pthread_exit(NULL);
                                        }
                                }
                        }
                }
        }
        //if code reaches here then standard page fault didn't occur because would've exited once found correct page

        signal(SIGSEGV, SIG_DFL);
        signal(SIGBUS, SIG_DFL);
        raise(sig);
}

void tls_init(){
        struct sigaction sigact;
        page_size = getpagesize();
        sigemptyset(&sigact.sa_mask);
        sigact.sa_flags = SA_SIGINFO;
        sigact.sa_sigaction = tls_handle_page_fault;

        sigaction(SIGBUS, &sigact, NULL);
        sigaction(SIGSEGV, &sigact, NULL);
        initialized = 0;
}
int tls_create(unsigned int size){
        if(!initialized){
                tls_init();
        }
        //check if already has LSA
        pthread_t TID = pthread_self();
        int ind;
        for(ind = 0; ind < HASH_SIZE; ind++){
                struct hash_element* iter = NULL;
                for(iter = hash_table[ind]; iter != NULL; iter = iter->next){
                        if(iter->tid == TID){
                                return -1;
                        }
                }
        }



        if(size <= 0){
                return -1;
        }

        //set threadlocs members equal to specifications
        TLS* threadlocs = (TLS*) calloc(1, sizeof(TLS));
        threadlocs->tid = TID;
        threadlocs->size = size;
        threadlocs->page_num = size / page_size;
        if((size % page_size) > 0){ //make an extra page if size has nonzero remainder
                (threadlocs->page_num)++;
        }

        threadlocs->pages = (page**) calloc(threadlocs->page_num, sizeof(page*));
        int i;
        for(i = 0; i < threadlocs->page_num; i++){
                page* p = (page*) malloc(sizeof(page));
                p->address = (unsigned long int) mmap(0, page_size, PROT_NONE, (MAP_ANON | MAP_PRIVATE), 0, 0);
                //stuff for each page. given as pseudocode in slides
                p->ref_count = 1;
                threadlocs->pages[i] = p;
        }
        //allocate space and set members
        struct hash_element* hashy = (struct hash_element*) malloc(sizeof(struct hash_element));
        hashy->tls = threadlocs;
        hashy->tid = TID;
        int j = TID % HASH_SIZE;
        if(hash_table[j] == NULL){
                //no collision: just begin a new LL
                hash_table[j] = hashy;
                hash_table[j]->next = NULL;
        }
        else{ //collision in table: append this element to head of LL
                struct hash_element* temp = hash_table[j];
                hash_table[j] = hashy;
                hash_table[j]->next = temp;
        }

        return 0;

}

int tls_destroy(){
        pthread_t TID = pthread_self();
        int ind = TID % HASH_SIZE;
        struct hash_element* iter = hash_table[ind];
        //check if thread has LSA and focus on element to be removed

        //remove mapping from LL
        if(hash_table[ind] != NULL){
                struct hash_element* first = NULL;
                struct hash_element* second = NULL;

                for(second = hash_table[ind]; second != NULL; second = second->next){
                        if(second->tid == TID){
                                break;
                        }
                        first = second;
                }

                if(first != NULL){
                        first = second->next;
                }

        //focus on element to be removed
                iter = second;
        }
        else{
                return -1;
        }

        int i;
        for(i = 0; i < iter->tls->page_num; i++){
        //cleaning up all pages
                if(iter->tls->pages[i]->ref_count > 1){
                        //shared: can just decrement ref_count variable
                        (iter->tls->pages[i]->ref_count)--;
                }
                else{
                        //not shared: free and unmap
                        munmap((void *) iter->tls->pages[i]->address, page_size);
                        free(iter->tls->pages[i]);
                }
        }
        //free everything that had been calloc'ed and malloc'ed
        free(iter->tls->pages);
        free(iter->tls);
        free(iter);
        return 0;
}


int tls_read(unsigned int offset, unsigned int length, char* buffer){
        pthread_t TID = pthread_self();
        int ind = TID % HASH_SIZE;
//check if thread has LSA
        struct hash_element* iter = hash_table[ind];
        if(hash_table[ind] != NULL){
                for(iter = hash_table[ind]; iter != NULL; iter = iter->next){
                        if(iter->tid == TID){
                                break;
                        }
                }
        }

        if(iter == NULL || (offset + length) > iter->tls->size){return -1;}

        //unprotect
        int i;
        for(i = 0; i < iter->tls->page_num; i++){
                tls_unprotect(iter->tls->pages[i]);
        }
        //read op
        int cnt; int idx;
        for(cnt = 0, idx = offset; idx < (offset + length); ++cnt, ++idx){
                page *p;
                unsigned int pn, poff;
                pn = idx / page_size;
                poff = idx % page_size;
                p = iter->tls->pages[pn];
                char* src = ((char *) p->address) + poff;
                buffer[cnt] = *src;
        }
        //protect
        for(i = 0; i < iter->tls->page_num; i++){
                tls_protect(iter->tls->pages[i]);
        }

        return 0;
}



int tls_write(unsigned int offset, unsigned int length, char* buffer){
        pthread_t TID = pthread_self();

        int ind = TID % HASH_SIZE;
        //check if thread has LSA
        struct hash_element* iter = hash_table[ind];
        if(hash_table[ind] != NULL){
                for(iter = hash_table[ind]; iter != NULL; iter = iter->next){
                        if(iter->tid == TID){
                                break;
                        }
                }
        }


        if(iter == NULL || (offset + length) > iter->tls->size){return -1;}

        int i;
        //unprotect
        for(i = 0; i < iter->tls->page_num; i++){
                tls_unprotect(iter->tls->pages[i]);
        }
        //write: provided in slide
        int cnt; int idx;
        for(cnt = 0, idx = offset; idx < (offset + length); ++cnt, ++idx){
                page *p, *copy;
                unsigned int pn, poff;
                pn = idx / page_size;
                poff = idx % page_size;
                p = iter->tls->pages[pn];
                if(p->ref_count > 1) {
                        copy = (page *) calloc(1, sizeof(page));
                        copy->address = (unsigned long int) mmap(0, page_size, PROT_WRITE, (MAP_ANON | MAP_PRIVATE), 0, 0);

                        memcpy((void*) copy->address, (void*)p->address, page_size);
                        copy->ref_count = 1;
                        iter->tls->pages[pn] = copy;

                        p->ref_count--;
                        tls_protect(p);
                        p = copy;
                }

                char* dst = ((char* ) p->address) + poff;
                *dst = buffer[cnt];
        }
        //reprotect
        for(i = 0; i < iter->tls->page_num; i++){
                tls_protect(iter->tls->pages[i]);
        }

        return 0;
}

int tls_clone(pthread_t tid) {
        int ind = tid % HASH_SIZE;
        pthread_t clonetid = pthread_self();
        int clone_ind = clonetid % HASH_SIZE;
        struct hash_element* new_item = NULL;
        struct hash_element* iter = NULL;
        for(new_item = hash_table[clone_ind]; new_item != NULL; new_item = new_item->next){
                if(new_item->tid == clonetid){
                        return -1;
                }
        }


        for(iter = hash_table[ind]; iter != NULL; iter = iter->next){
                if(iter->tid == tid){
                        break;
                }
        }
        //check if current thread already has LSA or if target thread doesn't have LSA
        if(new_item != NULL || iter == NULL){ return -1; }


        new_item = (struct hash_element*) calloc(1, sizeof(struct hash_element));
        new_item->tid = clonetid;
        new_item->tls = (TLS*) calloc(1, sizeof(TLS));
//allocate TLS

        new_item->tls->size = iter->tls->size;
        new_item->tls->page_num = iter->tls->page_num;
        new_item->tls->pages = (page**) calloc(iter->tls->page_num, sizeof(page*));

        //copy pages
        int i;
        for(i = 0; i < iter->tls->page_num; i++){
                //make cloned page entires point to original data-pages
                new_item->tls->pages[i] = iter->tls->pages[i];
                //fix ref count
                (new_item->tls->pages[i]->ref_count)++;
        }
        //Add to global hash table
        if(hash_table[clone_ind] == NULL){
                hash_table[clone_ind] = new_item;
        }
        else{
                struct hash_element* temp = hash_table[clone_ind];
                hash_table[clone_ind] = new_item;
                hash_table[clone_ind]->next = temp;
        }

        return 0;

}
