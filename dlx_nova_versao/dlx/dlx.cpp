#include <bits/stdc++.h>
#include "chvatal.hpp"
#include "common-functions.hpp"
#include "instance.hpp"

using namespace std;

const string DEFAULT_INSTANCE_PATH =  "../scpa41/scpa41.txt";
// scp6* ultrapassa 10 mil incidencias mesmo apos o presolve.
// O armazenamento continua estatico e contiguo, como no DLX original.
const int MAXNODE = 200000;
const int MAXCOL  = 1001;
const int MAXROW  = 3001;
const int MAXSOL  = 1001;
constexpr bool EXIBIR_LOG_BUSCA = true;

int leftN[MAXNODE], rightN[MAXNODE];
int upN[MAXNODE],   downN[MAXNODE];
int rowID[MAXNODE];
int colID[MAXNODE];
int colSize[MAXCOL];
bool colAtiva[MAXCOL];
double pesoDual[MAXCOL];
int menorCustoColuna[MAXCOL];
int rowSize[MAXROW];

double lowerBoundAtivo = 0.0;
double lowerBoundRaiz  = 0.0;

int header    = 0;
int solution[MAXSOL];
int solSize   = 0;
int nodeCount = 0;
int nCols;

int  rowCost[MAXROW];
int  bestCost    = INT_MAX;
int  bestSol[MAXSOL];
int  bestSolSize = 0;
bool usedInBranch[MAXROW];
bool rowEliminada[MAXROW];
bool forbiddenRow[MAXROW];

long long totalNos   = 0;
long long totalPodas = 0;
long long totalPodasLowerBound = 0;
long long totalPodasLowerBoundFilho = 0;
long long totalPodasColunaSemLinha = 0;
long long totalPodasMenorCusto = 0;
double limiteTempoBusca = numeric_limits<double>::infinity();
bool limiteTempoAtingido = false;

constexpr double BOUND_EPSILON = 1e-6;

struct Candidato
{
    int node;
    int row;
    int novasCoberturas;
    double custoReduzido;
    double limiteFilho;
};

// Arenas em forma de pilha para o hot path. Cada nivel recursivo reserva
// apenas uma faixa e a libera ao retornar, sem alocacao dinamica.
Candidato arenaCandidatos[MAXNODE];
int topoArenaCandidatos = 0;
int arenaColunasCobertas[MAXCOL];
int topoArenaColunasCobertas = 0;

bool limiteNaoPodeMelhorar(double limite)
{
    // Todos os custos sao inteiros. Se LB > UB-1, o teto do limite ja
    // alcanca o incumbent e nao existe solucao inteira estritamente melhor.
    return limite > static_cast<double>(bestCost) - 1.0 + BOUND_EPSILON;
}

// ============================================================
// cover / uncover  — apenas lista horizontal de headers
// ============================================================
void cover(int c)
{
    rightN[leftN[c]] = rightN[c];
    leftN[rightN[c]] = leftN[c];
    colAtiva[c] = false;
    lowerBoundAtivo -= pesoDual[c];
}

void uncover(int c)
{
    rightN[leftN[c]] = c;
    leftN[rightN[c]] = c;
    colAtiva[c] = true;
    lowerBoundAtivo += pesoDual[c];
}

int chooseColumn()
{
    // colSize desconsidera tanto as linhas eliminadas na raiz
    // quanto as linhas proibidas entre os ramos irmaos.
    int best = rightN[header];
    for (int c = rightN[header]; c != header; c = rightN[c])
        if (colSize[c] < colSize[best])
            best = c;
    return best;
}

bool colunaAtiva(int c)
{
    return colAtiva[c];
}

// Proibe uma linha nos proximos ramos irmaos. Somente as colunas
// ativas precisam ter colSize atualizado; as demais continuam
// cobertas durante toda a vida desta proibicao.
void proibirLinha(int node)
{
    forbiddenRow[rowID[node]] = true;

    int j = node;
    do
    {
        int c = colID[j];
        if (colunaAtiva(c))
            colSize[c]--;
        j = rightN[j];
    }
    while (j != node);
}

void liberarLinha(int node)
{
    int j = node;
    do
    {
        int c = colID[j];
        if (colunaAtiva(c))
            colSize[c]++;
        j = rightN[j];
    }
    while (j != node);

    forbiddenRow[rowID[node]] = false;
}

