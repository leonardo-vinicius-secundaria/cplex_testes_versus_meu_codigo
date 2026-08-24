#include "common-functions.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

namespace
{
constexpr double DUAL_EPSILON = 1e-9;

struct ElementIncidence
{
    // Estrutura CSR: os conjuntos que contem o elemento e ocupam o intervalo
    // [offsets[e], offsets[e + 1]) de sets. Os dois vetores sao montados uma
    // unica vez no no raiz e reutilizados pelo dual ascent.
    std::vector<int> offsets;
    std::vector<int> sets;
};

ElementIncidence buildElementIncidence(
    const Instance& instance,
    const std::vector<unsigned char>& activeSets
)
{
    ElementIncidence incidence;
    incidence.offsets.assign(instance.elementCount + 2, 0);

    int activeIncidences = 0;
    for (int set = 1; set <= instance.setCount; set++)
    {
        if (!activeSets[set]) continue;

        for (int element : instance.elementsBySet[set])
        {
            incidence.offsets[element + 1]++;
            activeIncidences++;
        }
    }

    for (int element = 1; element <= instance.elementCount; element++)
        incidence.offsets[element + 1] += incidence.offsets[element];

    incidence.sets.resize(activeIncidences);
    std::vector<int> nextPosition = incidence.offsets;

    for (int set = 1; set <= instance.setCount; set++)
    {
        if (!activeSets[set]) continue;

        for (int element : instance.elementsBySet[set])
            incidence.sets[nextPosition[element]++] = set;
    }

    return incidence;
}

void projectDualFeasible(
    const Instance& instance,
    const std::vector<unsigned char>& activeSets,
    std::vector<double>& dualWeights
)
{
    // Uma unica passagem basta: cada correcao apenas reduz multiplicadores.
    // Logo, um conjunto que ja ficou factivel nunca volta a ser violado.
    for (int set = 1; set <= instance.setCount; set++)
    {
        if (!activeSets[set]) continue;

        double dualSum = 0.0;
        for (int element : instance.elementsBySet[set])
            dualSum += dualWeights[element];

        if (dualSum <= instance.setCosts[set] + DUAL_EPSILON)
            continue;

        const double scale = instance.setCosts[set] / dualSum;
        for (int element : instance.elementsBySet[set])
            dualWeights[element] *= scale;
    }
}

double runDualAscent(
    const Instance& instance,
    const std::vector<unsigned char>& activeSets,
    const ElementIncidence& incidence,
    std::vector<double>& dualWeights
)
{
    std::vector<double> rowSlack(
        instance.setCount + 1,
        std::numeric_limits<double>::infinity()
    );

    for (int set = 1; set <= instance.setCount; set++)
    {
        if (!activeSets[set]) continue;

        double slack = instance.setCosts[set];
        for (int element : instance.elementsBySet[set])
            slack -= dualWeights[element];

        rowSlack[set] = std::max(0.0, slack);
    }

    // Tambem basta uma passagem. Depois que um elemento encosta em alguma
    // restricao dual com folga zero, as folgas seguintes so podem diminuir.
    for (int element = 1; element <= instance.elementCount; element++)
    {
        double increase = std::numeric_limits<double>::infinity();
        for (int index = incidence.offsets[element];
             index < incidence.offsets[element + 1];
             index++)
        {
            increase = std::min(increase, rowSlack[incidence.sets[index]]);
        }

        if (!std::isfinite(increase) || increase <= DUAL_EPSILON)
            continue;

        dualWeights[element] += increase;
        for (int index = incidence.offsets[element];
             index < incidence.offsets[element + 1];
             index++)
        {
            double& slack = rowSlack[incidence.sets[index]];
            slack = std::max(0.0, slack - increase);
        }
    }

    return std::accumulate(
        dualWeights.begin() + 1,
        dualWeights.end(),
        0.0
    );
}

// ============================================================
// TIMER — medicao de tempo no estilo CPLEX
//
// CPLEX reporta dois tempos separados:
//   - "Elapsed time": tempo de parede (wall clock), medido com
//     um relogio monotonico que nunca retrocede mesmo que o
//     horario do sistema mude. Aqui: std::chrono::steady_clock.
//   - "CPU time": tempo de processador consumido pelo processo.
//     Aqui: std::clock() / CLOCKS_PER_SEC.
//
// O timer e iniciado imediatamente antes da busca e interrompido
// imediatamente depois, evitando incluir a impressao do resultado
// final no tempo total do algoritmo.
// ============================================================
class Timer
{
public:
    void start()
    {
        wallStart = Clock::now();
        cpuStart = std::clock();
    }

