#pragma once

#include <cctype>
#include <string>
#include <algorithm>

bool isNumber(const std::string& s){
  return !s.empty() && std::find_if(s.begin(),
    s.end(), [](char c) { return !std::isdigit(c); }) == s.end();
}