// ============================================================
// Instala os multiplicadores duais factiveis calculados uma unica vez
// no no raiz. Durante search(), cover/uncover mantem sua soma em O(1).
// ============================================================
void carregarPesosDuais(const vector<double>& dualWeights)
{
    lowerBoundAtivo = 0.0;
    pesoDual[header] = 0.0;
    for (int c = 1; c <= nCols; c++)
    {
        pesoDual[c] = dualWeights[c];
        lowerBoundAtivo += pesoDual[c];
    }
    lowerBoundRaiz = lowerBoundAtivo;
}

// ============================================================
// Reduced-cost fixing
//
// Para cada linha r:
//
//   custoReduzido(r) = custo(r) - soma dos pesos duais de r
//
// Toda solucao que usa r custa pelo menos:
//
//   lowerBoundRaiz + custoReduzido(r)
//
// Se esse valor nao melhora o UB atual, r pode ser eliminada.
// O calculo e feito somente uma vez, antes da busca. Durante a
// busca, rowEliminada permite testar a linha em O(1).
// ============================================================
int aplicarReducedCostFixing(int totalLinhas)
{
    double custoReduzido[MAXROW] = {};

    for (int r = 1; r <= totalLinhas; r++)
        custoReduzido[r] = rowCost[r];

    for (int node = nCols + 1; node < nodeCount; node++)
        custoReduzido[rowID[node]] -= pesoDual[colID[node]];

    int linhasEliminadas = 0;
    for (int r = 1; r <= totalLinhas; r++)
    {
        if (rowEliminada[r]) continue;

        if (limiteNaoPodeMelhorar(lowerBoundRaiz + custoReduzido[r]))
        {
            rowEliminada[r] = true;
            linhasEliminadas++;
        }
    }

    // Atualiza uma unica vez o tamanho efetivo e o menor custo
    // disponivel de cada coluna. Assim, ambos podem ser consultados
    // em O(1) durante a busca.
    fill(menorCustoColuna, menorCustoColuna + nCols + 1, INT_MAX);

    for (int node = nCols + 1; node < nodeCount; node++)
    {
        int r = rowID[node];
        int c = colID[node];

        if (rowEliminada[r])
            colSize[c]--;
        else
            menorCustoColuna[c] = min(menorCustoColuna[c], rowCost[r]);
    }

    return linhasEliminadas;
}

