// ============================================================
//  DLX — Set Cover
//  Adaptado para instâncias OR-Library (formato Beasley 1987)
//  Testado com scp41.txt  (200 elementos, 1000 conjuntos)
//
//  Formato do arquivo de entrada:
//    Linha 1 : m  n       (elementos, conjuntos)
//    Prox.   : n custos   (um por conjunto, em multiplas linhas)
//    Para cada elemento i (1..m):
//        k_i              (quantos conjuntos cobrem o elemento i)
//        j_1 j_2 ... j_k  (indices desses conjuntos, 1-indexados)
// ============================================================

#include <bits/stdc++.h>
#include <chrono>
#include <ctime>
#include <iomanip>
using namespace std;

// ============================================================
//  TIMER — estilo CPLEX (wall clock + CPU time)
// ============================================================
struct Timer
{
    using Clock     = chrono::steady_clock;
    using TimePoint = Clock::time_point;

    TimePoint wallStart;
    clock_t   cpuStart;

    void start()
    {
        wallStart = Clock::now();
        cpuStart  = clock();
    }
    double elapsedWall() const
    {
        return chrono::duration<double>(Clock::now() - wallStart).count();
    }
    double elapsedCPU() const
    {
        return static_cast<double>(clock() - cpuStart) / CLOCKS_PER_SEC;
    }
};

Timer gTimer;

// ============================================================
//  CONSTANTES — dimensionadas para scp41 com folga
//
//  scp41: 200 elementos (DLX colunas), 1000 conjuntos (DLX linhas)
//  Total de nós de dados medido: 4009
//  Nós de cabeçalho: 201  →  total real: 4210
//  MAXNODE = 50000 cobre qualquer instância OR-Library de porte similar
// ============================================================
const int MAXNODE  = 50000;   // nós da estrutura DLX
const int MAXCOL   = 1001;    // colunas DLX (elementos), 1-indexado; 201 basta p/ scp41
const int MAXROW   = 1001;    // linhas DLX (conjuntos), 1-indexado
const int MAXSOL   = 500;     // profundidade máxima da solução

// Limite de tempo (wall clock) — o algoritmo retorna a melhor
// solução encontrada até aqui se o tempo for atingido.
const double TIME_LIMIT_SEC = 3600.0;   // 1 hora

// Frequência de impressão de progresso (em número de nós visitados)
const long long LOG_EVERY = 1000000LL;

// ============================================================
//  ESTRUTURA DLX
// ============================================================
int leftN[MAXNODE], rightN[MAXNODE];
int upN[MAXNODE],   downN[MAXNODE];
int rowID[MAXNODE]; // ID do conjunto (1..nSets) para cada nó de dados
int colID[MAXNODE]; // ID do elemento (1..nElems) para cada nó de dados
int  colSize[MAXCOL];
bool colAtiva[MAXCOL];  // true iff a coluna ainda está na lista de cabeçalhos (O(1) lookup)

int header    = 0;
int nodeCount = 0;
int nElems;   // número de elementos  → DLX colunas (= m)
int nSets;    // número de conjuntos  → DLX linhas  (= n)

int  rowCost[MAXROW];       // custo do conjunto j
int  solution[MAXSOL];      // nós escolhidos no ramo atual
int  solSize   = 0;
int  bestCost  = INT_MAX;
int  bestSol[MAXSOL];
int  bestSolSize = 0;
bool usedInBranch[MAXROW];  // proteção contra repetição no Set Cover

long long totalNos    = 0;
long long totalPodas  = 0;
bool      stopSearch  = false; // sinaliza time-limit ou outro critério de parada

// ============================================================
//  PROGRESSO — imprime a cada LOG_EVERY nós  e verifica tempo
// ============================================================
inline void checkProgress()
{
    // Verifica a cada LOG_EVERY nós para não chamar elapsedWall()
    // em todo nó (overhead desnecessário)
    if (totalNos % LOG_EVERY == 0)
    {
        double tw = gTimer.elapsedWall();
        cout << fixed << setprecision(2)
             << "[PROGRESSO] nos=" << totalNos
             << " | podas=" << totalPodas
             << " | bestCost=" << (bestCost == INT_MAX ? -1 : bestCost)
             << " | wall=" << tw << "s\n"
             << defaultfloat;
        cout.flush();

        // if (tw >= TIME_LIMIT_SEC)
        // {
        //     cout << "[STOP] Limite de tempo atingido ("
        //          << TIME_LIMIT_SEC << "s). Retornando melhor solucao encontrada.\n";
        //     stopSearch = true;
        // }
    }
}

