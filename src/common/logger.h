#pragma once
#include <fmt/core.h>
#include <fmt/color.h>

// Simple logger implementation using fmtlib
#define INFO(...)  do { fmt::print(fg(fmt::color::green), "[INFO] "); fmt::print(__VA_ARGS__); fmt::print("\n"); } while(0)
#define WARNING(...) do { fmt::print(fg(fmt::color::yellow), "[WARNING] "); fmt::print(__VA_ARGS__); fmt::print("\n"); } while(0)
#define ERROR(...)   do { fmt::print(fg(fmt::color::red), "[ERROR] "); fmt::print(__VA_ARGS__); fmt::print("\n"); } while(0)
#define DEBUG(...)   do { fmt::print(fg(fmt::color::cyan), "[DEBUG] "); fmt::print(__VA_ARGS__); fmt::print("\n"); } while(0)

// For raw printing without prefixes or newlines (e.g. for streaming UI)
#define LOG_RAW(...) do { fmt::print(__VA_ARGS__); std::fflush(stdout); } while(0)
#define LOG_NL()     do { fmt::print("\n"); std::fflush(stdout); } while(0)
