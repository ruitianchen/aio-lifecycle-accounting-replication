#include "pq_table.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
template<class T> std::vector<T> load(const std::string& path,uint32_t& n,uint32_t& d){
  std::ifstream in(path,std::ios::binary);in.read(reinterpret_cast<char*>(&n),4);in.read(reinterpret_cast<char*>(&d),4);
  if(!in || !n || !d)throw std::runtime_error("PQ verification input header");
  std::vector<T> data(size_t(n)*d);in.read(reinterpret_cast<char*>(data.data()),data.size()*sizeof(T));
  if(!in)throw std::runtime_error("PQ verification input length");
  return data;
}
int main(int argc,char** argv){
  try{
    if(argc!=4)throw std::runtime_error("verify_pq index_prefix raw_full_bin initial_index_prefix");
    const std::string prefix=argv[1];uint32_t n,d,pn,pd,tn,td;
    auto raw=load<float>(argv[2],n,d);auto pq=load<uint8_t>(prefix+"_pq_compressed.bin",pn,pd);
    auto tags=load<uint32_t>(prefix+"_disk.index.tags",tn,td);
    if(tn!=pn || td!=1)throw std::runtime_error("PQ/tag dimensions");
    uint32_t bn,bd,btn,btd;std::string baseline=argv[3];
    auto bpq=load<uint8_t>(baseline+"_pq_compressed.bin",bn,bd);
    auto btags=load<uint32_t>(baseline+"_disk.index.tags",btn,btd);
    if(bn!=btn || bd!=pd || btd!=1)throw std::runtime_error("initial PQ shape");
    std::vector<int64_t> original_slot(n,-1);
    for(size_t i=0;i<btn;++i)if(btags[i]!=uint32_t(-1)){
      if(btags[i]>=n)throw std::runtime_error("initial tag range");
      original_slot[btags[i]]=int64_t(i);
    }
    diskann::FixedChunkPQTable<float> table;table.load_pq_centroid_bin((prefix+"_pq_pivots.bin").c_str(),pd);
    std::vector<uint8_t> code(pd);size_t checked=0,wrong=0,preserved=0,reencoded=0;
    for(size_t i=0;i<pn;++i)if(tags[i]!=uint32_t(-1)){
      if(tags[i]>=n)throw std::runtime_error("PQ tag range");
      if(original_slot[tags[i]]>=0){
        std::copy(bpq.begin()+original_slot[tags[i]]*pd,bpq.begin()+(original_slot[tags[i]]+1)*pd,code.begin());++preserved;
      }else{table.deflate_vec(raw.data()+size_t(tags[i])*d,code.data());++reencoded;}
      for(size_t j=0;j<pd;++j)if(code[j]!=pq[i*pd+j]){++wrong;break;}
      ++checked;
    }
    std::cout<<"PQ_CHECKED "<<checked<<" INITIAL_CODES_PRESERVED "<<preserved<<" INSERTED_REENCODED "<<reencoded<<" MISMATCH "<<wrong<<'\n';return wrong?1:0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