// ============================================================
//  printMelhorSolucao — simplificada (sem tabela de cobertura,
//  que seria 200 colunas × N linhas e poluiria o log)
// ============================================================
void printMelhorSolucao()
{
    double tWall = gTimer.elapsedWall();
    double tCPU  = gTimer.elapsedCPU();

    int total = 0;
    for (int i = 0; i < solSize; i++)
        total += rowCost[rowID[solution[i]]];

    cout << "\n[INCUMBENTE]";
    cout << fixed << setprecision(4)
         << " custo=" << total;
    if (bestCost != INT_MAX)
        cout << " (melhora=" << (bestCost - total) << ")";
    else
        cout << " (primeira solucao)";
    cout << " | wall=" << tWall << "s"
         << " | CPU="  << tCPU  << "s"
         << " | nos="  << totalNos
         << defaultfloat << "\n";

    cout << "  Conjuntos (" << solSize << "): ";
    for (int i = 0; i < solSize; i++)
        cout << rowID[solution[i]] << " ";
    cout << "\n\n";
    cout.flush();
}

// ============================================================
//  printTimingSummary — estilo CPLEX Solution Summary
// ============================================================
void printTimingSummary(double tWall, double tCPU)
{
    cout << "\n";
    cout << "+-------------------------------------------------+\n";
    cout << "|        RESUMO DE TEMPO  (CPLEX-style)           |\n";
    cout << "+-------------------------------------------------+\n";
    cout << fixed << setprecision(4);
    cout << "| Tempo de parede (Elapsed time) : "
         << setw(10) << tWall << " s |\n";
    cout << "| Tempo de CPU   (CPU time)      : "
         << setw(10) << tCPU  << " s |\n";
    cout << "+-------------------------------------------------+\n";
    cout << defaultfloat;
}

// ============================================================
//  cover / uncover — apenas lista horizontal de cabeçalhos
//  (Set Cover: listas verticais ficam intactas; a mesma linha
//   pode cobrir múltiplas colunas simultaneamente)
// ============================================================
void cover(int c)
{
    rightN[leftN[c]] = rightN[c];
    leftN[rightN[c]] = leftN[c];
    colAtiva[c] = false;  // O(1) — substitui colunaAtiva()
}

void uncover(int c)
{
    rightN[leftN[c]] = c;
    leftN[rightN[c]] = c;
    colAtiva[c] = true;
}

// ============================================================
//  chooseColumn — começando das com maior custo
int chooseColumn()
{
    int best = -1;

    double bestScore = 1e18;

    for (int c = rightN[header]; c != header; c = rightN[c])
    {
        int minCost = INT_MAX;

        for (int r = downN[c]; r != c; r = downN[r])
        {
            int row = rowID[r];

            if (usedInBranch[row]) continue;

            minCost = min(minCost, rowCost[row]);
        }

        // score híbrido
        double score =
            (double)minCost * 1000.0 + colSize[c];

        if (score < bestScore)
        {
            bestScore = score;
            best = c;
        }
    }

    return best;
}

// colunaAtiva — O(1) via array colAtiva[] (mantido por cover/uncover)
inline bool colunaAtiva(int c) { return colAtiva[c]; }

