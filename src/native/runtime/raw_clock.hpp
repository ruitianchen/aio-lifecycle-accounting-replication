#pragma once
#include <chrono>
#include <time.h>
#include <stdexcept>
struct NativeRawClock {
  using rep=long long;
  using period=std::nano;
  using duration=std::chrono::nanoseconds;
  using time_point=std::chrono::time_point<NativeRawClock>;
  static constexpr bool is_steady=true;
  static time_point now(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC_RAW,&t))throw std::runtime_error("RAW clock failed");return time_point(duration(rep(t.tv_sec)*1000000000LL+t.tv_nsec));}
};
