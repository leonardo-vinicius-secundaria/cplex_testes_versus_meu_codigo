#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <ilcplex/ilocplex.h>

ILOSTLBEGIN

struct SetCoverInstance {
    int m; // elementos
    int n; // conjuntos
    std::vector<std::vector<int>> A; // A[i][j] = 1 se conjunto j cobre elemento i
};

SetCoverInstance readSetCoverInstance(const std::string& filename) {
    std::ifstream in(filename);
    if (!in.is_open()) {
        throw std::runtime_error("Nao foi possivel abrir o arquivo: " + filename);
    }

    SetCoverInstance inst;
    in >> inst.m >> inst.n;
    if (!in || inst.m <= 0 || inst.n <= 0) {
        throw std::runtime_error("Cabecalho invalido em: " + filename);
    }

    inst.A.assign(inst.m, std::vector<int>(inst.n, 0));

    for (int i = 0; i < inst.m; ++i) {
        for (int j = 0; j < inst.n; ++j) {
            in >> inst.A[i][j];
            if (!in || (inst.A[i][j] != 0 && inst.A[i][j] != 1)) {
                throw std::runtime_error("Matriz invalida em: " + filename);
            }
        }
    }

    return inst;
}

int main(int argc, char* argv[]) {
    std::string filename = (argc > 1) ? argv[1] : "instancia_exemplo2.txt";

    IloEnv env;
    try {
        SetCoverInstance inst = readSetCoverInstance(filename);

        IloModel model(env);
        IloBoolVarArray x(env, inst.n);

        // Objetivo: minimizar o numero de conjuntos escolhidos
        IloExpr obj(env);
        for (int j = 0; j < inst.n; ++j) {
            obj += x[j];
        }
        model.add(IloMinimize(env, obj));
        obj.end();

        // Cobertura: cada elemento deve ser coberto por pelo menos um conjunto
        for (int i = 0; i < inst.m; ++i) {
            IloExpr lhs(env);
            for (int j = 0; j < inst.n; ++j) {
                if (inst.A[i][j] == 1) {
                    lhs += x[j];
                }
            }
            model.add(lhs >= 1);
            lhs.end();
        }

        IloCplex cplex(model);
        cplex.setOut(env.getNullStream());

        if (!cplex.solve()) {
            std::cout << "Nao foi encontrada solucao viavel para o set cover.\n";
            env.end();
            return 0;
        }

        std::cout << "=== SET COVER ===\n";
        std::cout << "Arquivo: " << filename << "\n";
        std::cout << "Valor objetivo: " << cplex.getObjValue() << "\n";
        std::cout << "Conjuntos selecionados (indice 1-based): ";

        bool first = true;
        for (int j = 0; j < inst.n; ++j) {
            if (cplex.getValue(x[j]) > 0.5) {
                if (!first) std::cout << ", ";
                std::cout << (j + 1);
                first = false;
            }
        }
        std::cout << "\n";

        std::cout << "x = [";
        for (int j = 0; j < inst.n; ++j) {
            std::cout << (cplex.getValue(x[j]) > 0.5 ? 1 : 0);
            if (j + 1 < inst.n) std::cout << " ";
        }
        std::cout << "]\n";
    }
    catch (const IloException& e) {
        std::cerr << "Erro do CPLEX: " << e << "\n";
    }
    catch (const std::exception& e) {
        std::cerr << "Erro: " << e.what() << "\n";
    }

    env.end();
    return 0;
}