// ============================================================
//  search() — Set Cover
//
//  Cada coluna precisa ser coberta AO MENOS uma vez.
//  Listas verticais NÃO são podadas; usedInBranch impede que
//  o mesmo conjunto seja selecionado duas vezes no mesmo ramo.
//  colunaAtiva() evita cobrir duas vezes a mesma coluna.
//
//  Correção de segurança: se chooseColumn() retornar uma
//  coluna com colSize == 0, nenhum conjunto pode cobri-la →
//  backtrack imediato (poda de inviabilidade).
// ============================================================
// ============================================================
//  lowerBound(custoAtual)
//
//  Bound GREEDY SUM (muito mais forte que o anterior "max"):
//
//  Passa sobre as colunas ativas ordenadas por colSize crescente
//  (mais restrita primeiro). Para cada coluna ainda não coberta
//  neste passe:
//    1. Encontra o conjunto de menor custo que a cobre
//       (excluindo usedInBranch).
//    2. Acumula esse custo no lb.
//    3. Marca todas as colunas ativas cobertas por aquele conjunto
//       como "cobertas neste passe" — assim seu custo não é
//       contado de novo.
//
//  Isso é equivalente a resolver um Set Cover greedy no subproblema
//  restante, dando um lb muito mais apertado que apenas o max.
//
//  Retorna INT_MAX/2 se detectar inviabilidade (alguma coluna
//  não tem nenhuma linha disponível).
//  Faz early-exit assim que custoAtual + lb_parcial >= bestCost.
// ============================================================
int lowerBound(int custoAtual)
{
    // Arrays estáticos — evitam heap allocation a cada chamada.
    // Seguro pois lowerBound não é chamado recursivamente.
    static int  activeCols[MAXCOL];
    static bool coveredLB [MAXCOL];

    int nActive = 0;
    for (int c = rightN[header]; c != header; c = rightN[c])
    {
        coveredLB[c]        = false;
        activeCols[nActive++] = c;
    }

    // Ordena por colSize crescente (mais restrita = menos opções = prioridade)
    sort(activeCols, activeCols + nActive,
         [](int a, int b){ return colSize[a] < colSize[b]; });

    int lb = 0;

    for (int i = 0; i < nActive; i++)
    {
        int c = activeCols[i];
        if (coveredLB[c]) continue;

        int  bestCostLB = INT_MAX;
        int  bestRow    = -1;

        for (int r = downN[c]; r != c; r = downN[r])
        {
            int row = rowID[r];
            if (usedInBranch[row]) continue;
            if (rowCost[row] < bestCostLB)
            {
                bestCostLB = rowCost[row];
                bestRow    = r;
            }
        }

        // Inviabilidade: coluna ativa sem nenhuma linha disponível
        if (bestRow == -1) return INT_MAX / 2;

        lb += bestCostLB;

        // Early-exit: lb parcial já suficiente para podar
        if (custoAtual + lb >= bestCost) return lb;

        // Marca colunas cobertas pelo conjunto escolhido (passe de LB)
        coveredLB[c] = true;
        for (int j = rightN[bestRow]; j != bestRow; j = rightN[j])
        {
            int col = colID[j];
            if (colAtiva[col])          // O(1) agora
                coveredLB[col] = true;
        }
    }

    return lb;
}

void search(int k, int MAX_K, int custoAtual)
{
    if (stopSearch) return;

    totalNos++;
    checkProgress();

    // =====================================================
    // PODA POR LOWER BOUND
    // (passa custoAtual para early-exit interno e evitar overflow)
    // =====================================================
    int lb = lowerBound(custoAtual);

    if (lb >= INT_MAX / 2 || custoAtual + lb >= bestCost)
    {
        totalPodas++;
        return;
    }

    // =====================================================
    // SOLUÇÃO COMPLETA
    // =====================================================
    if (rightN[header] == header)
    {
        printMelhorSolucao();

        bestCost    = custoAtual;
        bestSolSize = solSize;

        for (int i = 0; i < solSize; i++)
            bestSol[i] = solution[i];

        return;
    }

    // =====================================================
    // PODA POR PROFUNDIDADE
    // =====================================================
    if (k >= MAX_K)
        return;

    // =====================================================
    // ESCOLHE COLUNA (heurística S melhorada)
    // =====================================================
    int c = chooseColumn();

    // =====================================================
    // COLUNA IMPOSSÍVEL
    // =====================================================
    if (colSize[c] == 0)
    {
        totalPodas++;
        return;
    }

    // =====================================================
    // REMOVE COLUNA ESCOLHIDA
    // =====================================================
    cover(c);
    colSize[c]--;

    // =====================================================
    // ORDENA LINHAS CANDIDATAS
    //
    // score = custo / novasCoberturas
    //
    // menor score = melhor
    // =====================================================
    // Array na stack — evita heap alloc (new/delete) a cada nó visitado.
    // MAXROW = 1001 → ~16 KB por frame; profundidade máxima ~30 → seguro.
    pair<double,int> candidatos[MAXROW];
    int numCandidatos = 0;

    for (int r = downN[c]; r != c; r = downN[r])
    {
        int row = rowID[r];

        if (usedInBranch[row])
            continue;

        int novasCoberturas = 1; // conta a própria coluna c

        for (int j = rightN[r]; j != r; j = rightN[j])
        {
            if (colunaAtiva(colID[j]))
                novasCoberturas++;
        }

        double score =
            (double)rowCost[row] / novasCoberturas;

        candidatos[numCandidatos++] = {score, r};
    }

    // menor score primeiro
    sort(candidatos, candidatos + numCandidatos);

    // =====================================================
    // EXPLORA EM ORDEM INTELIGENTE
    // =====================================================
    for (int ci = 0; ci < numCandidatos; ci++)
    {
        auto &[score, r] = candidatos[ci];
        if (stopSearch)
            break;

        int row = rowID[r];

        int novoCusto = custoAtual + rowCost[row];

        // =================================================
        // PODA POR CUSTO
        // =================================================
        if (novoCusto >= bestCost)
        {
            totalPodas++;
            continue;
        }

        // =================================================
        // ESCOLHE LINHA
        // =================================================
        usedInBranch[row] = true;

        solution[solSize++] = r;

        // =================================================
        // COBRE COLUNAS DA LINHA
        // =================================================
        // Array na stack para colunas cobertas por esta linha
        int cobertas[MAXCOL];
        int numCobertas = 0;

        for (int j = rightN[r]; j != r; j = rightN[j])
        {
            int col = colID[j];

            if (!colunaAtiva(col))
                continue;

            colSize[col]--;

            cover(col);

            cobertas[numCobertas++] = col;
        }

        // =================================================
        // RECURSÃO
        // =================================================
        search(k + 1, MAX_K, novoCusto);

        // =================================================
        // RESTAURA
        // =================================================
        for (int i = numCobertas - 1; i >= 0; i--)
        {
            uncover(cobertas[i]);
            colSize[cobertas[i]]++;
        }

        solSize--;

        usedInBranch[row] = false;
    }

    // =====================================================
    // RESTAURA COLUNA PRINCIPAL
    // =====================================================
    colSize[c]++;
    uncover(c);
}

