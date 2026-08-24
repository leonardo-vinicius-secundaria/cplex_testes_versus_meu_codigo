/*
 * ============================================================
 *  Set Cover Problem — Solver com CPLEX
 *  Instância: SCP4.1  (200 elementos, 1000 conjuntos)
 *
 *  Formato do arquivo de entrada (OR-Library):
 *    Linha 1 : m  n          (elementos e conjuntos)
 *    Próximas linhas: custos dos n conjuntos (múltiplas linhas)
 *    Para cada conjunto j = 1..n:
 *      número de elementos que ele cobre
 *      índices dos elementos cobertos (1-based)
 *
 *  Compilar (exemplo):
 *    g++ -O2 -std=c++17 scp_cplex.cpp \
 *        -I$CPLEX_HOME/include \
 *        -L$CPLEX_HOME/lib/x86-64_linux/static_pic \
 *        -lcplex -lm -lpthread -ldl \
 *        -o scp_cplex
 *
 *  Executar:
 *    ./scp_cplex scp41.txt
 * ============================================================
 */

#include <ilcplex/ilocplex.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <ctime>
#include <iomanip>

ILOSTLBEGIN   // necessário para usar STL com CPLEX no namespace ILO

// ─────────────────────────────────────────────────────────────
//  Leitura da instância no formato OR-Library
// ─────────────────────────────────────────────────────────────
struct Instance {
    int m;                                    // número de elementos
    int n;                                    // número de conjuntos
    std::vector<double>              cost;    // cost[j]  custo do conjunto j
    std::vector<std::vector<int>>    cover;   // cover[j] = {elementos cobertos por j} (0-based)
};

Instance readInstance(const std::string& filename) {
    std::ifstream fin(filename);
    if (!fin.is_open()) {
        std::cerr << "Erro: nao foi possivel abrir o arquivo \"" << filename << "\"\n";
        std::exit(1);
    }

    Instance inst;
    fin >> inst.m >> inst.n;

    // Lê os n custos (podem estar distribuídos em várias linhas)
    inst.cost.resize(inst.n);
    for (int j = 0; j < inst.n; ++j)
        fin >> inst.cost[j];

    /*
     * Formato OR-Library (correto):
     *   Para cada ELEMENTO i = 1..m:
     *     k           <- número de conjuntos que cobrem o elemento i
     *     s1 s2 ... sk  <- índices (1-based) dos conjuntos que cobrem i
     *
     * ATENÇÃO: NÃO é "por conjunto", é "por elemento".
     */
    inst.cover.resize(inst.n);   // cover[j] = elementos cobertos pelo conjunto j (0-based)

    for (int i = 0; i < inst.m; ++i) {
        int k;
        fin >> k;                // quantos conjuntos cobrem o elemento i
        for (int t = 0; t < k; ++t) {
            int j;
            fin >> j;
            j--;                 // converte para índice 0-based do conjunto
            inst.cover[j].push_back(i);   // elemento i (0-based) é coberto pelo conjunto j
        }
    }

    fin.close();
    return inst;
}

