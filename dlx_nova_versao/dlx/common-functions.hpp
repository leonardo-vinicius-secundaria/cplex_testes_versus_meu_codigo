#ifndef COMMON_FUNCTIONS_HPP
#define COMMON_FUNCTIONS_HPP

#include "instance.hpp"

#include <string>
#include <vector>

struct LagrangianResult
{
    // Multiplicadores duais factiveis, indexados por elemento (1..m).
    // Estes sao os pesos usados incrementalmente por cover/uncover.
    std::vector<double> dualWeights;

    double lagrangianBound = 0.0;
    double feasibleDualBound = 0.0;
    int iterations = 0;
};

// Remove conjuntos j dominados por k:
// S_j subseteq S_k e custo(j) >= custo(k).
// activeSets e devolvido indexado de 1 ate instance.setCount.
int removeDominatedSets(
    const Instance& instance,
    std::vector<unsigned char>& activeSets
);

// Otimiza os multiplicadores somente no no raiz. Nenhuma dessas rotinas
// e chamada durante search(): o B&B usa apenas o vetor dual factivel
// devolvido aqui e o atualiza em O(1) por cover/uncover.
LagrangianResult optimizeLagrangianDual(
    const Instance& instance,
    const std::vector<unsigned char>& activeSets,
    int upperBound,
    int maxIterations = 3000
);

void startTimer();
void stopTimer();
double elapsedTimerWall();
double elapsedTimerCPU();
double totalTimerWall();
double totalTimerCPU();
void printTimingSummary(const std::string& fileName);

#endif