// ============================================================
//  initDLX — inicializa a lista circular de cabeçalhos
// ============================================================
void initDLX(int cols)
{
    nElems = cols;
    for (int i = 0; i <= cols; i++)
    {
        leftN[i]  = i - 1;
        rightN[i] = i + 1;
        upN[i] = downN[i] = i;
        colSize[i] = 0;
        colAtiva[i] = (i > 0);  // colunas 1..cols ativas; 0 = header, não é coluna
    }
    colAtiva[0] = false; // header não é coluna de elemento
    leftN[0]     = cols;
    rightN[cols] = 0;
    nodeCount    = cols + 1;
    memset(usedInBranch, false, sizeof(usedInBranch));
}

// ============================================================
//  addNode — insere nó (conjunto r, elemento c) na estrutura
//
//  Pré-condição: todos os nós do mesmo conjunto r devem ser
//  adicionados consecutivamente para que o encadeamento
//  horizontal funcione corretamente.
// ============================================================
void addNode(int r, int c)
{
    int node = nodeCount++;

    // Verificação de overflow de buffer
    if (node >= MAXNODE)
    {
        cerr << "[ERRO] MAXNODE atingido! Aumente a constante e recompile.\n";
        exit(1);
    }

    rowID[node] = r;
    colID[node] = c;
    colSize[c]++;

    // Insere no topo da lista vertical da coluna c
    downN[node]   = downN[c];
    upN[node]     = c;
    upN[downN[c]] = node;
    downN[c]      = node;

    // Encadeamento horizontal: verifica se o nó anterior
    // pertence ao mesmo conjunto
    if (rowID[node - 1] != r)
    {
        // Primeiro nó deste conjunto → lista horizontal singleton
        leftN[node] = rightN[node] = node;
    }
    else
    {
        // Insere à esquerda do nó anterior do mesmo conjunto
        leftN[node]          = leftN[node - 1];
        rightN[node]         = node - 1;
        rightN[leftN[node]]  = node;
        leftN[node - 1]      = node;
    }
}

