#pragma once
#include <cstdlib>
#include <stdexcept>
#include <string>
namespace grt_driver {
inline std::string result_file(const char* root,int elapsed,bool merged){
  if(!root || !*root || *root!='/')throw std::runtime_error("GRT_DRIVER_RESULT_ROOT requires an absolute per-window directory");
  std::string p(root);
  if(p.back()!='/')p.push_back('/');
  return p+"result_overall_diskann"+std::to_string(elapsed)+(merged?"merged.bin":".bin");
}
}
