# Project Guidance for ffind-1.0


## C/C++ Best Practices
- Use RAII and smart pointers
- Prefer modern C++ features (C++17/20/23)
- Enable warnings: -Wall -Wextra -Wpedantic
- Use sanitizers in debug builds: -fsanitize=address,undefined


## General Guidelines
- Write tests for new functionality
- Keep functions small and focused
- Document non-obvious code
- Profile before optimizing