// ============================================================
// search() — Set Cover
// ============================================================
void search(int k, int MAX_K, int custoAtual)
{
    if (limiteTempoAtingido)
        return;

    totalNos++;
    // Consulta o relogio apenas a cada 1024 nos: mantem o hot path leve e
    // reduz bastante a ultrapassagem do limite em instancias mais dificeis.
    if ((totalNos == 1 || (totalNos & 0x3FFLL) == 0)
        && elapsedTimerWall() >= limiteTempoBusca)
    {
        limiteTempoAtingido = true;
        return;
    }

    double limiteDoNo = custoAtual + lowerBoundAtivo;
    bool podaPorLowerBound = limiteNaoPodeMelhorar(limiteDoNo);

    // if (EXIBIR_LOG_BUSCA)
    // {
    //     cout << "[NO " << totalNos << "]"
    //          << " nivel=" << k
    //          << " | custo=" << custoAtual
    //          << " | LB_restante=" << lowerBoundAtivo
    //          << " | LB_no=" << limiteDoNo
    //          << " | UB=" << bestCost
    //          << " | decisao="
    //          << (podaPorLowerBound ? "PODA_LB" : "CONTINUA")
    //          << "\n";
    // }

    if (podaPorLowerBound)
    {
        totalPodas++;
        totalPodasLowerBound++;
        return;
    }

    if (rightN[header] == header)
    {
        bestCost    = custoAtual;
        bestSolSize = solSize;
        for (int i = 0; i < solSize; i++)
            bestSol[i] = rowID[solution[i]];
        return;
    }

    if (k >= MAX_K)
        return;

    int c = chooseColumn();

    // A coluna escolhida ainda precisa ser coberta. Se nenhuma
    // linha nao eliminada cobre essa coluna, o ramo e inviavel.
    if (colSize[c] == 0)
    {
        totalPodas++;
        totalPodasColunaSemLinha++;
        return;
    }

    // Toda continuacao precisa escolher pelo menos uma linha que
    // cubra c. Usa o mais forte entre o LB dual restante e o menor
    // custo de uma linha disponivel para c, sem somar os dois.
    double lowerBoundRestante = max(
        lowerBoundAtivo,
        static_cast<double>(menorCustoColuna[c])
    );

    if (limiteNaoPodeMelhorar(custoAtual + lowerBoundRestante))
    {
        totalPodas++;
        totalPodasMenorCusto++;
        return;
    }

    const int inicioCandidatos = topoArenaCandidatos;
    for (int node = downN[c]; node != c; node = downN[node])
    {
        int row = rowID[node];
        if (rowEliminada[row] || forbiddenRow[row] || usedInBranch[row])
            continue;

        int novasCoberturas = 1;
        double pesoDualCoberto = pesoDual[c];
        for (int j = rightN[node]; j != node; j = rightN[j])
        {
            if (colunaAtiva(colID[j]))
            {
                novasCoberturas++;
                pesoDualCoberto += pesoDual[colID[j]];
            }
        }

        double limiteFilho = custoAtual
                           + rowCost[row]
                           + lowerBoundAtivo
                           - pesoDualCoberto;

        const double custoReduzido = rowCost[row] - pesoDualCoberto;
        if (topoArenaCandidatos >= MAXNODE)
            throw runtime_error("Arena de candidatos insuficiente");

        arenaCandidatos[topoArenaCandidatos++] = {
            node,
            row,
            novasCoberturas,
            custoReduzido,
            limiteFilho
        };
    }

    const int fimCandidatos = topoArenaCandidatos;
    sort(arenaCandidatos + inicioCandidatos,
         arenaCandidatos + fimCandidatos,
         [](const Candidato& a, const Candidato& b)
    {
        // O multiplicador Lagrangiano tambem orienta o branching. O valor ja
        // foi calculado para o bound do filho; nao ha nova varredura da linha.
        const double reduzidoEsquerda =
            a.custoReduzido * b.novasCoberturas;
        const double reduzidoDireita =
            b.custoReduzido * a.novasCoberturas;

        if (abs(reduzidoEsquerda - reduzidoDireita) > BOUND_EPSILON)
            return reduzidoEsquerda < reduzidoDireita;

        long long esquerda = 1LL * rowCost[a.row] * b.novasCoberturas;
        long long direita  = 1LL * rowCost[b.row] * a.novasCoberturas;

        if (esquerda != direita)
            return esquerda < direita;
        if (rowCost[a.row] != rowCost[b.row])
            return rowCost[a.row] < rowCost[b.row];
        if (a.novasCoberturas != b.novasCoberturas)
            return a.novasCoberturas > b.novasCoberturas;
        return a.row < b.row;
    });

    const double lowerBoundAntesDaColuna = lowerBoundAtivo;
    cover(c);
    colSize[c]--;

    int candidatosProcessados = 0;
    for (int indiceCandidato = inicioCandidatos;
         indiceCandidato < fimCandidatos;
         indiceCandidato++)
    {
        if (limiteTempoAtingido)
            break;

        const Candidato& candidato = arenaCandidatos[indiceCandidato];
        int r = candidato.node;

        // Este e exatamente o LB que seria calculado no inicio do
        // filho. A poda aqui evita cover/uncover e chamada recursiva.
        if (limiteNaoPodeMelhorar(candidato.limiteFilho))
        {
            totalPodas++;
            totalPodasLowerBoundFilho++;
            proibirLinha(r);
            candidatosProcessados++;
            continue;
        }

        int novoCusto = custoAtual + rowCost[rowID[r]];

        if (novoCusto >= bestCost)
        {
            totalPodas++;
            proibirLinha(r);
            candidatosProcessados++;
            continue;
        }

        usedInBranch[rowID[r]] = true;
        solution[solSize++]    = r;

        const double lowerBoundAntesDoFilho = lowerBoundAtivo;
        const int inicioColunasCobertas = topoArenaColunasCobertas;
        for (int j = rightN[r]; j != r; j = rightN[j])
        {
            if (!colunaAtiva(colID[j]))
                continue;

            if (topoArenaColunasCobertas >= MAXCOL)
                throw runtime_error("Arena de colunas cobertas insuficiente");

            colSize[colID[j]]--;
            cover(colID[j]);
            arenaColunasCobertas[topoArenaColunasCobertas++] = colID[j];
        }

        search(k + 1, MAX_K, novoCusto);

        for (int i = topoArenaColunasCobertas - 1;
             i >= inicioColunasCobertas;
             i--)
        {
            uncover(arenaColunasCobertas[i]);
            colSize[arenaColunasCobertas[i]]++;
        }
        topoArenaColunasCobertas = inicioColunasCobertas;
        // Evita acumular erro de ponto flutuante depois de milhoes de
        // pares cover/uncover. A estrutura DLX ja foi restaurada acima.
        lowerBoundAtivo = lowerBoundAntesDoFilho;

        solSize--;
        usedInBranch[rowID[r]] = false;

        // Nos proximos irmaos esta linha nao pode mais ser usada.
        // Isso associa cada cobertura ao seu primeiro candidato e
        // elimina as diferentes permutacoes da mesma solucao.
        proibirLinha(r);
        candidatosProcessados++;
    }

    for (int i = candidatosProcessados - 1; i >= 0; i--)
        liberarLinha(arenaCandidatos[inicioCandidatos + i].node);

    topoArenaCandidatos = inicioCandidatos;

    colSize[c]++;
    uncover(c);
    lowerBoundAtivo = lowerBoundAntesDaColuna;
}

