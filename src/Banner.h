#pragma once
#include <iostream>
#include <string>

// ANSI color codes
namespace Color {
    constexpr const char* RESET   = "\033[0m";
    constexpr const char* CYAN    = "\033[36m";
    constexpr const char* MAGENTA = "\033[35m";
    constexpr const char* YELLOW  = "\033[33m";
    constexpr const char* GREEN   = "\033[32m";
    constexpr const char* BOLD    = "\033[1m";
}

inline void print_banner() {
    static const char* art =
R"( ________  ___  ___  ___  ___  __    ___  ________  ________     
|\   ____\|\  \|\  \|\  \|\  \|\  \ |\  \|\   ___ \|\   __  \    
\ \  \___|\ \  \\\  \ \  \ \  \/  /|\ \  \ \  \_|\ \ \  \|\ /_   
 \ \_____  \ \   __  \ \  \ \   ___  \ \  \ \  \ \\ \ \   __  \  
  \|____|\  \ \  \ \  \ \  \ \  \\ \  \ \  \ \  \_\\ \ \  \|\  \ 
    ____\_\  \ \__\ \__\ \__\ \__\\ \__\ \__\ \_______\ \_______\
   |\_________\|__|\|__|\|__|\|__| \|__|\|__|\|_______|\|_______|
   \|_________|                                                  
                                                                 
                                                                 )";

    std::cout << Color::CYAN << art << Color::RESET << "\n\n";
    std::cout << Color::BOLD << Color::MAGENTA
               << "ShikiDB v1.1" << Color::RESET << "\n";
    std::cout << Color::BOLD << Color::MAGENTA << "Made by: " << Color::RESET
               << Color::YELLOW << "Shikicaaa\nAndrija Stefanovic" << Color::RESET << "\n\n";

}