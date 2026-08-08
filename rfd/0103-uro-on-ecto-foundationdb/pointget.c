#define FDB_API_VERSION 730
#include <foundationdb/fdb_c.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static FDBDatabase *db;
static uint64_t now_ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
static int cmp(const void*a,const void*b){uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b;return (x>y)-(x<y);}
static void *net(void*a){(void)a;fdb_run_network();return NULL;}
static void rep(const char*n,uint64_t*s,int c){qsort(s,c,sizeof(uint64_t),cmp);
  printf("FDBGET %-28s median=%7.3f ms  p99=%7.3f ms  n=%d\n",n,s[c/2]/1e6,s[(int)(c*0.99)]/1e6,c);fflush(stdout);}
int main(void){
  fdb_select_api_version(FDB_API_VERSION); fdb_setup_network();
  pthread_t th; pthread_create(&th,NULL,net,NULL); usleep(300000);
  if(fdb_create_database(NULL,&db)){printf("no db\n");return 1;}
  /* load 200000 identity rows, batched */
  int N=200000, B=1000;
  for(int base=0; base<N; base+=B){
    FDBTransaction*tr; fdb_database_create_transaction(db,&tr);
    for(int i=base;i<base+B;i++){char k[64],v[64];int kl=snprintf(k,sizeof k,"identity/%08d",i);int vl=snprintf(v,sizeof v,"user%d",i);
      fdb_transaction_set(tr,(const uint8_t*)k,kl,(const uint8_t*)v,vl);}
    FDBFuture*f=fdb_transaction_commit(tr); fdb_future_block_until_ready(f); fdb_future_destroy(f); fdb_transaction_destroy(tr);
  }
  printf("FDBGET loaded %d rows\n",N); fflush(stdout);
  /* point reads, one transaction each, like an Ecto Repo.get */
  int R=2000; uint64_t*s=malloc(sizeof(uint64_t)*R); int n=0;
  for(int i=0;i<R;i++){
    char k[64]; int kl=snprintf(k,sizeof k,"identity/%08d",i%N);
    uint64_t t0=now_ns();
    FDBTransaction*tr; fdb_database_create_transaction(db,&tr);
    FDBFuture*f=fdb_transaction_get(tr,(const uint8_t*)k,kl,0);
    fdb_future_block_until_ready(f);
    fdb_bool_t present; const uint8_t*val; int vlen;
    fdb_future_get_value(f,&present,&val,&vlen);
    s[n++]=now_ns()-t0;
    fdb_future_destroy(f); fdb_transaction_destroy(tr);
  }
  rep("point GET (new txn each)",s,n);
  /* reads reusing one transaction, like inside Repo.transactional */
  n=0; FDBTransaction*tr; fdb_database_create_transaction(db,&tr);
  for(int i=0;i<R;i++){
    char k[64]; int kl=snprintf(k,sizeof k,"identity/%08d",i%N);
    uint64_t t0=now_ns();
    FDBFuture*f=fdb_transaction_get(tr,(const uint8_t*)k,kl,0);
    fdb_future_block_until_ready(f);
    fdb_bool_t present; const uint8_t*val; int vlen;
    fdb_future_get_value(f,&present,&val,&vlen);
    s[n++]=now_ns()-t0; fdb_future_destroy(f);
  }
  fdb_transaction_destroy(tr);
  rep("point GET (shared txn)",s,n);
  printf("FDBGET done\n"); return 0;
}