// ============================================================
// initDLX e addNode
// ============================================================
void initDLX(int cols)
{   
    cout << "Inicializando DLX com " << cols << " colunas...\n";
    nCols = cols;
    for (int i = 0; i <= cols; i++)
    {
        leftN[i]  = i - 1;
        rightN[i] = i + 1;
        upN[i] = downN[i] = i;
        colSize[i] = 0;
        colAtiva[i] = (i != header);
        pesoDual[i] = 0.0;
    }
    leftN[0]     = cols;
    rightN[cols] = 0;
    nodeCount    = cols + 1;
    memset(usedInBranch, false, sizeof(usedInBranch));
    memset(rowEliminada, false, sizeof(rowEliminada));
    memset(forbiddenRow, false, sizeof(forbiddenRow));
    memset(rowSize, 0, sizeof(rowSize));
    topoArenaCandidatos = 0;
    topoArenaColunasCobertas = 0;
}

void addNode(int r, int c)
{
    if (nodeCount >= MAXNODE)
        throw runtime_error("MAXNODE insuficiente para a instancia");

    const bool primeiroNodeDaLinha = (rowSize[r] == 0);
    int node = nodeCount++;
    rowID[node] = r;
    colID[node] = c;
    colSize[c]++;
    rowSize[r]++;

    downN[node]    = downN[c];
    upN[node]      = c;
    upN[downN[c]]  = node;
    downN[c]       = node;

    if (primeiroNodeDaLinha)
    {
        leftN[node] = rightN[node] = node;
    }
    else
    {
        leftN[node]          = leftN[node - 1];
        rightN[node]         = node - 1;
        rightN[leftN[node]]  = node;
        leftN[node - 1]      = node;
    }
}