// ============================================================
//  lerInstanciaORLibrary — parser para formato Beasley 1987
//
//  Retorna false se o arquivo não pôde ser aberto.
//  Inverte a cobertura lida por elemento para construir a DLX
//  por conjunto (necessário para o encadeamento horizontal).
// ============================================================
bool lerInstanciaORLibrary(const string& caminho)
{
    ifstream in(caminho);
    if (!in.is_open())
    {
        cerr << "[ERRO] Nao foi possivel abrir: " << caminho << "\n";
        return false;
    }

    int m, n;
    if (!(in >> m >> n))
    {
        cerr << "[ERRO] Falha ao ler m e n.\n";
        return false;
    }
    nSets = n;

    cout << "[LEITURA] m=" << m << " elementos | n=" << n << " conjuntos\n";

    // --- Lê os n custos ---
    vector<int> cost(n + 1);
    for (int j = 1; j <= n; j++)
    {
        if (!(in >> cost[j]))
        {
            cerr << "[ERRO] Falha ao ler custo do conjunto " << j << "\n";
            return false;
        }
    }
    cout << "[LEITURA] Custos lidos. Faixa: "
         << *min_element(cost.begin() + 1, cost.end()) << ".."
         << *max_element(cost.begin() + 1, cost.end()) << "\n";

    // --- Lê a cobertura por elemento e inverte para por conjunto ---
    // setCoverage[j] = lista de elementos cobertos pelo conjunto j
    vector<vector<int>> setCoverage(n + 1);
    int totalNos_dados = 0;

    for (int i = 1; i <= m; i++)
    {
        int k;
        if (!(in >> k))
        {
            cerr << "[ERRO] Falha ao ler contagem de conjuntos do elemento " << i << "\n";
            return false;
        }
        for (int t = 0; t < k; t++)
        {
            int j;
            if (!(in >> j))
            {
                cerr << "[ERRO] Falha ao ler conjunto #" << t+1
                     << " do elemento " << i << "\n";
                return false;
            }
            if (j < 1 || j > n)
            {
                cerr << "[ERRO] Indice de conjunto fora do range: " << j
                     << " (esperado 1.." << n << ")\n";
                return false;
            }
            setCoverage[j].push_back(i);
            totalNos_dados++;
        }
    }
    cout << "[LEITURA] Cobertura lida. Total de 1s na matriz: "
         << totalNos_dados << "\n";

    // --- Monta a estrutura DLX ---
    // DLX colunas = elementos (1..m)
    // DLX linhas  = conjuntos (1..n)
    initDLX(m);

    for (int j = 1; j <= n; j++)
    {
        rowCost[j] = cost[j];
        // Ordena elementos para garantir encadeamento horizontal determinístico
        sort(setCoverage[j].begin(), setCoverage[j].end());
        for (int elem : setCoverage[j])
            addNode(j, elem);
    }

    cout << "[LEITURA] DLX construida. Nos totais: " << nodeCount
         << " (max=" << MAXNODE << ")\n\n";
    return true;
}

// ============================================================
//  main
// ============================================================
int main()
{
    const string caminho = "scp41.txt";

    cout << "iniciando set cover";

    if (!lerInstanciaORLibrary(caminho))
        return 1;

    cout << "Instancia carregada com sucesso!\n";
    cout << "Elementos (DLX colunas) : " << nElems << "\n";
    cout << "Conjuntos (DLX linhas)  : " << nSets  << "\n";
    cout << "Limite de tempo         : " << TIME_LIMIT_SEC << " s\n\n";

    // -------------------------------------------------------
    //  Timer inicia aqui — exatamente como CPXXmipopt() no CPLEX
    //  (após leitura/configuração, somente o solver é medido)
    // -------------------------------------------------------
    gTimer.start();

    search(0, nElems, 0);

    double tWallTotal = gTimer.elapsedWall();
    double tCPUTotal  = gTimer.elapsedCPU();
    // -------------------------------------------------------

    // --- Resultado final ---
    cout << "\n=== RESULTADO FINAL ===\n";
    if (bestSolSize == 0)
    {
        cout << "Nenhuma solucao encontrada";
        if (stopSearch) cout << " (tempo esgotado antes da primeira solucao)";
        cout << ".\n";
    }
    else
    {
        cout << "Custo minimo : " << bestCost << "\n";
        cout << "Num. conjuntos: " << bestSolSize << "\n";
        cout << "Conjuntos escolhidos: ";
        for (int i = 0; i < bestSolSize; i++)
            cout << rowID[bestSol[i]] << " ";
        cout << "\n";
        if (stopSearch)
            cout << "[AVISO] Solucao pode nao ser otima — busca interrompida por time-limit.\n";
    }

    cout << "\nEstatisticas de busca:\n";
    cout << "  Nos explorados : " << totalNos   << "\n";
    cout << "  Podas          : " << totalPodas << "\n";

    printTimingSummary(tWallTotal, tCPUTotal);

    return 0;
}