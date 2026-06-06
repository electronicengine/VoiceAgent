#pragma once
#include <iostream>
#include <string>

// Simple logger implementation
#define INFO(...)  std::cout << "[INFO] " << __VA_ARGS__ << std::endl
#define WARNING(...) std::cerr << "[WARNING] " << __VA_ARGS__ << std::endl
#define ERROR(...)   std::cerr << "[ERROR] " << __VA_ARGS__ << std::endl