    void stop()
    {
        finalWall = elapsedWall();
        finalCPU = elapsedCPU();
    }

    // Segundos de parede decorridos desde start().
    double elapsedWall() const
    {
        const auto now = Clock::now();
        return std::chrono::duration<double>(now - wallStart).count();
    }

    // Segundos de CPU consumidos desde start().
    double elapsedCPU() const
    {
        return static_cast<double>(std::clock() - cpuStart) / CLOCKS_PER_SEC;
    }

    double totalWall() const
    {
        return finalWall;
    }

    double totalCPU() const
    {
        return finalCPU;
    }

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    TimePoint wallStart;
    std::clock_t cpuStart = 0;
    double finalWall = 0.0;
    double finalCPU = 0.0;
};

Timer timer;
}

int removeDominatedSets(
    const Instance& instance,
    std::vector<unsigned char>& activeSets
)
{
    const int wordCount = (instance.elementCount + 63) / 64;
    const std::size_t rowStride = static_cast<std::size_t>(wordCount);

    // Matriz plana: uma unica alocacao, sem vector por conjunto.
    std::vector<std::uint64_t> rowMasks(
        static_cast<std::size_t>(instance.setCount + 1) * rowStride,
        0
    );
    std::vector<int> rowSizes(instance.setCount + 1, 0);
    std::vector<int> order(instance.setCount);

    activeSets.assign(instance.setCount + 1, 1);
    activeSets[0] = 0;

    for (int set = 1; set <= instance.setCount; set++)
    {
        rowSizes[set] = static_cast<int>(instance.elementsBySet[set].size());
        const std::size_t base = static_cast<std::size_t>(set) * rowStride;

        for (int element : instance.elementsBySet[set])
        {
            const int zeroBasedElement = element - 1;
            rowMasks[base + (zeroBasedElement >> 6)] |=
                std::uint64_t{1} << (zeroBasedElement & 63);
        }
    }

    std::iota(order.begin(), order.end(), 1);
    std::sort(order.begin(), order.end(), [&](int a, int b)
    {
        if (rowSizes[a] != rowSizes[b])
            return rowSizes[a] > rowSizes[b];
        if (instance.setCosts[a] != instance.setCosts[b])
            return instance.setCosts[a] < instance.setCosts[b];
        return a < b;
    });

    int removed = 0;
    for (int dominated : order)
    {
        if (!activeSets[dominated]) continue;

        const std::size_t dominatedBase =
            static_cast<std::size_t>(dominated) * rowStride;

        for (int dominator : order)
        {
            if (dominator == dominated || !activeSets[dominator]) continue;
            if (rowSizes[dominator] < rowSizes[dominated]) continue;
            if (instance.setCosts[dominator] > instance.setCosts[dominated])
                continue;

            const std::size_t dominatorBase =
                static_cast<std::size_t>(dominator) * rowStride;

            bool isSubset = true;
            for (int word = 0; word < wordCount; word++)
            {
                if (rowMasks[dominatedBase + word]
                    & ~rowMasks[dominatorBase + word])
                {
                    isSubset = false;
                    break;
                }
            }

            if (!isSubset) continue;

            // Entre conjuntos identicos de mesmo custo, conserva o menor ID.
            if (rowSizes[dominator] == rowSizes[dominated]
                && instance.setCosts[dominator] == instance.setCosts[dominated]
                && dominated < dominator)
            {
                continue;
            }

            activeSets[dominated] = 0;
            removed++;
            break;
        }
    }

    return removed;
}

