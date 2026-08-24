#ifndef CHVATAL_HPP
#define CHVATAL_HPP

#include "instance.hpp"

#include <vector>

int chvatal(
    const Instance& instance,
    const std::vector<unsigned char>& activeSets,
    std::vector<int>& selectedSets
);
int improveUpperBound(
    const Instance& instance,
    const std::vector<unsigned char>& activeSets,
    std::vector<int>& selectedSets
);

#endif