// ============================================================
// main
// ============================================================
int main(int argc, char* argv[])
{
    const string instancePath = argc > 1 ? argv[1] : DEFAULT_INSTANCE_PATH;
    if (argc > 2)
    {
        limiteTempoBusca = stod(argv[2]);
        if (limiteTempoBusca <= 0.0)
        {
            cerr << "Limite de tempo deve ser positivo.\n";
            return 1;
        }
    }

    Instance instance;
    if (!readORLibraryInstance(instancePath, instance))
    {
        cerr << "Erro ao ler a instancia: " << instancePath << "\n";
        return 1;
    }

    const auto inicioPreparacao = chrono::steady_clock::now();
    const clock_t inicioCPUPreparacao = clock();

    vector<unsigned char> activeSets;
    int linhasDominadas = removeDominatedSets(instance, activeSets);

    vector<int> solucaoChvatal;
    int upperBoundChvatal = chvatal(instance, activeSets, solucaoChvatal);
    int upperBoundInicial = improveUpperBound(
        instance,
        activeSets,
        solucaoChvatal
    );

    LagrangianResult lagrangian = optimizeLagrangianDual(
        instance,
        activeSets,
        upperBoundInicial
    );

    initDLX(instance.elementCount);
    for (int set = 1; set <= instance.setCount; set++)
    {
        rowCost[set] = instance.setCosts[set];
        if (!activeSets[set])
        {
            rowEliminada[set] = true;
            continue;
        }

        for (int element : instance.elementsBySet[set])
            addNode(set, element);
    }

    carregarPesosDuais(lagrangian.dualWeights);

    bestCost = upperBoundInicial;
    bestSolSize = static_cast<int>(solucaoChvatal.size());
    for (int i = 0; i < bestSolSize; i++)
        bestSol[i] = solucaoChvatal[i];

    int linhasEliminadas = aplicarReducedCostFixing(instance.setCount);

    const double tempoPreparacao = chrono::duration<double>(
        chrono::steady_clock::now() - inicioPreparacao
    ).count();
    const double cpuPreparacao = static_cast<double>(
        clock() - inicioCPUPreparacao
    ) / CLOCKS_PER_SEC;

    cout << "\nInstancia carregada!\n";
    cout << "Arquivo             : " << instancePath << "\n";
    cout << "Elementos (colunas) : " << instance.elementCount << "\n";
    cout << "Conjuntos (linhas)  : " << instance.setCount << "\n";
    cout << "Incidencias         : " << instance.incidenceCount << "\n";
    cout << "Linhas dominadas    : " << linhasDominadas << "\n";
    cout << "Iteracoes subgrad.  : " << lagrangian.iterations << "\n";
    cout << fixed << setprecision(4);
    cout << "LB Lagrangiano      : " << lagrangian.lagrangianBound << "\n";
    cout << "LB dual factivel    : " << lowerBoundRaiz << "\n";
    cout << defaultfloat;
    cout << "Upper Bound Chvatal : " << upperBoundChvatal << "\n";
    cout << "UB apos busca local : " << upperBoundInicial << "\n";
    cout << "Linhas eliminadas RC: " << linhasEliminadas << "\n";
    if (isfinite(limiteTempoBusca))
        cout << "Limite busca (s)    : " << limiteTempoBusca << "\n";
    cout << "Modo   : Set Cover\n\n";

    startTimer();

    search(0, instance.elementCount, 0);

    stopTimer();

    const double tempoBusca = totalTimerWall();
    const double cpuBusca = totalTimerCPU();
    const double tempoTotalAposLeitura = tempoPreparacao + tempoBusca;
    const double cpuTotalAposLeitura = cpuPreparacao + cpuBusca;

    cout << "\nResultado final:\n";
    cout << "Status DLX: "
         << (limiteTempoAtingido ? "TimeLimit" : "Optimal") << "\n";
    if (bestSolSize == 0)
    {
        cout << "Nenhuma solucao encontrada.\n";
    }
    else
    {
        cout << "Custo minimo: " << bestCost << "\n";
        cout << "Numero de conjuntos: " << bestSolSize << "\n";
        cout << "Linhas escolhidas: ";
        for (int i = 0; i < bestSolSize; i++)
            cout << "L" << bestSol[i] << " ";
        cout << "\n";
    }

    cout << "\nEstatisticas:\n";
    cout << "  Nos explorados : " << totalNos   << "\n";
    cout << "  Podas          : " << totalPodas << "\n";
    cout << "  Podas por LB dual    : " << totalPodasLowerBound << "\n";
    cout << "  Podas por LB do filho: " << totalPodasLowerBoundFilho << "\n";
    cout << "  Podas por menor custo: " << totalPodasMenorCusto << "\n";
    cout << "  Podas sem linha      : " << totalPodasColunaSemLinha << "\n";

    cout << fixed << setprecision(9);
    cout << "Tempo preparacao apos leitura (s): " << tempoPreparacao << "\n";
    cout << "Tempo busca (s)                 : " << tempoBusca << "\n";
    cout << "Tempo total apos leitura (s)    : " << tempoTotalAposLeitura << "\n";
    cout << "CPU preparacao apos leitura (s) : " << cpuPreparacao << "\n";
    cout << "CPU busca (s)                   : " << cpuBusca << "\n";
    cout << "CPU total apos leitura (s)      : " << cpuTotalAposLeitura << "\n";
    cout << defaultfloat;

    printTimingSummary(instancePath);

    return 0;
}