LagrangianResult optimizeLagrangianDual(
    const Instance& instance,
    const std::vector<unsigned char>& activeSets,
    int upperBound,
    int maxIterations
)
{
    LagrangianResult result;
    result.dualWeights.assign(instance.elementCount + 1, 0.0);

    std::vector<double> currentDual(
        instance.elementCount + 1,
        std::numeric_limits<double>::infinity()
    );
    std::vector<int> coverageCount(instance.elementCount + 1, 0);
    std::vector<unsigned char> lagrangianSelection(instance.setCount + 1, 0);

    // Inicializacao custo/cardinalidade feita em uma passagem pelas
    // incidencias, sem procurar novamente os conjuntos de cada elemento.
    for (int set = 1; set <= instance.setCount; set++)
    {
        if (!activeSets[set] || instance.elementsBySet[set].empty()) continue;

        const double unitCost = static_cast<double>(instance.setCosts[set])
                              / static_cast<double>(
                                  instance.elementsBySet[set].size()
                              );
        for (int element : instance.elementsBySet[set])
            currentDual[element] = std::min(currentDual[element], unitCost);
    }

    for (int element = 1; element <= instance.elementCount; element++)
    {
        if (!std::isfinite(currentDual[element]))
            currentDual[element] = 0.0;
    }

    double bestBound = -std::numeric_limits<double>::infinity();
    double stepScale = 2.0;
    int iterationsWithoutImprovement = 0;

    for (int iteration = 0; iteration < maxIterations; iteration++)
    {
        double lagrangianBound = std::accumulate(
            currentDual.begin() + 1,
            currentDual.end(),
            0.0
        );

        for (int set = 1; set <= instance.setCount; set++)
        {
            if (!activeSets[set])
            {
                lagrangianSelection[set] = 0;
                continue;
            }

            double value = instance.setCosts[set];
            for (int element : instance.elementsBySet[set])
                value -= currentDual[element];

            lagrangianSelection[set] = (value < -DUAL_EPSILON);
            if (lagrangianSelection[set])
                lagrangianBound += value;
        }

        if (lagrangianBound > bestBound + DUAL_EPSILON)
        {
            bestBound = lagrangianBound;
            result.dualWeights = currentDual;
            iterationsWithoutImprovement = 0;
        }
        else
        {
            iterationsWithoutImprovement++;
        }

        result.iterations = iteration + 1;

        // Custos sao inteiros: LB > UB-1 ja prova que nao existe solucao
        // inteira estritamente melhor que o incumbent.
        if (bestBound > static_cast<double>(upperBound) - 1.0 + DUAL_EPSILON)
            break;

        if (iterationsWithoutImprovement >= 30)
        {
            stepScale *= 0.5;
            iterationsWithoutImprovement = 0;
        }
        if (stepScale < 5e-4)
            break;

        std::fill(coverageCount.begin(), coverageCount.end(), 0);
        for (int set = 1; set <= instance.setCount; set++)
        {
            if (!lagrangianSelection[set]) continue;
            for (int element : instance.elementsBySet[set])
                coverageCount[element]++;
        }

        double squaredNorm = 0.0;
        for (int element = 1; element <= instance.elementCount; element++)
        {
            const double subgradient = 1.0 - coverageCount[element];
            squaredNorm += subgradient * subgradient;
        }
        if (squaredNorm <= DUAL_EPSILON)
            break;

        const double gap = static_cast<double>(upperBound) - lagrangianBound;
        if (gap <= DUAL_EPSILON)
            break;

        const double step = stepScale * gap / squaredNorm;
        for (int element = 1; element <= instance.elementCount; element++)
        {
            const double subgradient = 1.0 - coverageCount[element];
            currentDual[element] = std::max(
                0.0,
                currentDual[element] + step * subgradient
            );
        }
    }

    result.lagrangianBound = bestBound;

    projectDualFeasible(instance, activeSets, result.dualWeights);
    const ElementIncidence incidence =
        buildElementIncidence(instance, activeSets);
    result.feasibleDualBound = runDualAscent(
        instance,
        activeSets,
        incidence,
        result.dualWeights
    );

    return result;
}

void startTimer()
{
    timer.start();
}

void stopTimer()
{
    timer.stop();
}

double elapsedTimerWall()
{
    return timer.elapsedWall();
}

double elapsedTimerCPU()
{
    return timer.elapsedCPU();
}

double totalTimerWall()
{
    return timer.totalWall();
}

double totalTimerCPU()
{
    return timer.totalCPU();
}

void printTimingSummary(const std::string& fileName)
{
    std::cout << fileName << "\n\n";
    std::cout << "+-------------------------------------------------+\n";
    std::cout << "|            RESUMO DE TEMPO (CPLEX-style)        |\n";
    std::cout << "+-------------------------------------------------+\n";
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "| Tempo de parede (Elapsed time) : "
              << std::setw(10) << timer.totalWall() << " s |\n";
    std::cout << "| Tempo de CPU   (CPU time)      : "
              << std::setw(10) << timer.totalCPU() << " s |\n";
    std::cout << "+-------------------------------------------------+\n";
    std::cout << std::defaultfloat;
}
