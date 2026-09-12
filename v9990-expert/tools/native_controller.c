
#define MAXSTATE 4096
static unsigned char *state_begin;
static size_t state_size;
typedef struct { unsigned char bytes[MAXSTATE]; float cost; unsigned char first; unsigned short x,y; } Node;
static Node *order_base;
static int cmpnode(const void *aa,const void *bb){int a=*(const int*)aa,b=*(const int*)bb;return order_base[a].cost<order_base[b].cost?-1:order_base[a].cost>order_base[b].cost?1:0;}
static void save_state(unsigned char *p){memcpy(p,state_begin,state_size);}
static void load_state(const unsigned char *p){memcpy(state_begin,p,state_size);}
static void find_state(void){
 uintptr_t lo=UINTPTR_MAX,hi=0;
#define BOUND(v) do {uintptr_t a=(uintptr_t)&v,b=a+sizeof(v);if(a<lo)lo=a;if(b>hi)hi=b;}while(0)
@BOUNDS@
#undef BOUND
 state_begin=(unsigned char*)lo;state_size=hi-lo;
 if(state_size>MAXSTATE){fprintf(stderr,"state size %llu too large\n",(unsigned long long)state_size);exit(2);}
 fprintf(stderr,"state size %llu\n",(unsigned long long)state_size);
}
static float danger(unsigned strategy){
 float cost=0;unsigned i;int dx,dy,d;
 const float ax=strategy&1?100:128,ay=strategy&2?190:170;
 for(i=0;i<192;i++)if(bullets[i].live){
  dx=abs((bullets[i].x>>4)-(int)player_x);dy=abs((bullets[i].y>>4)-(int)player_y);d=dx>dy?dx:dy;
  if(d<40)cost+=500.0f/((d+1)*(d+1));
 }
 for(i=0;i<laser_count;i++){
  dx=abs(laser_px[i]-(int)player_x);dy=abs(laser_py[i]-(int)player_y);d=dx>dy?dx:dy;
  if(d<50)cost+=600.0f/((d+1)*(d+1));
 }
 dx=(int)player_x-(int)ax;dy=(int)player_y-(int)ay;
 cost+=(dx*dx+dy*dy)*.001f;
 if(player_y<56)cost+=(56-player_y)*.8f;
 if(player_y>201)cost+=(player_y-201)*.15f;
 return cost;
}
static unsigned choose(unsigned width,unsigned depth,unsigned chunk,unsigned strategy){
 static const unsigned char moves[17]={0,1,2,4,8,5,6,9,10,17,18,20,24,21,22,25,26};
 Node *beam=calloc(width,sizeof(Node)),*next=calloc(width*17,sizeof(Node));
 int *indices=malloc(width*17*sizeof(int));
 unsigned char original[MAXSTATE];save_state(original);save_state(beam[0].bytes);beam[0].cost=0;
 unsigned count=1,d,n,i,j,q,best=0,last_best=0;
 for(d=0;d<depth;d++){
  n=0;
  for(i=0;i<count;i++)for(j=0;j<17;j++){
   load_state(beam[i].bytes);
   float cost=beam[i].cost;unsigned alive=1;
   for(q=0;q<chunk;q++){
    if(mode!=1){if(result!=2)alive=0;break;}
    step(moves[j]);if(mode!=1&&result!=2){alive=0;break;}
    cost+=danger(strategy)/(1+d*.08f);
   }
   if(!alive)continue;
   next[n].first=d?beam[i].first:moves[j];next[n].cost=cost;
   next[n].x=player_x;next[n].y=player_y;save_state(next[n].bytes);indices[n]=n;n++;
  }
  if(!n){best=last_best;break;}
  order_base=next;qsort(indices,n,sizeof(int),cmpnode);best=next[indices[0]].first;last_best=best;
  count=0;
  for(i=0;i<n&&count<width;i++){
   Node *cand=next+indices[i];unsigned duplicate=0;
   for(q=0;q<count;q++)if(cand->x==beam[q].x&&cand->y==beam[q].y&&cand->first==beam[q].first){duplicate=1;break;}
   if(!duplicate)beam[count++]=*cand;
  }
 }
 load_state(original);free(indices);free(beam);free(next);return best;
}
int main(int argc,char **argv){
 unsigned width=argc>1?atoi(argv[1]):12,depth=argc>2?atoi(argv[2]):8,chunk=argc>3?atoi(argv[3]):3,strategy=argc>4?atoi(argv[4]):0;
 unsigned target=argc>5?atoi(argv[5]):16384;
 FILE *f=fopen("planned-inputs.bin","wb"),*trace=fopen("host-route.tsv","w");if(!f||!trace)return 4;
 find_state();reset_run();unsigned maxlive=0;
 while(mode==1&&tick<target){
  unsigned k=choose(width,depth,chunk,strategy),q;
  for(q=0;q<chunk&&mode==1&&tick<target;q++){
   unsigned before=tick;step(k);fputc(k,f);
   unsigned i,n=0;for(i=0;i<192;i++)if(bullets[i].live)n++;
   if(n>maxlive)maxlive=n;
   fprintf(trace,"%u\t%u\t%u\t%u\t%lu\t%u\t%u\t%u\t%u\n",before,player_x,player_y,rng,(unsigned long)score,n,laser_count,pattern_id,pattern_age);
   if(tick%300==0){printf("tick %u score %lu live %u drops %u\n",tick,(unsigned long)score,n,spawn_dropped);fflush(stdout);}
  }
 }
 fclose(f);fclose(trace);
 printf("END tick=%u mode=%u result=%u score=%lu maxlive=%u dropped=%u\n",tick,mode,result,(unsigned long)score,maxlive,spawn_dropped);
 return tick>=target?0:1;
}