// ─────────────────────────────────────────────────────────────
//  Modelo de Programação Inteira (IP) com CPLEX
// ─────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {

    // ── Arquivo de entrada ─────────────────────────────────
    std::string filename = "scp41.txt";
    double timeLimit = 3600.0;
    if (argc >= 2) filename = argv[1];
    if (argc >= 3) timeLimit = std::stod(argv[2]);

    std::cout << "============================================\n";
    std::cout << " Set Cover Problem — CPLEX Solver\n";
    std::cout << " Instancia: " << filename << "\n";
    std::cout << "============================================\n\n";

    // ── Lê instância ───────────────────────────────────────
    Instance inst = readInstance(filename);
    std::cout << "Elementos (m) : " << inst.m << "\n";
    std::cout << "Conjuntos (n) : " << inst.n << "\n\n";

    // A leitura fica fora do cronometro interno nos dois solvers. A partir
    // daqui medimos toda a preparacao necessaria para resolver a instancia.
    const auto preparationStart = std::chrono::steady_clock::now();
    const std::clock_t preparationCpuStart = std::clock();

    // Constrói lista inversa: para cada elemento i, quais conjuntos o cobrem
    std::vector<std::vector<int>> elem_sets(inst.m);   // elem_sets[i] = lista de conjuntos que cobrem i
    for (int j = 0; j < inst.n; ++j)
        for (int i : inst.cover[j])
            elem_sets[i].push_back(j);

    // ── Ambiente CPLEX ─────────────────────────────────────
    IloEnv env;
    try {
        IloModel model(env);

        // ── Variáveis de decisão ───────────────────────────
        // x[j] = 1 se o conjunto j for selecionado, 0 caso contrário
        IloBoolVarArray x(env, inst.n);
        for (int j = 0; j < inst.n; ++j) {
            x[j] = IloBoolVar(env);
            std::string vname = "x_" + std::to_string(j + 1);
            x[j].setName(vname.c_str());
        }

        // ── Função Objetivo ────────────────────────────────
        // Minimizar: sum_j  cost[j] * x[j]
        IloExpr obj(env);
        for (int j = 0; j < inst.n; ++j)
            obj += inst.cost[j] * x[j];
        model.add(IloMinimize(env, obj));
        obj.end();

        // ── Restrições de cobertura ────────────────────────
        // Para cada elemento i: sum_{j cobre i} x[j] >= 1
        for (int i = 0; i < inst.m; ++i) {
            IloExpr coverage(env);
            for (int j : elem_sets[i])
                coverage += x[j];
            std::string cname = "cover_" + std::to_string(i + 1);
            model.add(IloRange(env, 1.0, coverage, IloInfinity, cname.c_str()));
            coverage.end();
        }

        // ── Configuração do solver ─────────────────────────
        IloCplex cplex(model);

        // Sem log durante o trecho cronometrado. O DLX tambem nao imprime
        // dentro de search(), evitando medir custo de terminal em um so lado.
        cplex.setParam(IloCplex::Param::MIP::Display,   0);
        cplex.setParam(IloCplex::Param::Simplex::Display, 0);

        // Comparacao justa com o DLX exato: exige prova de otimalidade.
        cplex.setParam(IloCplex::Param::MIP::Tolerances::MIPGap, 0.0);

        // Limite de tempo (segundos) — configurável via argv[2]
        cplex.setParam(IloCplex::Param::TimeLimit, timeLimit);

        // Comparacao justa com o DLX: execucao sequencial em uma thread.
        // Threads = 0 deixa o CPLEX escolher automaticamente e pode usar
        // todos os nucleos disponiveis.
        cplex.setParam(IloCplex::Param::Threads, 1);

        // Exporta o modelo LP para inspeção (opcional)
        // cplex.exportModel("scp41.lp");

        // ── Resolve ────────────────────────────────────────
        const double preparationWall = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - preparationStart
        ).count();
        const double preparationCpu = static_cast<double>(
            std::clock() - preparationCpuStart
        ) / CLOCKS_PER_SEC;

        std::cout << "Iniciando otimizacao...\n\n";
        const auto solveStart = std::chrono::steady_clock::now();
        const std::clock_t solveCpuStart = std::clock();
        bool solved = cplex.solve();
        const double solveWall = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - solveStart
        ).count();
        const double solveCpu = static_cast<double>(
            std::clock() - solveCpuStart
        ) / CLOCKS_PER_SEC;
        const double totalWallAfterRead = preparationWall + solveWall;
        const double totalCpuAfterRead = preparationCpu + solveCpu;

        // ── Resultados ─────────────────────────────────────
        std::cout << "\n============================================\n";
        std::cout << " RESULTADOS\n";
        std::cout << "============================================\n";
        std::cout << "Status CPLEX  : " << cplex.getStatus() << "\n";
        std::cout << "Fim CPLEX     : " << cplex.getCplexStatus() << "\n";
        std::cout << std::fixed << std::setprecision(9);
        std::cout << "Tempo preparacao apos leitura (s): " << preparationWall << "\n";
        std::cout << "Tempo solve (s)                  : " << solveWall << "\n";
        std::cout << "Tempo total apos leitura (s)     : " << totalWallAfterRead << "\n";
        std::cout << "CPU preparacao apos leitura (s)  : " << preparationCpu << "\n";
        std::cout << "CPU solve (s)                    : " << solveCpu << "\n";
        std::cout << "CPU total apos leitura (s)       : " << totalCpuAfterRead << "\n";
        std::cout << std::defaultfloat;
        std::cout << "Nos CPLEX     : " << cplex.getNnodes() << "\n";

        if (solved) {
            double objVal   = cplex.getObjValue();
            double bestBound = cplex.getBestObjValue();
            double gap       = cplex.getMIPRelativeGap() * 100.0;

            std::cout << "Valor obj.    : " << objVal    << "\n";
            std::cout << "Melhor bound  : " << bestBound << "\n";
            std::cout << "MIP gap (%)   : " << gap       << "\n";

            // Conjuntos selecionados
            std::vector<int> selected;
            for (int j = 0; j < inst.n; ++j)
                if (cplex.getValue(x[j]) > 0.5)
                    selected.push_back(j + 1);   // 1-based para saída

            std::cout << "Conjuntos sel.: " << selected.size() << "\n";
            std::cout << "\nConjuntos selecionados (1-based):\n";
            for (int s : selected)
                std::cout << s << " ";
            std::cout << "\n";

            // Verifica cobertura
            std::vector<bool> covered(inst.m, false);
            for (int j = 0; j < inst.n; ++j)
                if (cplex.getValue(x[j]) > 0.5)
                    for (int i : inst.cover[j])
                        covered[i] = true;

            bool feasible = true;
            for (int i = 0; i < inst.m; ++i)
                if (!covered[i]) { feasible = false; break; }

            std::cout << "\nSolucao valida: " << (feasible ? "SIM" : "NAO") << "\n";
        } else {
            std::cout << "Nenhuma solucao encontrada.\n";
        }

    } catch (IloException& e) {
        std::cerr << "Excecao CPLEX: " << e.getMessage() << "\n";
        env.end();
        return 1;
    }

    env.end();
    return 0;
}
